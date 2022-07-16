#ifndef HUB_UTILITIES_H
#define HUB_UTILITIES_H

#include <Arduino.h>
#include <ArduinoBLE.h>

// All pins for the project should be declared here and set in setupPins

// Digital pins
#define PAIR_PIN  10
#define RGB_R  9
#define RGB_G  6
#define RGB_B  5
#define SIM_MOSFET 4

// Analog pins
#define BATT_PIN A0

struct Command {
  char type[30]{};
  char value[50]{};
};

namespace Utilities {
  /**
   * Single place to register all used I/O pins
   */
  void setupPins();

  /**
   * Writes to pins 9, 3, and 2 RGB values from 0 - 255
  **/
  void analogWriteRGB(uint8_t r, uint8_t g, uint8_t b, bool print = true);

  void happyDance();
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
  bool readUntilResp(const char* head, char* buffer, BLELocalDevice* BLE = nullptr, uint16_t timeout = 1000);

  /**
   * Prints a char array as bytes up to the termination character
  **/
  void printBytes(char* buffer);

  /**
   * Power on or off the NINA module in the Arduino
   * Returs false if there was an error
   */
  bool setBlePower(bool on);
}

#endif