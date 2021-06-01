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
const char* SENSOR_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fb";
const char* VOLT_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fb";

const char* HUB_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fc";
const char* SENSOR_VOLTS_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fc";
const char* COMMAND_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fd";

BLEService hubService = BLEService(HUB_SERVICE_UUID);
BLEIntCharacteristic sensorVoltsChar(SENSOR_VOLTS_CHARACTERISTIC_UUID, BLERead | BLEWrite | BLEWriteWithoutResponse | BLEIndicate | BLEBroadcast);
BLEStringCharacteristic commandChar(COMMAND_CHARACTERISTIC_UUID, BLERead | BLEWrite, 20);

BLEDevice peripheral;
bool isScanning = false;
bool isAdvertising = false;

BLECharacteristic volts;
bool armed = false;
bool alarmTriggered = false;

unsigned long pairingStartTime = 0;
unsigned long pairButtonHoldStartTime = 0;
BLEDevice* phone;

Network network;

void onBLEConnected(BLEDevice d) {
  Serial.println(">>> BLEConnected");
  if(d.deviceName() != PERIPHERAL_NAME) {
    phone = &d;
    pairingStartTime = 0;
    BLE.stopAdvertise();
    isAdvertising = false;
    digitalWrite(LED_BUILTIN, HIGH);
    // Grab access_token from userId of connected phone
    char rawCommand[20]{};
    Serial.println("Trying to read userid...");
    while(strlen(rawCommand) < 1) {
      // Required to allow the phone to finish connecting properly
      BLE.available();
      if(commandChar.written()) {
        String writtenVal = commandChar.value();
        writtenVal.toCharArray(rawCommand, 20);
      }
      Serial.print(".");
      delay(100);
    }
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
    StaticJsonDocument<400> loginDoc = network.SendRequest(loginMutationStr);

    if(loginDoc["data"] && loginDoc["data"]["loginAsHub"]) {
      const char* token = (const char *)(loginDoc["data"]["loginAsHub"]);
      strcpy(network.accessToken, token);
      Serial.print("token is: ");
      Serial.println(network.accessToken);
      // TODO check if token is different from existing token in flash storage, if so replace it
      // cmaglie/FlashStorage
    } else {
      Serial.println("Error reading token");
      return;
    }

    char getHubQueryStr[70] = "{\"query\":\"query getHubViewer{hubViewer{id}}\",\"variables\":{}}";
    StaticJsonDocument<100> hubViewerDoc = network.SendRequest(getHubQueryStr);
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
  if(d.deviceName() != PERIPHERAL_NAME) {
    phone = NULL;
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D8, INPUT);

  Utilities::analogWriteRGB(0, 0, 0);
  Serial.begin(9600);
  while(!Serial);
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
  hubService.addCharacteristic(sensorVoltsChar);
  hubService.addCharacteristic(commandChar);
  BLE.addService(hubService);
  sensorVoltsChar.writeValue(0);

  // Bluetooth LE connection handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);
  BLE.stopAdvertise();
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

void ScanForSensor() {
  if(!isScanning) {
    // this was the first call to start scanning
    BLE.scanForName(PERIPHERAL_NAME);
    isScanning = true;
    Utilities::analogWriteRGB(255, 0, 0);
    Serial.print("Hub scanning for peripheral...");
  }
  BLEDevice scannedDevice = BLE.available();
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
  Serial.println("Connecting ...");
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
    // if(sensorVoltsChar.canWrite()) {
      sensorVoltsChar.writeValue(voltage);
    // } else {
    //   Serial.println("Missing permissions to write");
    // }
    // TODO rework alarm triggering and add confirmation to sensor read
    if(alarmTriggered) {
      if(millis() / 1000 % 2) digitalWrite(D4, HIGH);
      else digitalWrite(D4, LOW);
    }
    else if(!armed && voltage > 28) {
      Utilities::analogWriteRGB(10, 100, 0);
      armed = true;
    }
    else if(armed && voltage < 25) {
      alarmTriggered = true;
    }
  } else {
    Serial.println("Unable to read volts");
  }
}

void loop() {

  if(Serial1.available() > 0) {
    Serial.write(Serial1.read());
  }
  if(Serial.available() > 0) {
    int in = Serial.read();
    Serial1.write(in);
    if(in == 'r') {
      int32_t userIdValue = 1;
      char mutationStr[100 + sizeof userIdValue + strlen(DEVICE_SERIAL)]{};
      sprintf(mutationStr, "{\"query\":\"mutation loginAsHub{loginAsHub(userId:%ld, serial:\\\"%s\\\")}\",\"variables\":{}}\n", userIdValue, DEVICE_SERIAL);
      StaticJsonDocument<400> doc = network.SendRequest(mutationStr);
      if(doc["data"] && doc["data"]["loginAsHub"]) {
        const char* token = (const char*)(doc["data"]["loginAsHub"]);
        Serial.print("token is: ");
        Serial.println(token);
      }
    }
  }

  CheckInput();

  // Hub has entered pairing mode
  if(pairingStartTime > 0) {
    PairToPhone();
  } else if(phone) {
    // Required so that services can be read for some reason
    // FIXME - https://github.com/arduino-libraries/ArduinoBLE/issues/175
    BLE.available();
  }

  // Scan for peripheral
  // if(!peripheral) {
  //   ScanForSensor();
  // } 
  // else if (!peripheral.connected()) {
  //   ConnectToFoundSensor();
  // } else {
  //   MonitorSensor();
  // }

  // debugging is a bit crazy 
  if(Serial.availableForWrite()) {
    delay(400);
  }
}