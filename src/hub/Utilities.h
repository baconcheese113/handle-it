#include <Arduino.h>

struct Command {
    char type[10]{};
    char value[50]{};
};

namespace Utilities {
    /**
     * Writes to pins 9, 3, and 2 RGB values from 0 - 255
    **/
    void analogWriteRGB(u_int8_t r, u_int8_t g, u_int8_t b);

    /**
     * Parses BLE char arrays separated by a colon ( : ) delimeter into a Command struct
     * Prints an error message if unable to parse
    **/
    Command parseRawCommand(char* rawCmd);
}