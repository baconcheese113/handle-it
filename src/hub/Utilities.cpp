#include<Arduino.h>
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
}