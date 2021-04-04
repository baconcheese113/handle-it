#include <ArduinoBLE.h>
#define RGB_R  9
#define RGB_G  3
#define RGB_B  2
#define D4  4
const char* DEFAULT_NAME = "HandleIt Client";
const char* uuidOfService = "0000181a-0000-1000-8000-00805f9b34fb";
const char* uuidOfVolts = "00002A58-0000-1000-8000-00805f9b34fb";

BLEDevice peripheral;

void analogWriteRGB(int r, int g, int b) {
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

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(D4, OUTPUT);

  analogWriteRGB(0, 0, 0);
  Serial.begin(9600);
  // while(!Serial);
   // begin BLE initialization
  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");

    while (1);
  }
  Serial.println("Booting...");
  analogWriteRGB(255, 0, 0);
  delay(3000);
  bool available = false;
  BLE.scanForName(DEFAULT_NAME);
  Serial.print("Hub scanning for peripheral...");
  while(!available) {
    peripheral = BLE.available();
    available = peripheral.deviceName() == DEFAULT_NAME || peripheral.localName() == DEFAULT_NAME;
    Serial.print(".");
    delay(100);
  }
  analogWriteRGB(255, 30, 0);
  Serial.print("\nAvailable: ");
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
  delay(2000);
}

void loop() {
  if (!peripheral.connected()) {
    while(!peripheral.connect()) {
      analogWriteRGB(255, 0, 0);
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("Failed to connect retrying....");
      delay(1000);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(2000);
    }
    analogWriteRGB(255, 100, 200);
    Serial.println("Peripheral connected!");
    delay(1000);
  }
  Serial.print("Service count: ");
  Serial.println(peripheral.serviceCount());
  Serial.print("Appearance: ");
  Serial.println(peripheral.appearance());
  Serial.print("Has force service: ");
  Serial.println(peripheral.hasService(uuidOfService));
  Serial.print("Discover the force: ");
  Serial.println(peripheral.discoverService(uuidOfService));
  BLEService forceService = peripheral.service(uuidOfService);
  bool hasVolts = forceService.hasCharacteristic(uuidOfVolts);
  Serial.print("Has volts: ");
  Serial.println(hasVolts);
  BLECharacteristic volts = forceService.characteristic(uuidOfVolts);
  Serial.print("Characteristic value length: ");
  Serial.println(volts.valueLength());
  Serial.print("Characteristic descriptor: ");
  Serial.println(volts.descriptor(0));
  Serial.print("Can read: ");
  Serial.println(volts.canRead());
  Serial.print("Can subscribe: ");
  Serial.println(volts.canSubscribe());
  bool armed = false;
  bool alarmTriggered = false;
  while(peripheral.connected()) {
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
}


