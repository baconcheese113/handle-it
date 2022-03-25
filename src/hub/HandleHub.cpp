#include <ArduinoBLE.h>
#include <ArduinoJson.h>
#include <./hub/Utilities.h>
#include <./hub/Network.h>

#define D8 8
#define D4 4

// Serial number
const char* DEVICE_SERIAL = "RealSensorSerial0";
// Device name
const char* DEVICE_NAME = "HandleIt Hub";

const char* PERIPHERAL_NAME = "HandleIt Client";
const char* SENSOR_SERVICE_UUID = "1000181a-0000-1000-8000-00805f9b34fb";
const char* VOLT_CHARACTERISTIC_UUID = "10002A58-0000-1000-8000-00805f9b34fb";

const char* HUB_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fc";
const char* COMMAND_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fd";

const char* COMMAND_START_SENSOR_SEARCH = "StartSensorSearch";
const char* COMMAND_SENSOR_CONNECT = "SensorConnect";


BLEService hubService = BLEService(HUB_SERVICE_UUID);
BLEStringCharacteristic commandChar(COMMAND_CHARACTERISTIC_UUID, BLERead | BLEWrite, 20);

BLEDevice peripheral;
bool isScanning = false;
bool isAdvertising = false;

BLECharacteristic volts;

unsigned long pairingStartTime = 0;
unsigned long pairButtonHoldStartTime = 0;
BLEDevice* phone;

Network network;

Command currentCommand;
String lastReadCommand = "";

uint8_t autoConnectSensorId = 0;
int32_t lastReadVoltage = 0;

void onBLEConnected(BLEDevice d) {
  Serial.print(">>> BLEConnected to: ");
  Serial.println(d.address());
  if(d.deviceName() != PERIPHERAL_NAME) {
    phone = &d;
    pairingStartTime = 0;
    BLE.stopAdvertise();
    isAdvertising = false;
    digitalWrite(LED_BUILTIN, HIGH);
    if(strlen(network.accessToken) > 0) {
      Serial.println("Already have accessToken");
      return;
    }
    // Grab access_token from userId of connected phone
    char rawCommand[20]{};
    Serial.println("Trying to read userid...");
    while(strlen(rawCommand) < 1 && phone) {
      // Required to allow the phone to finish connecting properly
      BLE.available();
      if(commandChar.written()) {
        String writtenVal = commandChar.value();
        writtenVal.toCharArray(rawCommand, 20);
      }
      Serial.print(".");
      delay(100);
    }
    if(!phone) return;
    Serial.print("\nUserID value: ");
    Serial.println(rawCommand);

    Command command = Utilities::parseRawCommand(rawCommand);
    if(strcmp(command.type, "UserId") != 0) {
      Serial.print("Error: command.type is not equal to UserId, command.type: ");
      Serial.println(command.type);
      return;
    } else {
      Serial.println("command.type is UserId");
    }
    char loginMutationStr[100 + strlen(command.value) + strlen(DEVICE_SERIAL)]{};
    sprintf(loginMutationStr, "{\"query\":\"mutation loginAsHub{loginAsHub(userId:%s, serial:\\\"%s\\\")}\",\"variables\":{}}\n", command.value, DEVICE_SERIAL);
    DynamicJsonDocument loginDoc = network.SendRequest(loginMutationStr);
    if(loginDoc["data"] && loginDoc["data"]["loginAsHub"]) {
      const char* token = (const char *)(loginDoc["data"]["loginAsHub"]);
      network.SetAccessToken(token);
      Serial.print("token is: ");
      Serial.println(token);
      Serial.print("network.accessToken is: ");
      Serial.println(network.accessToken);
      // TODO check if token is different from existing token in flash storage, if so replace it
      // cmaglie/FlashStorage
    } else {
      Serial.println("Error reading token");
      return;
    }

    char getHubQueryStr[] = "{\"query\":\"query getHubViewer{hubViewer{id}}\",\"variables\":{}}";
    DynamicJsonDocument hubViewerDoc = network.SendRequest(getHubQueryStr);
    if(hubViewerDoc["data"] && hubViewerDoc["data"]["hubViewer"]) {
      const uint8_t id = (const uint8_t)(hubViewerDoc["data"]["hubViewer"]["id"]);
      Serial.print("getHubViewer id: ");
      Serial.println(id);
      String hubCommand = "HubId:";
      hubCommand.concat(id);
      Serial.print("Wrote HubId command back to phone: ");
      Serial.println(hubCommand);
      commandChar.writeValue(hubCommand);
    } else {
      Serial.println("Error getting hubId");
      return;
    }
  }
}

void onBLEDisconnected(BLEDevice d) {
  Serial.println(">>> BLEDisconnected");
  digitalWrite(LED_BUILTIN, LOW);
  if(phone && d.deviceName() != phone->deviceName()) {
    Serial.println("Phone disconnected");
    phone = NULL;
    memset(currentCommand.type, 0, sizeof currentCommand.type);
    memset(currentCommand.value, 0, sizeof currentCommand.value);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D8, INPUT);

  Utilities::analogWriteRGB(0, 0, 0);
  // Serial.begin(9600);
  // while(!Serial);
  Serial1.begin(9600);
  while(!Serial1);
   // begin BLE initialization
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");

    while (1);
  }
  Serial.println("Booting...");
  // BLE service to advertise to phone
  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(hubService);
  hubService.addCharacteristic(commandChar);
  BLE.addService(hubService);

  // Bluetooth LE connection handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);
  BLE.stopAdvertise();

  network.InitializeAccessToken();

  // TODO get serials of all sensors to autoConnect with
  if(strlen(network.accessToken)) {
    char sensorQuery[] = "{\"query\":\"query getMySensors{hubViewer{sensors{id}}}\",\"variables\":{}}";
    DynamicJsonDocument doc = network.SendRequest(sensorQuery);
    if(doc["data"] && doc["data"]["hubViewer"] && doc["data"]["hubViewer"]["sensors"]) {
      const JsonArrayConst sensors = doc["data"]["hubViewer"]["sensors"];
      if(sensors.size()) {
        autoConnectSensorId = sensors[0]["id"];
        Serial.print("Looking for existing sensor: ");
        Serial.println(autoConnectSensorId);
      }
    } else {
      Serial.print("Get sensors failed, but accessToken strlen is: ");
      Serial.println(strlen(network.accessToken));
    }
  } else {
    Serial.println("No accessToken found");
  }
}

void CheckInput() {
  
  bool pressingPairButton = digitalRead(D8) == HIGH;
  if(!pressingPairButton) {
    pairButtonHoldStartTime = 0;
    return;
  }
  
  if(pairButtonHoldStartTime == 0) {
    pairButtonHoldStartTime = millis();
  } 
  else if(millis() > pairButtonHoldStartTime + 5000) {
    // enter pair mode
    pairingStartTime = millis();
  }
}

void PairToPhone() {
  if(!strlen(network.accessToken) && millis() > pairingStartTime + 60000) {
    // pairing timed out
    pairingStartTime = 0;
    BLE.stopAdvertise();
    isAdvertising = false;
    Utilities::analogWriteRGB(255, 0, 0);
    return;
  }
  if(millis() / 1000 % 2) Utilities::analogWriteRGB(75, 0, 130);
  else Utilities::analogWriteRGB(75, 0, 80);

  if(!isAdvertising) {
    BLE.advertise();
    isAdvertising = true;  
  }
  // Must be called while pairing so characteristics are available
  BLE.available();
}

void ListenForPhoneCommands() {
  if(!phone || !commandChar.written()) return;
  // Grab access_token from userId of connected phone
  char rawCommand[30]{};
  String writtenVal = commandChar.value();
  if(writtenVal == lastReadCommand) return;
  lastReadCommand = writtenVal;
  writtenVal.toCharArray(rawCommand, 30);

  Serial.print("\nCommand value: ");
  Serial.println(rawCommand);

  currentCommand = Utilities::parseRawCommand(rawCommand);

  Serial.print("Parsed command type: ");
  Serial.println(currentCommand.type);
  Serial.print("Parsed command value: ");
  Serial.println(currentCommand.value);
}

void ScanForSensor() {
  if(!isScanning) {
    // this was the first call to start scanning
    BLE.scanForName(PERIPHERAL_NAME);
    isScanning = true;
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.print("Hub scanning for peripheral...");
  }
  BLEDevice scannedDevice = BLE.available();
  if(scannedDevice.deviceName().length()) Serial.println(scannedDevice.deviceName());
  Serial.print(".");
  bool isPeripheral = scannedDevice.deviceName() == PERIPHERAL_NAME || scannedDevice.localName() == PERIPHERAL_NAME;
  if (!isPeripheral) return;

  // We found a Sensor!
  peripheral = scannedDevice;
  Utilities::analogWriteRGB(255, 30, 0);
  Serial.println("\nPERIPHERAL FOUND");
  Serial.print("Address found: ");
  Serial.println(peripheral.address());
  Serial.print("Local Name: ");
  Serial.println(peripheral.localName());
  Serial.print("Device Name: ");
  Serial.println(peripheral.deviceName());
  Serial.print("Advertised Service UUID: ");
  Serial.println(peripheral.advertisedServiceUuid());
  Serial.print("Advertised Service UUID Count: ");
  Serial.println(peripheral.advertisedServiceUuidCount());
  BLE.stopScan();
  isScanning = false;
  
  commandChar.writeValue("SensorFound:1");
  memset(currentCommand.type, 0, sizeof currentCommand.type);
  memset(currentCommand.value, 0, sizeof currentCommand.value);
}

void ConnectToFoundSensor() {
  if(!peripheral.connect()) {
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.println("\nFailed to connect retrying....");
    delay(1000);
    return;
  }

  // We're connected to sensor!
  Utilities::analogWriteRGB(255, 100, 200);
  Serial.println("Peripheral connected!");
  Serial.print("Service count: ");
  Serial.println(peripheral.serviceCount());
  Serial.print("Appearance: ");
  Serial.println(peripheral.appearance());
  Serial.print("Has force service: ");
  Serial.println(peripheral.hasService(SENSOR_SERVICE_UUID));
  Serial.print("Discover the force: ");
  Serial.println(peripheral.discoverService(SENSOR_SERVICE_UUID));

  // TODO get sensor serial
  const char sensorSerial[] = "1";
  char mutationStr[155 + strlen(sensorSerial)]{};
  sprintf(mutationStr, "{\"query\":\"mutation createSensor{createSensor(doorColumn: 0, doorRow: 0, isOpen: true, isConnected: true, serial:\\\"%s\\\"){id}}\",\"variables\":{}}\n", sensorSerial);
  DynamicJsonDocument doc = network.SendRequest(mutationStr);
  if(doc["data"] && doc["data"]["createSensor"]) {
    const uint8_t id = (const uint8_t)(doc["data"]["createSensor"]["id"]);
    Serial.print("createSensor id: ");
    Serial.println(id);
    if(phone) {
      String sensorAdded = "SensorAdded:";
      sensorAdded.concat(id);
      commandChar.writeValue(sensorAdded);
      autoConnectSensorId = id;
    }
  } else {
    Serial.println("doc not valid");
  }
  
  memset(currentCommand.type, 0, sizeof currentCommand.type);
  memset(currentCommand.value, 0, sizeof currentCommand.value);
}

void MonitorSensor() {
  if(!volts) {
    // this was the first call to start monitoring
    BLEService forceService = peripheral.service(SENSOR_SERVICE_UUID);
    bool hasVolts = forceService.hasCharacteristic(VOLT_CHARACTERISTIC_UUID);
    Serial.print("Has volts: ");
    Serial.println(hasVolts);
    if(!hasVolts) return;
    volts = forceService.characteristic(VOLT_CHARACTERISTIC_UUID);
    Serial.print("Characteristic value length: ");
    Serial.println(volts.valueLength());
    Serial.print("Characteristic descriptor: ");
    Serial.println(volts.descriptor(0));
    Serial.print("Can read: ");
    Serial.println(volts.canRead());
    Serial.print("Can subscribe: ");
    Serial.println(volts.canSubscribe());
  }
  if(volts.canRead()) {
    int32_t voltage = 0;
    volts.readValue(voltage);
    bool isOpenNow = voltage < 5;
    bool wasOpenBefore = lastReadVoltage < 5;
    if(isOpenNow == wasOpenBefore) return;
    char updateSensorMutation[100 + 4]{};
    sprintf(updateSensorMutation, "{\"query\":\"mutation updateSensor{updateSensor(id: %i, isOpen: %s){ isOpen }}\",\"variables\":{}}\n", autoConnectSensorId, isOpenNow ? "true" : "false");
    DynamicJsonDocument doc = network.SendRequest(updateSensorMutation);
    if(doc["data"] && doc["data"]["updateSensor"]) {
      const boolean isOpen = (const boolean)(doc["data"]["updateSensor"]["isOpen"]);
      Serial.print("isOpen is: ");
      Serial.println(isOpen);
      lastReadVoltage = voltage;
    } else {
      Serial.println("error parsing doc");
    }
  } else {
    Serial.println("Unable to read volts");
  }
}

void loop() {

  CheckInput();

  // Hub has entered pairing mode
  if((!phone && strlen(network.accessToken)) || pairingStartTime > 0) {
    PairToPhone();
  } else if(phone) {
    // Required so that services can be read for some reason
    // FIXME - https://github.com/arduino-libraries/ArduinoBLE/issues/175
    BLE.available();
    ListenForPhoneCommands();
  }

  // Scan for peripheral
  if(!peripheral && (autoConnectSensorId || strcmp(currentCommand.type, COMMAND_START_SENSOR_SEARCH) == 0)) {
    ScanForSensor();
  }
  else if (peripheral && !peripheral.connected() && (autoConnectSensorId || strcmp(currentCommand.type, COMMAND_SENSOR_CONNECT)) == 0) {
    ConnectToFoundSensor();
  }
  else if (peripheral && peripheral.connected()) {
    MonitorSensor();
  }

  // debugging is a bit crazy 
  if(Serial.availableForWrite()) {
    delay(400);
  }
}