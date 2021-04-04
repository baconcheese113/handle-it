#include <ArduinoBLE.h>
// Device name
const char* nameOfPeripheral = "HandleIt Client";
const char* uuidOfService = "0000181a-0000-1000-8000-00805f9b34fb";
// const char* uuidOfRxChar = "00002A3D-0000-1000-8000-00805f9b34fb";
const char* uuidOfVolts = "00002A58-0000-1000-8000-00805f9b34fb";
BLEService forceService(uuidOfService);
// Rx/Tx Characteristics
// BLECharacteristic rxChar(uuidOfRxChar, BLEWriteWithoutResponse | BLEWrite, 256, false);
BLEIntCharacteristic volts(uuidOfVolts, BLERead | BLEWrite | BLEWriteWithoutResponse | BLEIndicate | BLEBroadcast);

unsigned long lastReset = 0;

void onBLEConnected(BLEDevice d) {
  Serial.println(">>> BLEConnected");
  digitalWrite(LED_BUILTIN, HIGH);
}
void onBLEDisconnected(BLEDevice d) {
  Serial.println(">>> BLEDisconnected");
  digitalWrite(LED_BUILTIN, LOW);
}
void onRxCharValueUpdate(BLEDevice d, BLECharacteristic c) {
  Serial.println(">>> RxCharValueUpdate");
}

void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  // while(!Serial);
  pinMode(A1, INPUT_PULLUP);
  pinMode(D8, OUTPUT);
  pinMode(D7, OUTPUT);
  pinMode(D6, OUTPUT);
  pinMode(D5, OUTPUT);
  pinMode(D4, OUTPUT);

  // begin BLE initialization
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");

    while (1);
  }
  Serial.println("Bluetooth setup beginning");
  BLE.setLocalName(nameOfPeripheral);
  BLE.setAdvertisedService(forceService);
  // forceService.addCharacteristic(rxChar);
  forceService.addCharacteristic(volts);
  BLE.addService(forceService);
  volts.writeValue(30);

  // Bluetooth LE connection handlers
  BLE.setEventHandler(BLEConnected, onBLEConnected);
  BLE.setEventHandler(BLEDisconnected, onBLEDisconnected);

  // rxChar.setEventHandler(BLEWritten, onRxCharValueUpdate);
  BLE.advertise();
  lastReset = millis();

  //  // Print out full UUID and MAC address.
  Serial.println("Peripheral advertising info: ");
  Serial.print("Name: ");
  Serial.println(nameOfPeripheral);
  Serial.print("MAC: ");
  Serial.println(BLE.address());
  Serial.print("Service UUID: ");
  Serial.println(forceService.uuid());
  Serial.print("txCharacteristics UUID: ");
  Serial.println(uuidOfVolts);
  

  Serial.println("Bluetooth device active, waiting for connections...");
}

// the loop routine runs over and over again forever:
void loop() {
  // ******* BLE ********
  // if(millis() > lastReset + 60000 && !BLE.connected()) {
  //   BLE.stopAdvertise();
  //   BLE.advertise();
  //   Serial.println("Advertise reset");
  //   lastReset = millis();
  // }


  // ***** force algorithm******
  digitalWrite(D6, LOW);
  digitalWrite(D5, LOW);
  digitalWrite(D4, LOW);
  // read the input on analog pin 0:
  int sensorValue = analogRead(A0);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 5V):
  float voltage = sensorValue * (3.3 / 1023.0);
  // print out the value you read:
  // Serial.println(voltage);
  if(voltage > 1.0) {
    digitalWrite(D6, HIGH);
  }
  if(voltage > 2.0) {
    digitalWrite(D5, HIGH);
  }
  if(voltage >= 2.5) {
    digitalWrite(D4, HIGH);
  }


  BLEDevice central = BLE.central();
  if(central) {
    int32_t intVoltage = static_cast<int32_t>(voltage * 10);
    Serial.print("Writing voltage of ");
    Serial.println(intVoltage);
    volts.writeValue(intVoltage);
    // volts.setValue(intVoltage);
  }

}
