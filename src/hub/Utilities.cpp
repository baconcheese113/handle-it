#include<Arduino.h>
#include <./hub/Utilities.h>
#define RGB_R  9
#define RGB_G  3
#define RGB_B  2

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
}