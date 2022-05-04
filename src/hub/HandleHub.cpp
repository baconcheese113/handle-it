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

BLEDevice* peripheral;
bool isScanning = false;
bool isAdvertising = false;
bool isAddingNewSensor = false;

unsigned long pairingStartTime = 0;
unsigned long pairButtonHoldStartTime = 0;
BLEDevice* phone;

Network network;

Command currentCommand;
String lastReadCommand = "";

String knownSensorAddrs[10];
uint8_t knownSensorAddrsLen = 0;
int32_t lastReadVoltage = 0;

void onBLEConnected(BLEDevice d) {
  Serial.print(">>> BLEConnected to: ");
  Serial.println(d.address());
  
  bool dNameMatch = d.deviceName().compareTo(PERIPHERAL_NAME) == 0 || d.localName().compareTo(PERIPHERAL_NAME) == 0;
  bool peripheralNameMatch = peripheral->deviceName().compareTo(PERIPHERAL_NAME) == 0 || peripheral->localName().compareTo(PERIPHERAL_NAME) == 0;
  if(!phone && !dNameMatch && !peripheralNameMatch) {
    phone = new BLEDevice();
    *phone = d;
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
    DynamicJsonDocument loginDoc = network.SendRequest(loginMutationStr, &BLE);
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
    DynamicJsonDocument hubViewerDoc = network.SendRequest(getHubQueryStr, &BLE);
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
  Serial.print("\n>>> BLEDisconnecting from: ");
  Serial.println(d.address());
  digitalWrite(LED_BUILTIN, LOW);
  if(peripheral && peripheral->address() == d.address()) {
    Serial.println("Peripheral disconnected");
    Utilities::analogWriteRGB(0, 0, 0);
  } else if(phone && phone->address() == d.address()) {
    Serial.println("Phone disconnected");
    delete phone;
    phone = nullptr;
    isAddingNewSensor = false;
    memset(currentCommand.type, 0, sizeof currentCommand.type);
    memset(currentCommand.value, 0, sizeof currentCommand.value);
  }
  delete peripheral;
  peripheral = nullptr;
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

  if(strlen(network.accessToken)) {
    char sensorQuery[] = "{\"query\":\"query getMySensors{hubViewer{sensors{serial}}}\",\"variables\":{}}";
    DynamicJsonDocument doc = network.SendRequest(sensorQuery, &BLE);
    if(doc["data"] && doc["data"]["hubViewer"] && doc["data"]["hubViewer"]["sensors"]) {
      const JsonArrayConst sensors = doc["data"]["hubViewer"]["sensors"];
      if(sensors.size()) {
        for(uint8_t i = 0; i < sensors.size(); i++) {
          knownSensorAddrs[i] = String((const char*)sensors[i]["serial"]);
          knownSensorAddrsLen++;
          Serial.print(knownSensorAddrs[i]);
          Serial.print(" is knownSensorAddrs at idx: ");
          Serial.println(i);
        }
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
  if(millis() > pairingStartTime + 60000) {
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
  
  // happens when clearing char and cancelling form
  if(writtenVal.length() <= 1) {
    isAddingNewSensor = false;
    return;
  }
  writtenVal.toCharArray(rawCommand, 30);

  Serial.print("\nCommand value: ");
  Serial.println(rawCommand);

  currentCommand = Utilities::parseRawCommand(rawCommand);

  if(strcmp(currentCommand.type, COMMAND_START_SENSOR_SEARCH) == 0) {
    Serial.println("Now adding new sensor");
    isAddingNewSensor = true;
  }

  Serial.print("Parsed command type: ");
  Serial.println(currentCommand.type);
  Serial.print("Parsed command value: ");
  Serial.println(currentCommand.value);
}

void ScanForSensor() {
  if(pairingStartTime > 0) return;
  if(!isScanning) {
    // this was the first call to start scanning
    BLE.scanForName(PERIPHERAL_NAME, true);
    isScanning = true;
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.print("Hub scanning for peripheral...");
  }
  BLEDevice scannedDevice = BLE.available();
  if(scannedDevice.localName().length() > 0) {
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

  if(!isAddingNewSensor) {
    bool isKnownSensor = false;
    for (uint8_t i = 0; i < knownSensorAddrsLen; i++) {
      Serial.print("Checking for a match with: ");
      Serial.println(knownSensorAddrs[i]);
      if(scannedDevice.address() == knownSensorAddrs[i]) {
        isKnownSensor = true;
        break;
      }
    }
    if(!isKnownSensor) return;
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
  
  if(isAddingNewSensor) {
    Serial.println("Waiting for command to connect~~~");
    String sensorFound = "SensorFound:";
    sensorFound.concat(peripheral->address());
    commandChar.writeValue(sensorFound);
    memset(currentCommand.type, 0, sizeof currentCommand.type);
    memset(currentCommand.value, 0, sizeof currentCommand.value);
  }
}

void ConnectToFoundSensor() {
  if(isAddingNewSensor && strcmp(currentCommand.type, COMMAND_SENSOR_CONNECT) != 0) {
    // TODO handle the form timing out at this location better
    Serial.print("~");
    return;
  }
  if(!peripheral->connect()) {
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.println("\nFailed to connect retrying....");
    delay(1000);
    return;
  }

  // We're connected to sensor!
  Utilities::analogWriteRGB(255, 100, 200);
  Serial.println("\nPeripheral connected!");
  Serial.println(peripheral->discoverService(SENSOR_SERVICE_UUID));
  Serial.println(peripheral->discoverAttributes());
  Serial.print("Service count: ");
  Serial.println(peripheral->serviceCount());
  Serial.print("Appearance: ");
  Serial.println(peripheral->appearance());
  Serial.print("Has force service: ");
  Serial.println(peripheral->hasService(SENSOR_SERVICE_UUID));
  Serial.print("Has volts: ");
  Serial.println(peripheral->hasCharacteristic(VOLT_CHARACTERISTIC_UUID));

  if(!isAddingNewSensor) return;

  const char* sensorSerial = peripheral->address().c_str();
  char mutationStr[155 + strlen(sensorSerial)]{};
  sprintf(mutationStr, "{\"query\":\"mutation createSensor{createSensor(doorColumn: 0, doorRow: 0, isOpen: true, isConnected: true, serial:\\\"%s\\\"){id}}\",\"variables\":{}}\n", sensorSerial);
  DynamicJsonDocument doc = network.SendRequest(mutationStr, &BLE);
  if(doc["data"] && doc["data"]["createSensor"]) {
    const uint8_t id = (const uint8_t)(doc["data"]["createSensor"]["id"]);
    Serial.print("createSensor id: ");
    Serial.println(id);
    Serial.print("Adding to knownSensorAddrs: ");
    Serial.println(peripheral->address());
    knownSensorAddrs[knownSensorAddrsLen] = peripheral->address();
    knownSensorAddrsLen++;
    if(phone) {
      commandChar.writeValue("SensorAdded:1");
    }
    peripheral->disconnect();
    // TODO implement a better way to prevent reconnecting
    delay(20000);
  } else {
    Serial.println("doc not valid");
  }
  
  memset(currentCommand.type, 0, sizeof currentCommand.type);
  memset(currentCommand.value, 0, sizeof currentCommand.value);
}

void MonitorSensor() {
  BLEService forceService = peripheral->service(SENSOR_SERVICE_UUID);
  bool hasVolts = forceService.hasCharacteristic(VOLT_CHARACTERISTIC_UUID);
  Serial.print("Has volts: ");
  Serial.println(hasVolts);
  if(!hasVolts) return;
  BLECharacteristic volts = forceService.characteristic(VOLT_CHARACTERISTIC_UUID);
  Serial.print("Characteristic value length: ");
  Serial.println(volts.valueLength());
  Serial.print("Characteristic descriptor: ");
  Serial.println(volts.descriptor(0));
  Serial.print("Can read: ");
  Serial.println(volts.canRead());
  Serial.print("Can subscribe: ");
  Serial.println(volts.canSubscribe());

  if(volts.canRead()) {
    int32_t voltage = 0;
    volts.readValue(voltage);
    Serial.print("Volts value: ");
    Serial.println(voltage);
    const char* address = peripheral->address().c_str();
    char createEvent[100 + strlen(address)]{};
    sprintf(createEvent, "{\"query\":\"mutation CreateEvent{createEvent(serial:\\\"%s\\\"){ id }}\",\"variables\":{}}\n", address);
    DynamicJsonDocument doc = network.SendRequest(createEvent, &BLE);
    if(doc["data"] && doc["data"]["createEvent"]) {
      const uint8_t id = (const uint8_t)(doc["data"]["createEvent"]["id"]);
      Serial.print("created event id is: ");
      Serial.println(id);
      lastReadVoltage = voltage;
    } else {
      Serial.println("error parsing doc");
    }
    peripheral->disconnect();
    // TODO implement a better way to prevent reconnecting
    delay(20000);
  } else {
    Serial.println("Unable to read volts");
  }
}

void loop() {

  CheckInput();

  // Hub has entered pairing mode
  if(pairingStartTime > 0) {
    PairToPhone();
  }
  else if(phone) {
    ListenForPhoneCommands();
  }

  // Scan for peripheral
  if(!peripheral) {
    ScanForSensor();
  }
  else if (!peripheral->connected()) {
    ConnectToFoundSensor();
  }
  else {
    MonitorSensor();
  }

  // debugging is a bit crazy 
  if(Serial.availableForWrite()) {
    delay(400);
  }
}