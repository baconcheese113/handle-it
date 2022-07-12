#ifndef HUB_UTILITIES_H
#define HUB_UTILITIES_H

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
  void analogWriteRGB(uint8_t r, uint8_t g, uint8_t b);

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

  /**
   * Workaround for LowPower.idle() not working correctly
   */
  void idle(unsigned long delay);

  /**
   * Reads n bytes into buffer (ignoring head) from Serial1
   * Returns true if OK received, false otherwise
  **/
  bool readUntilResp(const char* head, char* buffer);

  /**
   * Prints a char array as bytes up to the termination character
  **/
  void printBytes(char* buffer);
}

#endif