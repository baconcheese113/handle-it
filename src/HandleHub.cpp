#include <ArduinoBLE.h>
#define RGB_R  9
#define RGB_G  3
#define RGB_B  2
#define D8 8
#define D4  4


// Device name
const char* DEVICE_NAME = "HandleIt Hub";

const char* PERIPHERAL_NAME = "HandleIt Client";
const char* uuidOfService = "0000181a-0000-1000-8000-00805f9b34fb";
const char* uuidOfVolts = "00002A58-0000-1000-8000-00805f9b34fb";

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
  digitalWrite(LED_BUILTIN, HIGH);
  if(d.deviceName() != PERIPHERAL_NAME) {
    phone = &d;
    pairingStartTime = 0;
    BLE.stopAdvertise();
    isAdvertising = false;
    analogWriteRGB(10, 100, 0);
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
  // while(!Serial);
   // begin BLE initialization
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");

    while (1);
  }
  Serial.println("Booting...");
  BLE.setLocalName(DEVICE_NAME);
  // TODO add a service to advertise sensor data to phone
  // BLE.setAdvertisedService(forceService);
  // forceService.addCharacteristic(rxChar);
  // forceService.addCharacteristic(volts);
  // BLE.addService(forceService);
  // volts.writeValue(30);

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
  if(millis() > pairingStartTime + 30000) {
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
  Serial.println(peripheral.hasService(uuidOfService));
  Serial.print("Discover the force: ");
  Serial.println(peripheral.discoverService(uuidOfService));
}

void MonitorSensor() {
  if(!volts) {
    // this was the first call to start monitoring
    BLEService forceService = peripheral.service(uuidOfService);
    bool hasVolts = forceService.hasCharacteristic(uuidOfVolts);
    Serial.print("Has volts: ");
    Serial.println(hasVolts);
    if(!hasVolts) return;
    volts = forceService.characteristic(uuidOfVolts);
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