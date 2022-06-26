#include<Arduino.h>
#include <./hub/Utilities.h>
#define RGB_R  9
#define RGB_G  6
#define RGB_B  5

namespace Utilities {
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

    Command parseRawCommand(char* rawCmd) {
        Serial.println("in parseRawCmd");
        Command res;
        int8_t valueStartIdx = -1;
        for(u_int8_t i = 0; i < strlen(rawCmd); i++) {
            if(rawCmd[i] == ':' && valueStartIdx < 0) { // if delimeter
                valueStartIdx = i + 1;
            } else if(valueStartIdx >= 0) { // if parsing value (after delimeter)
                res.value[i - valueStartIdx] = rawCmd[i];
            } else { // if parsing type (before delimeter)
                res.type[i] = rawCmd[i];
            }
        }
        if(!strlen(res.type)) Serial.println("Error: Couldn't parse type");
        if(!strlen(res.value)) Serial.println("Error: Couldn't parse value");
        return res;
    }

    #ifdef __arm__
    // should use uinstd.h to define sbrk but Due causes a conflict
    extern "C" char* sbrk(int incr);
    #else  // __ARM__
    extern char *__brkval;
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
        while(millis() < endTime) {
            BLE->poll();
            delay(1);
        }
    }

    bool readUntilResp(const char* head, char* buffer) {
        bool didReadHead = strlen(head) == 0;
        uint16_t size = 0;
        uint16_t idx = 0;
        char c;
        unsigned long timeout = millis() + 2000;
        while (millis() < timeout)
        {
            while(Serial1.available()) {
                c = Serial1.read();
                if(!didReadHead) {
                    if(c != head[idx]) {
                        Serial.println("Head doesn't match");
                        return false;
                    }
                    if(idx == strlen(head) - 1) didReadHead = true;
                } else {
                    buffer[size] = c;
                    size++;
                }
                idx++;
            }
            if(size >= 6
                && buffer[size - 1] == 10 
                && buffer[size - 2] == 13
                && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K'
            ) {
                // OK response
                buffer[size - 8] = '\0';
                return true;
            }
            if(size >= 8
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
        Serial.println(">>TIMEOUT<<");
        return false;
    }
    
    void printBytes(char* buffer) {
        Serial.println("\n===== Printing Bytes =======");
        for(uint16_t idx = 0; idx < strlen(buffer); idx++) {
            Serial.print("0");
            Serial.print((uint8_t)buffer[idx]);
            if(buffer[idx] == '\n') Serial.println(""); 
            else Serial.print(" ");
        }
        Serial.println("\n===== End Bytes =======");
    }
}