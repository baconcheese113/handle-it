#include <Arduino.h>
#include <ArduinoLowPower.h>
#include <./hub/Utilities.h>

namespace Utilities {
  void setupPins() {
    pinMode(LED_BUILTIN, OUTPUT);

    pinMode(PAIR_PIN, INPUT);
    pinMode(RGB_R, OUTPUT);
    pinMode(RGB_G, OUTPUT);
    pinMode(RGB_B, OUTPUT);
    pinMode(SIM_MOSFET, OUTPUT);

    pinMode(BATT_PIN, INPUT);
  }

  void analogWriteRGB(uint8_t r, uint8_t g, uint8_t b, bool print) {
    if (print) {
      Serial.print("Writing rgb value: ");
      Serial.print(r);
      Serial.print(", ");
      Serial.print(g);
      Serial.print(", ");
      Serial.println(b);
    }
    int divisor = 1;
    analogWrite(RGB_R, r / divisor);
    analogWrite(RGB_G, g / divisor);
    analogWrite(RGB_B, b / divisor);
  }

  Command parseRawCommand(char* rawCmd) {
    Serial.println("in parseRawCmd");
    Command res;
    int8_t valueStartIdx = -1;
    for (uint8_t i = 0; i < strlen(rawCmd); i++) {
      if (rawCmd[i] == ':' && valueStartIdx < 0) { // if delimeter
        valueStartIdx = i + 1;
      } else if (valueStartIdx >= 0) { // if parsing value (after delimeter)
        res.value[i - valueStartIdx] = rawCmd[i];
      } else { // if parsing type (before delimeter)
        res.type[i] = rawCmd[i];
      }
    }
    if (!strlen(res.type)) Serial.println("Error: Couldn't parse type");
    if (!strlen(res.value)) Serial.println("Error: Couldn't parse value");
    return res;
  }

#ifdef __arm__
  // should use uinstd.h to define sbrk but Due causes a conflict
  extern "C" char* sbrk(int incr);
#else  // __ARM__
  extern char* __brkval;
#endif  // __arm__

  int freeMemory() {
    char top;
#ifdef __arm__
    return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
    return &top - __brkval;
#else  // __arm__
    return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
  }

  void bleDelay(uint16_t milliseconds, BLELocalDevice* BLE) {
    unsigned long endTime = millis() + milliseconds;
    while (millis() < endTime) {
      BLE->poll();
      delay(1);
    }
  }

  void idle(unsigned long delay) {
    unsigned long endTime = millis() + delay;
    while (millis() < endTime) {
      LowPower.idle(delay);
    }
  }

  bool readUntilResp(const char* head, char* buffer, BLELocalDevice* BLE, uint16_t timeout) {
    bool didReadHead = strlen(head) == 0;
    uint16_t size = 0;
    uint16_t idx = 0;
    char c;
    unsigned long dropDeadTime = millis() + timeout;
    while (millis() < dropDeadTime)
    {
      while (Serial1.available()) {
        c = Serial1.read();
        if (!didReadHead) {
          if (c != head[idx]) {
            Serial.println("Head doesn't match");
            while (Serial1.available()) Serial1.read();
            return false;
          }
          if (idx == strlen(head) - 1) didReadHead = true;
        } else {
          buffer[size] = c;
          size++;
        }
        idx++;

        if (size >= 6
          && buffer[size - 1] == 10
          && buffer[size - 2] == 13
          && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K'
          ) {
          // OK response
          buffer[size - 8] = '\0';
          return true;
        }
        if (size >= 8
          && buffer[size - 1] == 10
          && buffer[size - 2] == 13
          && buffer[size - 8] == 10 && buffer[size - 7] == 'E' && buffer[size - 6] == 'R' && buffer[size - 5] == 'R' && buffer[size - 4] == 'O' && buffer[size - 3] == 'R'
          ) {
          // ERROR response
          buffer[size - 9] = '\0';
          Serial.println("ERROR received");
          return false;
        }
      }
      if (BLE) Utilities::bleDelay(1, BLE);
    }
    Serial.println(">>READ TIMEOUT<<");
    return false;
  }

  void printBytes(char* buffer) {
    Serial.println("\n===== Printing Bytes =======");
    for (uint16_t idx = 0; idx < strlen(buffer); idx++) {
      if ((uint8_t)buffer[idx] < 100) Serial.print("0");
      Serial.print((uint8_t)buffer[idx]);
      if (buffer[idx] == '\n') Serial.println("");
      else Serial.print(" ");
    }
    Serial.println("\n===== End Bytes =======");
  }
}