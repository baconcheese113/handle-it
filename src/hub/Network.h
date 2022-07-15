#ifndef HUB_NETWORK_H
#define HUB_NETWORK_H

#include <ArduinoBLE.h>
#include <ArduinoJson.h>

// Needs to be large enough for error messages
const uint16_t RESPONSE_SIZE = 2000;

typedef struct {
  char accessToken[100]{};
  boolean isValid = false;
} TokenData;

class Network {
private:
  /**
   * Static memory used for reading network response, might need to increase size if expecting larger responses
  **/
  char buffer[RESPONSE_SIZE]{};

  /**
   * Network registration status
   */
  int8_t lastStatus = -1;

public:
  /**
   * Struct with mutatable token to access API_URL as Hub, set once registration is successful
  **/
  TokenData tokenData = {};

  void InitializeAccessToken();

  void SetAccessToken(const char newAccessToken[100]);

  /**
   * Sends a request containing query to API_URL, returns a json document with
   * response in the "data" field if no errors, otherwise errors will be in "errors"
  **/
  DynamicJsonDocument SendRequest(char* query, BLELocalDevice* BLE);

  /**
   * Utility function to set AT+CFUN=1 or 4 (1 = full, 4 = airplane mode)
   */
  void setFunMode(bool fullFunctionality);

  /**
   * Gets IMEI string from the SIM module and stores it into provided buffer
   * returns false if timed out
  **/
  bool GetImei(char* imeiBuffer);

  /**
   * Get the AT+CREG network status
   * 0 - Not registered, the device is currently not searching for new operator.
   * 1 - Registered to home network.
   * 2 - Not registered, but the device is currently searching for a new operator.
   * 3 - Registration denied.
   * 4 - Unknown. For example, out of range.
   * 5 - Registered, roaming. The device is registered on a foreign (national or international) network.
   * 
   * -1 - Failed to retrieve status
   * 
   * If BLE is provided, it will poll as it waits
   * 
   * https://docs.eseye.com/Content/ELS61/ATCommands/ELS61CREG.htm
   */
  int8_t getRegStatus(BLELocalDevice* BLE = nullptr);

  /**
   * Can be called after getRegStatus returns 1 or 5 to read the access tech
   * 0 - GSM
   * 2 - UTRAN
   * 3 - GSM w/EGPRS
   * 4 - UTRAN w/HSDPA
   * 5 - UTRAN w/HSUPA
   * 6 - UTRAN w/HSDPA and w/HSUPA
   * 7 - E-UTRAN
   * 
   * -1 - Failed to retrieve status or not registered
   * 
   * If BLE is provided, it will poll as it waits
   */
  int8_t getAccTech(BLELocalDevice* BLE = nullptr);

  /**
   * Wait until receiving the power on messages from the sim module
   * Returns true if powered on, false if timed out
   * If BLE is provided, it will poll as it waits
   */
  bool waitForPowerOn(BLELocalDevice* BLE = nullptr);

  /**
   * Set the power on or off for the SIMCOM module
   */
  void setPower(bool on);

  /**
   * We can't cache the power state of the module since it can change state separately
   * So this does a quick <100ms query to see if the module is attached and powered
   */
  bool isPoweredOn();

  /**
   * Shorthand for calling setPower(true), waitForPowerOn, and getRegStatus until registered
   * If BLE is provided, it will poll as it waits
   */
  bool setPowerOnAndWaitForReg(BLELocalDevice* BLE = nullptr);
};

#endif