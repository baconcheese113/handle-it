#include <FlashStorage.h>
#include <./hub/Utilities.h>
#include <./conf.cpp>
#include <./hub/Network.h>

FlashStorage(flashTokenData, TokenData);

uint8_t AT_HTTPDATA_IDX = 6;
uint8_t AT_HTTPACTION_IDX = 7;
uint8_t AT_HTTPREAD_IDX = 8;

void Network::InitializeAccessToken() {
  tokenData = flashTokenData.read();
  if (tokenData.isValid) {
    Serial.print("Found existing token: ");
    Serial.println(tokenData.accessToken);
    Serial.print("Token length: ");
    Serial.println(strlen(tokenData.accessToken));
  } else {
    Serial.println("No existing token found");
  }
}

void Network::SetAccessToken(const char newAccessToken[100]) {
  strcpy(tokenData.accessToken, newAccessToken);
  tokenData.isValid = true;
  flashTokenData.write(tokenData);
}

DynamicJsonDocument Network::SendRequest(char* query, BLELocalDevice* BLE) {
  Utilities::analogWriteRGB(0, 0, 60);
  Serial.println("Sending request");
  Serial.println(query);

  while (Serial1.available()) Serial.print(Serial1.read());
  char authCommand[55 + strlen(tokenData.accessToken)]{};
  if (tokenData.isValid) {
    sprintf(authCommand, "AT+HTTPPARA=\"USERDATA\",\"Authorization:Bearer %s\"", tokenData.accessToken);
  } else {
    strcpy(authCommand, "AT+HTTPPARA=\"USERDATA\",\"\"");
  }

  char urlCommand[30 + strlen(API_URL)]{};
  sprintf(urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"", API_URL);

  char lenCommand[30]{};
  sprintf(lenCommand, "AT+HTTPDATA=%d,%d", strlen(query), 5000);

  const char* const commands[] = {
    // "AT+SAPBR=3,1,\"APN\",\"hologram\"",
    // "AT+SAPBR=3,1,\"Contype\",\"GPRS\"",
    "AT+SAPBR=1,1",
    "AT+HTTPINIT",
    "AT+HTTPPARA=\"CID\",1",
    authCommand,
    urlCommand,
    "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
    lenCommand,
    "AT+HTTPACTION=1",
    "AT+HTTPREAD",
    "AT+HTTPTERM",
    "AT+SAPBR=0,1",
  };

  char response[RESPONSE_SIZE]{};
  int size;
  unsigned int commandsLen = sizeof commands / sizeof * commands;
  unsigned long timeout;
  Serial.print("Commands to iterate through: ");
  Serial.println(commandsLen);
  for (uint8_t i = 0; i < commandsLen; i++) {
    // Required so that services can be read for some reason
    // FIXME - https://github.com/arduino-libraries/ArduinoBLE/issues/175
    // https://github.com/arduino-libraries/ArduinoBLE/issues/236
    BLE->poll();
    memset(buffer, 0, RESPONSE_SIZE);
    size = 0;

    Serial1.println(commands[i]);
    Serial1.flush();
    timeout = millis() + 1200;
    if (i == AT_HTTPDATA_IDX) { // send query to HTTPDATA command
      char str[40]{};
      uint8_t len = 0;
      while (millis() < timeout) {
        if (Serial1.available()) {
          str[len++] = Serial1.read();
          if (len >= 30) {
            str[len] = '\0';
            if (strcmp(str + (len - 10), "DOWNLOAD\r\n") == 0) break;
          }
        }
      }
      Serial.println(str);
      Utilities::bleDelay(900, BLE); // receive NO CARRIER response without waiting this amount
      Serial1.write(query);
      Serial1.flush();
    }
    timeout = millis() + 5000;
    while (millis() < timeout) {
      if (Serial1.available()) {
        BLE->poll();
        buffer[size] = Serial1.read();
        Serial.write(buffer[size]);
        size++;
        if (size >= 6
          && buffer[size - 1] == 10
          && buffer[size - 2] == 13
          && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K' // OK
          ) {
          if (i == AT_HTTPACTION_IDX) { // special case for AT+HTTPACTION response responding OK before query resolve :/
            while (Serial1.available() < 1 && millis() < timeout) { BLE->poll(); }
            while (Serial1.available() > 0 && millis() < timeout) {
              BLE->poll();
              buffer[size] = Serial1.read();
              Serial.write(buffer[size]);
              size++;
            }
          }
          buffer[size] = '\0';
          if (i == AT_HTTPREAD_IDX) { // special case for AT+HTTPREAD to extract the response
            int16_t responseIdxStart = -1;
            for (int idx = 0; idx < size - 7; idx++) {
              BLE->poll();
              if (responseIdxStart == -1 && buffer[idx] == '{') responseIdxStart = idx;
              if (responseIdxStart > -1) response[idx - responseIdxStart] = buffer[idx];
              if (responseIdxStart > -1 && idx == size - 8) response[idx - responseIdxStart + 1] = '\0';
            }
          }
          break;
        }
      }
    }
    if (millis() >= timeout) {
      Utilities::analogWriteRGB(70, 5, 0);
      Serial.println(">>Network Request Timeout<<");
    }
  }
  Serial.print("Request complete\nResponse is: ");
  Serial.println(response);

  DynamicJsonDocument doc(RESPONSE_SIZE);
  DeserializationError error = deserializeJson(doc, (const char*)response);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
  } else {
    Utilities::analogWriteRGB(0, 25, 0);
    if(doc["errors"] && doc["errors"][0]["extensions"]["code"]) {
      // TODO clear knownSensorAddrs
      if(strcmp(doc["errors"][0]["extensions"]["code"], "UNAUTHENTICATED") == 0) {
        Serial.println("Unauthenticated: Clearing accessToken");
        memset(tokenData.accessToken, 0, 100);
        tokenData.isValid = false;
        flashTokenData.write(tokenData);
        Serial.println("accessToken cleared");
      }
    }
  }
  return doc;
}

void Network::setFunMode(bool fullFunctionality) {
  memset(buffer, 0, RESPONSE_SIZE);
  uint8_t size = 0;
  Serial1.print("AT+CFUN=");
  Serial1.println(fullFunctionality ? "1" : "4");
  Serial1.flush();
  unsigned long timeout = millis() + 2000;
  while (timeout > millis()) {
    if (Serial1.available()) {
      buffer[size] = Serial1.read();
      size++;
    }
    if (fullFunctionality && size > 12
      && buffer[size - 1] == 10
      && buffer[size - 2] == 13
      && buffer[size - 7] == 'R' && buffer[size - 6] == 'e' && buffer[size - 5] == 'a' && buffer[size - 4] == 'd' && buffer[size - 3] == 'y') // SMS Ready
    {
      return;
    } else if (!fullFunctionality && size >= 6
      && buffer[size - 1] == 10
      && buffer[size - 2] == 13
      && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K')
    {
      return;
    }
  }
}

bool Network::GetImei(char* imeiBuffer) {
  memset(buffer, 0, RESPONSE_SIZE);
  uint8_t size = 0;
  char command[] = "AT+GSN\r";
  while (Serial1.available()) Serial1.read();
  Serial1.write(command);
  Serial1.flush();
  unsigned long timeout = millis() + 2000;
  while (timeout > millis())
  {
    if (Serial1.available()) {
      buffer[size] = Serial1.read();
      size++;
    }
    if (size >= 6
      && buffer[size - 1] == 10
      && buffer[size - 2] == 13
      && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K') // OK
    {
      buffer[size] = '\0';
      uint8_t imeiLen = size - strlen(command) - 10;
      strncpy(imeiBuffer, buffer + strlen(command) + 2, imeiLen);
      imeiBuffer[imeiLen] = '\0';
      return true;
    }
  }
  return false;
}

bool Network::waitForPowerOn(BLELocalDevice* BLE) {
  if (isPoweredOn()) {
    Serial.println("Already powered on");
    return true;
  }
  unsigned long startTime = millis();
  char resp[100]{};
  unsigned long timeout = millis() + 10000;
  uint8_t size = 0;
  char msg[] = "SMS Ready\r\n";
  while (millis() < timeout) {
    while (Serial1.available()) {
      resp[size++] = Serial1.read();
      if (size > 12 && resp[size - 1] == '\n') {
        resp[size] = '\0';
        uint8_t msgStartIdx = strlen(resp) - strlen(msg);
        if (strcmp(resp + msgStartIdx, msg) == 0) {
          Serial.print("Powered On! Time(ms): ");
          Serial.println(millis() - startTime);
          return true;
        }
      }
    }
    if (BLE) Utilities::bleDelay(1, BLE);
    else delay(1);
  }
  return false;
}

int8_t Network::getRegStatus(BLELocalDevice* BLE) {
  char resp[10]{};
  while (Serial1.available()) Serial.write(Serial1.read());
  Serial1.println("AT+CREG?");
  Serial1.flush();
  Utilities::readUntilResp("AT+CREG?\r\r\n+CREG: ", resp, BLE);

  int8_t status = resp[2] - '0';
  if (status != lastStatus) {
    // Only print status if it has changed
    Serial.print("Registration Status: ");
    if (status == 0) Serial.println("Not registered, not searching");
    else if (status == 1) Serial.println("Registered, Home network");
    else if (status == 2) Serial.println("Not registered, searching");
    else if (status == 3) Serial.println("Registration denied");
    else if (status == 4) Serial.println("Unknown, possibly out of range");
    else if (status == 5) Serial.println("Registered, roaming");
    else Serial.println("ERROR");
    lastStatus = status;
  }
  return status;
}

int8_t Network::getAccTech(BLELocalDevice* BLE) {
  if (lastStatus != 1 && lastStatus != 5) return -1;
  char resp[30]{};
  while (Serial1.available()) Serial.write(Serial1.read());
  Serial1.println("AT+CREG=2");
  Serial1.flush();
  Utilities::readUntilResp("AT+CREG=2", resp, BLE);
  if (strlen(resp) < 1) return -1;

  Serial1.println("AT+CREG?");
  Serial1.flush();
  Utilities::readUntilResp("AT+CREG?\r\r\n+CREG: ", resp, BLE);

  int8_t status = resp[2] - '0';
  int8_t accTech = -1;

  if (status == 1 || status == 5) {
    // We're registered, print access technology
    accTech = resp[strlen(resp) - 1] - '0';
    Serial.print("Registered on network: ");
    if (accTech == 0) Serial.println("GSM");
    else if (accTech == 2) Serial.println("UTRAN");
    else if (accTech == 3) Serial.println("GSM w/EGPRS");
    else if (accTech == 4) Serial.println("UTRAN w/HSDPA");
    else if (accTech == 5) Serial.println("UTRAN w/HSUPA");
    else if (accTech == 6) Serial.println("UTRAN w/HSDPA and w/HSUPA");
    else if (accTech == 7) Serial.println("E-UTRAN");
    else Serial.println("ERROR");
  }

  Serial1.println("AT+CREG=0");
  Serial1.flush();
  Utilities::readUntilResp("AT+CREG=0", resp, BLE);
  return accTech;
}

void Network::setPower(bool on) {
  digitalWrite(SIM_MOSFET, on ? HIGH : LOW);
  if (on) {
    Serial.println("Powering on SIM module...");
    lastStatus = -1;
  } else {
    Serial.println("Powering off SIM module...");
  }
}

bool Network::isPoweredOn() {
  char resp[10]{};
  while (Serial1.available()) Serial1.read();
  Serial1.println("AT");
  Serial1.flush();
  return Utilities::readUntilResp("", resp, nullptr, 3);
}

bool Network::setPowerOnAndWaitForReg(BLELocalDevice* BLE) {
  unsigned long startTime = millis();
  if (BLE) BLE->poll();
  setPower(true);
  if (!waitForPowerOn(BLE)) {
    setPower(false);
    return false;
  }
  int8_t regStatus = -1;
  while (millis() < startTime + 15000) {
    regStatus = getRegStatus(BLE);
    if (regStatus == 5 || regStatus == 1) break;
    if (BLE) Utilities::bleDelay(50, BLE);
    else delay(50);
  }
  if (regStatus != 5 && regStatus != 1) {
    setPower(false);
    return false;
  }
  if (getAccTech(BLE) == -1) {
    setPower(false);
    return false;
  }
  Serial.print("Registered! Total Boot up time(ms): ");
  Serial.println(millis() - startTime);
  if (BLE) BLE->poll();
  return true;
}
// IMEI example
// 065 084 043 071 083 078 013 013 010 
// 056 054 057 057 058 049 048 053 053 057 057 055 056 057 049 013 010
// 013 010
// 079 075 013 010

// https://robu.in/sim800l-interfacing-with-arduino/
// https://github.com/stephaneAG/SIM800L
// AT+SAPBR=3,1,"Contype","GPRS"
// OK
// AT+SAPBR=1,1
// OK
// AT+HTTPINIT
// OK
// AT+HTTPPARA="CID",1
// OK
// AT+HTTPPARA="URL","http://thisshould.behidden.com"
// OK
// AT+HTTPPARA="CONTENT","application/json"
// OK
// AT+HTTPDATA=120,5000
// DOWNLOAD
// OK
// AT+HTTPACTION=1
// OK
// +HTTPACTION: 1,200,27
// AT+HTTPREAD
// +HTTPREAD: 27
// {"data":{"user":{"id":1}}}
// OK
// AT+HTTPTERM
// OK
// AT+SAPBR=0,1
// OK
