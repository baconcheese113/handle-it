#include <ArduinoBLE.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ArduinoLowPower.h>
#include <./hub/Utilities.h>
#include <./hub/Network.h>
#include <./hub/Location.h>

const int VERSION = 1;

const char* DEVICE_NAME = "HandleIt Hub";

const char* PERIPHERAL_NAME = "HandleIt Client";
const char* SENSOR_SERVICE_UUID = "1000181a-0000-1000-8000-00805f9b34fb";
const char* VOLT_CHARACTERISTIC_UUID = "10002A58-0000-1000-8000-00805f9b34fb";

const char* BATTERY_SERVICE_UUID = "0000180f-0000-1000-8000-00805f9b34fb";
const char* BATTERY_LEVEL_CHARACTERISTIC_UUID = "00002a19-0000-1000-8000-00805f9b34fb";

const char* HUB_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fc";
const char* COMMAND_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fd";
const char* TRANSFER_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fe";
const char* FIRMWARE_CHARACTERISTIC_UUID = "2A26";

const char* COMMAND_START_SENSOR_SEARCH = "StartSensorSearch";
const char* COMMAND_SENSOR_CONNECT = "SensorConnect";

const uint16_t CHUNK_SIZE = 250;
BLEService hubService = BLEService(HUB_SERVICE_UUID);
BLEStringCharacteristic commandChar(COMMAND_CHARACTERISTIC_UUID, BLERead | BLEWrite, 30);
BLECharacteristic transferChar(TRANSFER_CHARACTERISTIC_UUID, BLERead | BLEWrite, CHUNK_SIZE);
BLEIntCharacteristic firmwareChar(FIRMWARE_CHARACTERISTIC_UUID, BLERead);

BLEService battService = BLEService(BATTERY_SERVICE_UUID);
BLEIntCharacteristic battLevelChar(BATTERY_LEVEL_CHARACTERISTIC_UUID, BLERead | BLEWrite);

char deviceImei[20]{};

const unsigned long BLE_ADV_DURATION = 60 * 1000;
const unsigned long BLE_SCAN_INTERVAL = 10 * 1000;
const unsigned long BLE_SCAN_DURATION = 1 * 1000;
const unsigned long BLE_COOLDOWN = 20 * 1000;

const unsigned long BATT_UPDATE_INTERVAL = 30 * 60 * 1000;

BLEDevice* peripheral;
bool isAddingNewSensor = false;
bool isScanning = false;
unsigned long lastScanTime = 0;
unsigned long lastEventTime = 0;
unsigned long lastBatteryUpdateTime = 0;

unsigned long advStartTime = 0;
unsigned long pairButtonHoldStartTime = 0;
BLEDevice* phone;

Network network;
Location location;

Command currentCommand;
String lastReadCommand = "";

String knownSensorAddrs[10];
uint8_t knownSensorAddrsLen = 0;
int32_t lastReadVoltage = 0;

void setAdvMode(bool turnOn) {
  if (turnOn && advStartTime == 0) {
    if (!isScanning) Utilities::setBlePower(true);
    Serial.println("Now advertising");
    BLE.advertise();
  } else if (!turnOn) {
    advStartTime = 0;
    BLE.stopAdvertise();
    Serial.println("Stopped advertising");
  }
}

void onBLEConnected(BLEDevice d) {
  Serial.print("\n>>> BLEConnected to: ");
  Serial.println(d.address());

  // d not populated with localName for some reason
  bool dNameMatch = peripheral && peripheral->address().compareTo(d.address()) == 0;

  setAdvMode(false);
  if (dNameMatch) {
    // if we just connectd to a peripheral then there's nothing else to do
    Serial.println("Connected to a sensor");
    return;
  }
  Serial.println("Connected to a phone");
  phone = new BLEDevice();
  *phone = d;
  digitalWrite(LED_BUILTIN, HIGH);
  if (network.tokenData.isValid) {
    Serial.println("Already have accessToken");
    return;
  }
  // Grab access_token from userId of connected phone
  char rawCommand[30]{};
  Serial.print("Trying to read userid...");
  while (strlen(rawCommand) < 1 && phone) {
    // Required to allow the phone to finish connecting properly
    BLE.poll();
    if (commandChar.written()) {
      String writtenVal = commandChar.value();
      writtenVal.toCharArray(rawCommand, 30);
    }
    Serial.print(".");
    Utilities::bleDelay(50, &BLE);
  }
  if (!phone) return;
  Serial.print("\nUserID value: ");
  Serial.println(rawCommand);

  Command command = Utilities::parseRawCommand(rawCommand);
  if (strcmp(command.type, "UserId") != 0) {
    Serial.print("Error: command.type is not equal to UserId, command.type: ");
    Serial.println(command.type);
    return;
  } else {
    Serial.println("command.type is UserId");
  }

  if (!network.setPowerOnAndWaitForReg(&BLE)) return;
  BLE.poll(); // helps recover from starting up

  char loginMutationStr[100 + strlen(command.value) + strlen(deviceImei)]{};
  sprintf(loginMutationStr, "{\"query\":\"mutation loginAsHub{loginAsHub(userId:%s, serial:\\\"%s\\\", imei:\\\"%s\\\")}\",\"variables\":{}}", command.value, BLE.address().c_str(), deviceImei);
  DynamicJsonDocument loginDoc = network.SendRequest(loginMutationStr, &BLE);
  if (loginDoc["data"] && loginDoc["data"]["loginAsHub"]) {
    const char* token = (const char*)(loginDoc["data"]["loginAsHub"]);
    network.SetAccessToken(token);
    Serial.print("token is: ");
    Serial.println(token);
    Serial.print("network.accessToken is: ");
    Serial.println(network.tokenData.accessToken);
    Serial.print("And strlen: ");
    Serial.println(strlen(network.tokenData.accessToken));
    // TODO check if token is different from existing token in flash storage, if so replace it
    // cmaglie/FlashStorage
  } else {
    Serial.println("Error reading token");
    network.setPower(false);
    return;
  }

  char getHubQueryStr[] = "{\"query\":\"query getHubViewer{hubViewer{id}}\",\"variables\":{}}";
  DynamicJsonDocument hubViewerDoc = network.SendRequest(getHubQueryStr, &BLE);
  if (hubViewerDoc["data"] && hubViewerDoc["data"]["hubViewer"]) {
    const uint16_t id = (const uint16_t)(hubViewerDoc["data"]["hubViewer"]["id"]);
    Serial.print("getHubViewer id: ");
    Serial.println(id);
    String hubCommand = "HubId:";
    hubCommand.concat(id);
    Serial.print("Wrote HubId command back to phone: ");
    Serial.println(hubCommand);
    commandChar.writeValue(hubCommand);
    Utilities::bleDelay(5000, &BLE);
    setAdvMode(false);
  } else {
    Serial.println("Error getting hubId");
  }
  network.setPower(false);
}

void onBLEDisconnected(BLEDevice d) {
  Serial.print("\n>>> BLEDisconnecting from: ");
  Serial.println(d.address());
  if (phone) {
    Serial.print("Phone address: ");
    Serial.println(phone->address());
  }
  if (peripheral) {
    Serial.print("Peripheral address: ");
    Serial.println(peripheral->address());
  }
  digitalWrite(LED_BUILTIN, LOW);
  if (peripheral && peripheral->address() == d.address()) {
    Serial.println("Peripheral disconnected");
    Utilities::analogWriteRGB(0, 0, 0);
    delete peripheral;
    peripheral = nullptr;
  } else if (phone && phone->address() == d.address()) {
    Serial.println("Phone disconnected");
    delete phone;
    phone = nullptr;
    isAddingNewSensor = false;
    memset(currentCommand.type, 0, sizeof currentCommand.type);
    memset(currentCommand.value, 0, sizeof currentCommand.value);
    commandChar.writeValue("");
  }
}

bool initializeBLE() {
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");
    return false;
  }
  // BLE service to advertise to phone
  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(hubService);
  hubService.addCharacteristic(commandChar);
  hubService.addCharacteristic(transferChar);
  hubService.addCharacteristic(firmwareChar);
  BLE.addService(hubService);
  firmwareChar.writeValue(VERSION);
  battService.addCharacteristic(battLevelChar);
  BLE.addService(battService);

  // Bluetooth LE connection handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);
  BLE.stopAdvertise();
  Serial.print("BLE address: ");
  Serial.println(BLE.address());
  return true;
}

void setup() {
  Utilities::setupPins();

  Utilities::analogWriteRGB(0, 0, 0);
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Booting...");
  Serial1.begin(115200);
  while (!Serial1);
  Serial.println("Serial1 started at 115200 baud");
  while (Serial1.available()) Serial1.read();

  // while(true) {
  //   if(Serial.available()) {
  //     char c = Serial.read();
  //     if(c == 'p') {
  //       Serial.print("isPoweredOn: ");
  //       Serial.print(network.isPoweredOn());
  //     }
  //     if(c == 'y') network.setPower(true);
  //     if(c == 'n') network.setPower(false);
  //     if(c == 'w') network.waitForPowerOn();
  //     if(c == 'e') network.setPowerOnAndWaitForReg();
  //     if(c == 'b') network.setPowerOnAndWaitForReg(&BLE);
  //   }
  // }

  network.setPower(true);
  network.waitForPowerOn();
  while (strlen(deviceImei) < 1) {
    network.GetImei(deviceImei);
    Serial.print("Device IMEI: ");
    Serial.println(deviceImei);
  }
  location.setGPSPower(false);

  // begin BLE initialization
  if (!initializeBLE()) while (true);
  Serial.print("Sketch version ");
  Serial.print(VERSION);
  Serial.print(". Free Memory is: ");
  Serial.println(Utilities::freeMemory());

  network.InitializeAccessToken();

  if (network.tokenData.isValid && network.setPowerOnAndWaitForReg()) {
    char sensorQuery[] = "{\"query\":\"query getMySensors{hubViewer{sensors{serial}}}\",\"variables\":{}}";
    DynamicJsonDocument doc = network.SendRequest(sensorQuery, &BLE);
    if (doc["data"] && doc["data"]["hubViewer"] && doc["data"]["hubViewer"]["sensors"]) {
      const JsonArrayConst sensors = doc["data"]["hubViewer"]["sensors"];
      if (sensors.size()) {
        for (uint8_t i = 0; i < sensors.size(); i++) {
          knownSensorAddrs[i] = String((const char*)sensors[i]["serial"]);
          knownSensorAddrsLen++;
          Serial.print(knownSensorAddrs[i]);
          Serial.print(" is knownSensorAddrs at idx: ");
          Serial.println(i);
        }
      }
    } else {
      Serial.print("Get sensors failed, but accessToken strlen is: ");
      Serial.println(strlen(network.tokenData.accessToken));
    }
  }
  network.setPower(false);
}

void CheckInput() {

  bool pressingPairButton = digitalRead(PAIR_PIN) == HIGH;
  if (!pressingPairButton) {
    pairButtonHoldStartTime = 0;
    return;
  }
  if (advStartTime > 0) return;

  if (pairButtonHoldStartTime == 0) {
    pairButtonHoldStartTime = millis();
  } else if (millis() > pairButtonHoldStartTime + 3000) {
    // enter pair mode
    setAdvMode(true);
    advStartTime = millis();
    // Warm up SIM module
    network.setPower(true);
  }
}

void PairToPhone() {
  if (millis() > advStartTime + BLE_ADV_DURATION) {
    // pairing timed out
    setAdvMode(false);
    network.setPower(false);
    Utilities::analogWriteRGB(255, 0, 0);
    return;
  }
  if (millis() / 1000 % 2) Utilities::analogWriteRGB(75, 0, 130);
  else Utilities::analogWriteRGB(75, 0, 80);

  // Must be called while pairing so characteristics are available
  BLE.poll();
}

void ListenForPhoneCommands() {
  if (!phone || !commandChar.written()) return;
  // Grab access_token from userId of connected phone
  char rawCommand[30]{};
  String writtenVal = commandChar.value();
  if (writtenVal == lastReadCommand) return;
  lastReadCommand = writtenVal;

  // happens when clearing char and cancelling form
  if (writtenVal.length() <= 1) {
    isAddingNewSensor = false;
    return;
  }
  writtenVal.toCharArray(rawCommand, 30);

  Serial.print("\nCommand value: ");
  Serial.println(rawCommand);

  currentCommand = Utilities::parseRawCommand(rawCommand);

  if (strcmp(currentCommand.type, COMMAND_START_SENSOR_SEARCH) == 0) {
    Serial.println("Now adding new sensor");
    isAddingNewSensor = true;
  }

  Serial.print("Parsed command type: ");
  Serial.println(currentCommand.type);
  Serial.print("Parsed command value: ");
  Serial.println(currentCommand.value);
}

// Returns battery level represented from 0 - 100
double getBatteryLevel(double volts) {
  if (volts > 845) return 100.0;
  if (volts > 780) {
    return (-.00280590 * pow(volts, 2)) + (4.84906009 * volts) - 1994.38823741;
  }
  if (volts > 759) {
    return (-.00732943 * pow(volts, 3)) + (16.96151031 * pow(volts, 2)) - (13080.35413096 * volts) + 3361569.81985325;
  }
  if (volts > 725) {
    return (.01635 * pow(volts, 2)) - (23.57544 * volts) + 8499.67268;
  }
  return 0.0;
}

void UpdateBatteryLevel() {
  double avgVoltage = 0;
  uint8_t sampleSize = 90;
  for (uint8_t i = 0; i < sampleSize; i++) {
    avgVoltage += analogRead(BATT_PIN);
    if (i % 5 == 0) delay(1);
  }
  avgVoltage /= sampleSize;
  double level = getBatteryLevel(avgVoltage);
  Serial.print("avgVoltage is: ");
  Serial.print(avgVoltage);
  Serial.print(", level: ");
  Serial.println(level);
  battLevelChar.writeValue((uint8_t)round(level));

  lastBatteryUpdateTime = millis();
  if (!network.tokenData.isValid || !network.setPowerOnAndWaitForReg()) return;

  char updateHubBatteryLevel[150]{};
  sprintf(updateHubBatteryLevel, "{\"query\":\"mutation UpdateHubBatteryLevel{updateHubBatteryLevel(volts:%.2f, percent:%.2f){ id }}\",\"variables\":{}}", avgVoltage, level);
  DynamicJsonDocument doc = network.SendRequest(updateHubBatteryLevel, &BLE);
  if (doc["data"] && doc["data"]["updateHubBatteryLevel"]) {
    const uint16_t id = (const uint16_t)(doc["data"]["updateHubBatteryLevel"]["id"]);
    Serial.print("updatedHubBatteryLevel hubId is: ");
    Serial.println(id);
  } else {
    Serial.println("error parsing doc");
  }
  network.setPower(false);
}

void ScanForSensor() {
  // FIXME need to advertise during cooldown, so this check should be different
  if (advStartTime > 0) return;
  if (lastEventTime > 0) {
    if (millis() < lastEventTime + BLE_COOLDOWN) {
      Serial.print("-");
      BLE.poll();
    } else {
      lastEventTime = 0;
      setAdvMode(false);
      Utilities::setBlePower(false);
      Serial.println(">\nCooldown complete");
    }
    return;
  }
  if (!isScanning && (millis() > lastScanTime + BLE_SCAN_INTERVAL || lastScanTime == 0)) {
    // this was the first call to start scanning
    if (!phone) Utilities::setBlePower(true);
    BLE.scanForName(PERIPHERAL_NAME, true);
    lastScanTime = millis();
    isScanning = true;
    Utilities::analogWriteRGB(255, 0, 0, false);
    Serial.print("Hub scanning for peripheral...");
  } else if (!phone && isScanning && millis() > lastScanTime + BLE_SCAN_DURATION) {
    Serial.println("😴💤");
    BLE.stopScan();
    Utilities::setBlePower(false);
    Utilities::analogWriteRGB(0, 0, 0, false);
    isScanning = false;
  }
  if (!isScanning) return;

  BLEDevice scannedDevice = BLE.available();
  if (scannedDevice.hasLocalName()) {
    Serial.print("Scanned: (localName) ");
    Serial.println(scannedDevice.localName());
  } else if (scannedDevice.deviceName().length() > 0) {
    Serial.print("Scanned: (deviceName) ");
    Serial.println(scannedDevice.deviceName());
  } else {
    Serial.print(".");
  }
  bool isPeripheral = scannedDevice.deviceName() == PERIPHERAL_NAME || scannedDevice.localName() == PERIPHERAL_NAME;
  if (!isPeripheral) return;
  Serial.print("\nFound possible sensor: ");
  Serial.println(scannedDevice.address());

  bool isKnownSensor = false;
  for (uint8_t i = 0; i < knownSensorAddrsLen; i++) {
    Serial.print("Checking for a match with: ");
    Serial.println(knownSensorAddrs[i]);
    if (scannedDevice.address() == knownSensorAddrs[i]) {
      isKnownSensor = true;
      break;
    }
  }
  // if we're not adding new sensors and it's unknown
  if (!isAddingNewSensor && !isKnownSensor) {
    Serial.println("Sensor not paired to this hub");
    return;
  }
  // if we're adding new sensors and it's already added
  if (isAddingNewSensor && isKnownSensor) {
    Serial.println("Sensor already added to this hub");
    return;
  }

  // We found a Sensor!
  peripheral = new BLEDevice();
  *peripheral = scannedDevice;
  Utilities::analogWriteRGB(255, 30, 0);
  Serial.println("\nPERIPHERAL FOUND");
  Serial.print("Address found: ");
  Serial.println(peripheral->address());
  Serial.print("Local Name: ");
  Serial.println(peripheral->localName());
  Serial.print("Device Name: ");
  Serial.println(peripheral->deviceName());
  Serial.print("Advertised Service UUID: ");
  Serial.println(peripheral->advertisedServiceUuid());
  Serial.print("Advertised Service UUID Count: ");
  Serial.println(peripheral->advertisedServiceUuidCount());
  BLE.stopScan();
  isScanning = false;

  if (isAddingNewSensor) {
    Serial.print("Waiting for command to connect~~~");
    String sensorFound = "SensorFound:";
    sensorFound.concat(peripheral->address());
    commandChar.writeValue(sensorFound);
    memset(currentCommand.type, 0, sizeof currentCommand.type);
    memset(currentCommand.value, 0, sizeof currentCommand.value);
  }
}

void ConnectToFoundSensor() {
  if (isAddingNewSensor && strcmp(currentCommand.type, COMMAND_SENSOR_CONNECT) != 0) {
    // TODO handle the form timing out at this location better
    BLE.poll();
    Serial.print("~");
    return;
  }
  if (!peripheral->connect()) {
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.println("\nFailed to connect, resetting....");
    delete peripheral;
    peripheral = nullptr;
    Utilities::bleDelay(1000, &BLE);
    return;
  }

  // We're connected to sensor!
  Utilities::analogWriteRGB(255, 100, 200);
  Serial.println("\nPeripheral connected!");
  Serial.println(peripheral->discoverService(SENSOR_SERVICE_UUID));
  // FIXME discoverAttributes should work quickly
  // Serial.println(peripheral->discoverAttributes());
  Serial.print("Service count: ");
  Serial.println(peripheral->serviceCount());
  Serial.print("Appearance: ");
  Serial.println(peripheral->appearance());
  Serial.print("Has force service: ");
  Serial.println(peripheral->hasService(SENSOR_SERVICE_UUID));
  Serial.print("Has volts: ");
  Serial.println(peripheral->hasCharacteristic(VOLT_CHARACTERISTIC_UUID));

  if (!isAddingNewSensor) return;

  if (!network.setPowerOnAndWaitForReg(&BLE)) {
    peripheral->disconnect();
    return;
  }

  const char* sensorSerial = peripheral->address().c_str();
  char mutationStr[155 + strlen(sensorSerial)]{};
  sprintf(mutationStr, "{\"query\":\"mutation createSensor{createSensor(doorColumn: 0, doorRow: 0, isOpen: false, isConnected: true, serial:\\\"%s\\\"){id}}\",\"variables\":{}}", sensorSerial);
  DynamicJsonDocument doc = network.SendRequest(mutationStr, &BLE);
  if (doc["data"] && doc["data"]["createSensor"]) {
    const uint16_t id = (const uint16_t)(doc["data"]["createSensor"]["id"]);
    Serial.print("createSensor id: ");
    Serial.println(id);
    Serial.print("Adding to knownSensorAddrs: ");
    Serial.println(peripheral->address());
    knownSensorAddrs[knownSensorAddrsLen] = peripheral->address();
    knownSensorAddrsLen++;
    if (peripheral) peripheral->disconnect();
    if (phone) {
      commandChar.writeValue("SensorAdded:1");
      Utilities::bleDelay(2000, &BLE);
    }
    Serial.print("Cooling down to prevent peripheral reconnection---");
    setAdvMode(false);
    lastEventTime = millis();
    lastScanTime = lastEventTime + BLE_COOLDOWN;
  } else {
    Serial.println("doc not valid");
  }

  network.setPower(false);
  memset(currentCommand.type, 0, sizeof currentCommand.type);
  memset(currentCommand.value, 0, sizeof currentCommand.value);
}

void MonitorSensor() {
  BLEService forceService = peripheral->service(SENSOR_SERVICE_UUID);
  bool hasVolts = forceService.hasCharacteristic(VOLT_CHARACTERISTIC_UUID);
  Serial.print("Has volts: ");
  Serial.println(hasVolts);
  // if(!hasVolts) return;
  BLECharacteristic volts = forceService.characteristic(VOLT_CHARACTERISTIC_UUID);
  Serial.print("Characteristic value length: ");
  Serial.println(volts.valueLength());
  Serial.print("Characteristic descriptor: ");
  Serial.println(volts.descriptor(0));
  Serial.print("Can read: ");
  Serial.println(volts.canRead());
  Serial.print("Can subscribe: ");
  Serial.println(volts.canSubscribe());

  // if(volts.canRead()) {
    // int32_t voltage = 0;
    // volts.readValue(voltage);
    // Serial.print("Volts value: ");
    // Serial.println(voltage);
  if (!network.setPowerOnAndWaitForReg(&BLE)) {
    peripheral->disconnect();
    network.setPower(false);
    return;
  }
  BLE.poll();
  const char* address = peripheral->address().c_str();
  char createEvent[100 + strlen(address)]{};
  // FIXME phone not seeing hub after event
  setAdvMode(true);
  sprintf(createEvent, "{\"query\":\"mutation CreateEvent{createEvent(serial:\\\"%s\\\"){ id }}\",\"variables\":{}}\n", address);
  DynamicJsonDocument doc = network.SendRequest(createEvent, &BLE);
  if (doc["data"] && doc["data"]["createEvent"]) {
    const uint16_t id = (const uint16_t)(doc["data"]["createEvent"]["id"]);
    Serial.print("created event id is: ");
    Serial.println(id);
    // lastReadVoltage = voltage;
  } else {
    Serial.println("error parsing doc");
  }
  network.setPower(false);
  if (peripheral) peripheral->disconnect();
  Serial.print("Cooling down to prevent peripheral reconnection---");
  lastEventTime = millis();
  lastScanTime = lastEventTime + BLE_COOLDOWN;
  // } 
  // else {
  //   Serial.println("Unable to read volts");
  // }
}

void FirmwareUpdate() {
  unsigned long fileLength = strtoul(currentCommand.value, NULL, 10);
  if (fileLength == 0) {
    Serial.println("Phone didn't provide fileLength with command. Can't continue with update.");
    return;
  }
  Serial.print("Phone returned update file of size ");
  Serial.print(fileLength);
  Serial.println(" bytes");

  if (!InternalStorage.open(fileLength)) {
    Serial.println("There is not enough space to store the update. Can't continue with update.");
    return;
  }

  uint8_t buff[CHUNK_SIZE];
  uint16_t chunkLen = 0;
  uint8_t emptyByteArr[1] = { 0x00 };
  while (fileLength > 0 && phone && phone->connected()) {
    BLE.available();
    chunkLen = transferChar.valueLength();
    if (chunkLen < min(CHUNK_SIZE, fileLength)) {
      continue;
    }
    transferChar.readValue(buff, chunkLen);
    Serial.print("\nValue length is ");
    Serial.print(chunkLen);
    Serial.print(" and content size is ");
    Serial.print(sizeof(buff));
    Serial.print(" and Free Memory is: ");
    Serial.println(Utilities::freeMemory());
    transferChar.writeValue(emptyByteArr, 1, true);
    BLE.available();


    for (uint8_t i = 0; i < chunkLen; i++) {
      InternalStorage.write(buff[i]);
      fileLength--;
    }
    Serial.print("Bytes left to write: ");
    Serial.println(fileLength);
  }
  InternalStorage.close();
  if (fileLength > 0) {
    Serial.print("Timeout downloading update file at ");
    Serial.print(fileLength);
    Serial.println(" bytes. Can't continue with update.");
    return;
  }

  String hubCommand = "HubUpdateEnd:";
  hubCommand.concat(VERSION + 1);
  Serial.print("Writing ");
  Serial.println(hubCommand);
  commandChar.writeValue(hubCommand);
  BLE.poll();
  delay(10);
  BLE.poll();
  Serial.println("Stalling...");

  Utilities::bleDelay(2000, &BLE);

  Serial.println("Sketch update apply and reset.");
  Serial.flush();
  InternalStorage.apply(); // this doesn't return
}

void UpdateGPS() {
  if (!network.tokenData.isValid) return;
  if (millis() < location.lastGPSTime + GPS_UPDATE_INTERVAL) return;
  if (millis() < location.lastGPSTime + GPS_UPDATE_INTERVAL + GPS_BUFFER_TIME) {
    if (!location.isPowered) {
      network.setPower(true);
      network.waitForPowerOn();
      location.setGPSPower(true);
    }
    return;
  }
  location.lastGPSTime = millis();
  Serial1.println("AT+CGNSINF");
  Serial1.flush();

  char infBuffer[200]{};
  memset(infBuffer, 0, 200);
  bool didRead = Utilities::readUntilResp("AT+CGNSINF\r\r\n+CGNSINF: ", infBuffer);
  if (!didRead) {
    location.setGPSPower(false);
    network.setPower(false);
    return;
  }

  Serial.print("\nBuffer: ");
  Serial.println(infBuffer);
  LocReading reading = location.parseInf(infBuffer);
  Serial.println("\n\r*****Updating GPS location*****");
  if (!reading.hasFix) {
    Serial.println("No GPS fix yet, aborting");
    location.setGPSPower(false);
    network.setPower(false);
    return;
  }
  location.printLocReading(reading);

  double dist = location.distanceFromLastPoint(reading.lat, reading.lng);
  if (dist < 20) {
    Serial.print("New location is less than 20m away from previously sent location, aborting.\nDistance(m): ");
    Serial.println(dist);
    location.setGPSPower(false);
    network.setPower(false);
    return;
  }

  char createLocation[200]{};
  sprintf(createLocation, "{\"query\":\"mutation CreateLocation{createLocation(lat:%.5f, lng: %.5f, hdop: %.2f, speed: %.2f, course: %.2f, age: 0){ id }}\",\"variables\":{}}", reading.lat, reading.lng, reading.hdop, reading.kmph, reading.deg);
  DynamicJsonDocument doc = network.SendRequest(createLocation, &BLE);
  if (doc["data"] && doc["data"]["createLocation"]) {
    const uint16_t id = (const uint16_t)(doc["data"]["createLocation"]["id"]);
    Serial.print("created location id is: ");
    Serial.println(id);
    location.lastSentReading = reading;
  } else {
    Serial.println("error parsing doc");
  }
  location.setGPSPower(false);
  network.setPower(false);
}

void loop() {

  CheckInput();

  if (strcmp(currentCommand.type, "StartHubUpdate") == 0) {
    FirmwareUpdate();
  }

  // Hub has entered pairing mode
  if (advStartTime > 0) {
    PairToPhone();
  } else if (phone) {
    ListenForPhoneCommands();
  }

  // Scan for peripheral
  if (!peripheral) {
    ScanForSensor();
  } else if (!peripheral->connected()) {
    ConnectToFoundSensor();
  } else {
    MonitorSensor();
  }

  // Update GPS
  if (!phone && !peripheral && advStartTime == 0) {
    UpdateGPS();
    if (lastBatteryUpdateTime == 0 || millis() > lastBatteryUpdateTime + BATT_UPDATE_INTERVAL) {
      UpdateBatteryLevel();
    }
  }

  if (isScanning) {
    if (Serial) Utilities::idle(20);
    else Utilities::idle(5);
  } else {
    if (Serial) Utilities::idle(200);
    else Utilities::idle(300);
  }
}
