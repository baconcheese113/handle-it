#include <ArduinoBLE.h>
#include <./conf.cpp>
#define RGB_R  9
#define RGB_G  3
#define RGB_B  2
#define D8 8
#define D4  4

// Device name
const char* DEVICE_NAME = "HandleIt Hub";

const char* PERIPHERAL_NAME = "HandleIt Client";
const char* SENSOR_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fb";
const char* VOLT_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fb";

const char* HUB_SERVICE_UUID = "0000181a-0000-1000-8000-00805f9b34fc";
const char* SENSOR_VOLTS_CHARACTERISTIC_UUID = "00002A58-0000-1000-8000-00805f9b34fc";
BLEService hubService = BLEService(HUB_SERVICE_UUID);
BLEIntCharacteristic sensorVolts(SENSOR_VOLTS_CHARACTERISTIC_UUID, BLERead | BLEWrite | BLEWriteWithoutResponse | BLEIndicate | BLEBroadcast);

BLEDevice peripheral;
bool isScanning = false;
bool isAdvertising = false;

BLECharacteristic volts;
bool armed = false;
bool alarmTriggered = false;

unsigned long pairingStartTime = 0;
unsigned long pairButtonHoldStartTime = 0;
BLEDevice* phone;


void analogWriteRGB(u_int8_t r, u_int8_t g, u_int8_t b) {
  Serial.print("Writing rgb value: ");
  Serial.print(r);
  Serial.print(", ");
  Serial.print(g);
  Serial.print(", ");
  Serial.println(b);
  int divisor = 1;
  analogWrite(RGB_R, r / divisor);
  analogWrite(RGB_G, g / divisor);
  analogWrite(RGB_B, b / divisor);
}

void onBLEConnected(BLEDevice d) {
  Serial.println(">>> BLEConnected");
  if(d.deviceName() != PERIPHERAL_NAME) {
    phone = &d;
    pairingStartTime = 0;
    BLE.stopAdvertise();
    isAdvertising = false;
    digitalWrite(LED_BUILTIN, HIGH);
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

  analogWriteRGB(0, 0, 0);
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
  hubService.addCharacteristic(sensorVolts);
  BLE.addService(hubService);
  sensorVolts.writeValue(0);

  // Bluetooth LE connection handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);
  BLE.stopAdvertise();
}

// AT+SAPBR=3,1,"Contype","GPRS"
// OK
// AT+SAPBR=1,1
// OK
// AT+HTTPINIT
// OK
// AT+HTTPPARA="CID",1
// OK
// AT+HTTPPARA="URL","http://thisshould.behidden.com"
// OK
// AT+HTTPPARA="CONTENT","application/json"
// OK
// AT+HTTPDATA=120,5000
// DOWNLOAD

// OK
// AT+HTTPACTION=1
// OK

// +HTTPACTION: 1,200,27
// AT+HTTPREAD
// +HTTPREAD: 27
// {"data":{"user":{"id":1}}}

// OK
// AT+HTTPTERM
// OK
// AT+SAPBR=0,1
// OK

void SendRequest() {
  Serial.write("Sending request\n");

  char urlCommand[sizeof API_URL + 30];
  sprintf(urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"\n", API_URL);

  char query[120] = "{\"query\":\"query GetUsers{user(where:{email:\\\"steve@hotmail.com\\\"}){id}}\",\"variables\":{}}\n";
  size_t queryLen = strlen(query);
  char lenCommand[50];
  sprintf(lenCommand, "AT+HTTPDATA=%d,%d\n", queryLen, 5000);

  char* commands[] = {
    "AT+SAPBR=3,1,\"Contype\",\"GPRS\"\n",
    "AT+SAPBR=1,1\n",
    "AT+HTTPINIT\n",
    "AT+HTTPPARA=\"CID\",1\n",
    urlCommand,
    "AT+HTTPPARA=\"CONTENT\",\"application/json\"\n",
    lenCommand,
    "AT+HTTPACTION=1\n",
    "AT+HTTPREAD\n",
    "AT+HTTPTERM\n",
    "AT+SAPBR=0,1\n",
  };
  char buffer[500];
  char response[200];
  int size = 0;
  char log[300];
  unsigned int commandsLen = sizeof commands / sizeof *commands;
  unsigned long timeout;
  Serial.print("Commands to iterate through: ");
  Serial.println(commandsLen);
  for(uint8_t i = 0; i < commandsLen; i++) {
    Serial1.write(commands[i]);
    Serial1.flush();
    if(i == 6) { // send query to HTTPDATA command
      delay(200);
      Serial1.write(query);
      Serial1.flush();
    }
    timeout = millis() + 10000;
    while(millis() < timeout) {
      if(Serial1.available() < 1) continue;
      buffer[size] = Serial1.read();
      Serial.write(buffer[size]);
      size++;
      if(size >= 6
        && buffer[size - 1] == 10 
        && buffer[size - 2] == 13
        && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K' // OK
      ) {
        if(i == 7) { // special case for AT+HTTPACTION response responding OK before query resolve :/
          while(Serial1.available() < 1 && millis() < timeout);
          while(Serial1.available() > 0 && millis() < timeout) {
            buffer[size] = Serial1.read();
            Serial.write(buffer[size]);
            size++;
          }
        }
        buffer[size] = '\0';
        sprintf(
          log,
          "\nBuffer is \"%s\"\nSize of buffer is %d, last 6 characters are %i, %i, %i, %i, %i, and %i.\n", 
          buffer, 
          strlen(buffer),
          buffer[size - 5],
          buffer[size - 4],
          buffer[size - 3],
          buffer[size - 2],
          buffer[size - 1],
          buffer[size]);
        Serial.write(log);
        if(i == 8) { // extract the response
          int16_t responseIdxStart = -1;
          for(int idx = 0; idx < size - 7; idx++) {
            if(responseIdxStart == -1 && buffer[idx] == '{') responseIdxStart = idx;
            if(responseIdxStart > -1) response[idx - responseIdxStart] = buffer[idx];
            if(idx == size - 8) response[idx - responseIdxStart + 1] = '\0';
          }
          Serial.print("Response is: ");
          Serial.println(response);
        }
        break;
      }
    }
    memset(buffer, 0, 500);
    memset(log, 0, 300);
    size = 0;
  }
  Serial.println("Request complete");
}

void CheckInput() {
  
  // sensorVolts.writeValue(millis() / 1000 % 20);

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
    analogWriteRGB(255, 0, 0);
    return;
  }
  if(millis() / 1000 % 2) analogWriteRGB(75, 0, 130);
  else analogWriteRGB(75, 0, 80);

  if(!isAdvertising) {
    BLE.advertise();
    isAdvertising = true;  
  }
}

void ScanForSensor() {
  if(!isScanning) {
    // this was the first call to start scanning
    BLE.scanForName(PERIPHERAL_NAME);
    isScanning = true;
    analogWriteRGB(255, 0, 0);
    Serial.print("Hub scanning for peripheral...");
  }
  BLEDevice scannedDevice = BLE.available();
  Serial.print(".");
  bool available = scannedDevice.deviceName() == PERIPHERAL_NAME || scannedDevice.localName() == PERIPHERAL_NAME;
  if (!available) return;

  // We found a Sensor!
  peripheral = scannedDevice;
  analogWriteRGB(255, 30, 0);
  Serial.print("\nDEVICE FOUND");
  Serial.println(available);
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
    analogWriteRGB(255, 0, 0);
    Serial.println("Failed to connect retrying....");
    delay(1000);
    return;
  }

  // We're connected to sensor!
  analogWriteRGB(255, 100, 200);
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
    // if(sensorVolts.canWrite()) {
      sensorVolts.writeValue(voltage);
    // } else {
    //   Serial.println("Missing permissions to write");
    // }
    if(alarmTriggered) {
      if(millis() / 1000 % 2) digitalWrite(D4, HIGH);
      else digitalWrite(D4, LOW);
    }
    else if(!armed && voltage > 28) {
      analogWriteRGB(10, 100, 0);
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

  CheckInput();

  // Hub has entered pairing mode
  if(pairingStartTime > 0) {
    PairToPhone();
  }

  // Scan for peripheral
  if(!peripheral) {
    ScanForSensor();
  } else if (!peripheral.connected()) {
    ConnectToFoundSensor();
  } else {
    MonitorSensor();
  }

  // debugging is a bit crazy 
  if(Serial.availableForWrite()) {
    delay(400);
  }
}