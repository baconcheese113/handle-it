#include <Arduino.h>
#include <ArduinoBLE.h>

struct Command {
    char type[30]{};
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

    /**
     * Returns space between the heap and the stack, ignores deallocated memory
     */
    int freeMemory();

    /**
     * Delays for milliseconds while continuously polling
     */
    void bleDelay(uint16_t milliseconds, BLELocalDevice* BLE);
}