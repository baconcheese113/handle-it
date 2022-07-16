#include <Arduino.h>
#include <ArduinoLowPower.h>
#include <utility/HCI.h>
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
    int divisor = 5;
    analogWrite(RGB_R, r / divisor);
    analogWrite(RGB_G, g / divisor);
    analogWrite(RGB_B, b / divisor);
  }

  void happyDance() {
    uint8_t red, green, blue;
    for (uint16_t angle = 0; angle < 360; angle++) {
      if (angle < 60) {
        red = 255; green = round(angle * 4.25 - 0.01); blue = 0;
      } else if (angle < 120) {
        red = round((120 - angle) * 4.25 - 0.01); green = 255; blue = 0;
      } else if (angle < 180) {
        red = 0, green = 255; blue = round((angle - 120) * 4.25 - 0.01);
      } else if (angle < 240) {
        red = 0, green = round((240 - angle) * 4.25 - 0.01); blue = 255;
      } else if (angle < 300) {
        red = round((angle - 240) * 4.25 - 0.01), green = 0; blue = 255;
      } else {
        red = 255, green = 0; blue = round((360 - angle) * 4.25 - 0.01);
      }
      analogWriteRGB(red, green, blue, false);
      delay(2);
    }
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

  bool setBlePower(bool on) {
    if (on) {
      Serial.print("BLE trying to power up...");
      digitalWrite(NINA_RESETN, HIGH);
      delay(750);
      if (!HCI.begin()) return false;
      if (HCI.reset() != 0) return false;
      if (HCI.setEventMask(0x3FFFFFFFFFFFFFFF) != 0) return false;
      if (HCI.setLeEventMask(0x00000000000003FF) != 0) return false;
      Serial.println("BLE On!");
    } else {
      digitalWrite(NINA_RESETN, LOW);
      Serial.println("BLE powered down");
    }
    return true;
  }
}