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
    if(tokenData.isValid) {
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
    setFunMode(true);
    Utilities::analogWriteRGB(0, 0, 60);
    Utilities::bleDelay(2000, BLE);
    Serial.println("Sending request");
    Serial.println(query);

    char authCommand[55 + strlen(tokenData.accessToken)]{};
    if(tokenData.isValid) {
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
    unsigned int commandsLen = sizeof commands / sizeof *commands;
    unsigned long timeout;
    Serial.print("Commands to iterate through: ");
    Serial.println(commandsLen);
    for(uint8_t i = 0; i < commandsLen; i++) {
        // Required so that services can be read for some reason
        // FIXME - https://github.com/arduino-libraries/ArduinoBLE/issues/175
        // https://github.com/arduino-libraries/ArduinoBLE/issues/236
        BLE->poll();
        memset(buffer, 0, RESPONSE_SIZE);
        size = 0;

        Serial1.println(commands[i]);
        Serial1.flush();
        if(i == AT_HTTPDATA_IDX) { // send query to HTTPDATA command
            delay(900); // receive NO CARRIER response without waiting this amount
            Serial1.write(query);
            Serial1.flush();
        }
        timeout = millis() + 5000;
        while(millis() < timeout) {
            BLE->poll();
            if(Serial1.available() < 1) continue;
            buffer[size] = Serial1.read();
            Serial.write(buffer[size]);
            size++;
            if(size >= 6
                && buffer[size - 1] == 10 
                && buffer[size - 2] == 13
                && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K' // OK
            ) {
                if(i == AT_HTTPACTION_IDX) { // special case for AT+HTTPACTION response responding OK before query resolve :/
                    while(Serial1.available() < 1 && millis() < timeout) { BLE->poll(); }
                    while(Serial1.available() > 0 && millis() < timeout) {
                        BLE->poll();
                        buffer[size] = Serial1.read();
                        Serial.write(buffer[size]);
                        size++;
                    }
                }
                buffer[size] = '\0';
                if(i == AT_HTTPREAD_IDX) { // special case for AT+HTTPREAD to extract the response
                    int16_t responseIdxStart = -1;
                    for(int idx = 0; idx < size - 7; idx++) {
                        BLE->poll();
                        if(responseIdxStart == -1 && buffer[idx] == '{') responseIdxStart = idx;
                        if(responseIdxStart > -1) response[idx - responseIdxStart] = buffer[idx];
                        if(responseIdxStart > -1 && idx == size - 8) response[idx - responseIdxStart + 1] = '\0';
                    }
                }
                break;
            }
        }
        if(millis() >= timeout) {
            Utilities::analogWriteRGB(70, 5, 0);
            Serial.println(">>Timeout<<");
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
    }
    char substr[10];
    memcpy(substr, response, 10);
    substr[9] = '\0';
    if (strcmp(substr, "{\"errors\"") == 0) {
        Serial.println("Graph Failure: Clearing accessToken");
        memset(tokenData.accessToken, 0, 100);
        tokenData.isValid = false;
        flashTokenData.write(tokenData);
        Serial.println("accessToken cleared");
    }
    setFunMode(false);
    return doc;
}

void Network::setFunMode(bool fullFunctionality) {
    memset(buffer, 0, RESPONSE_SIZE);
    uint8_t size = 0;
    Serial1.print("AT+CFUN=");
    Serial1.println(fullFunctionality ? "1" : "4");
    Serial1.flush();
    unsigned long timeout = millis() + 2000;
    while(timeout > millis()) {
        if(Serial1.available()) {
            buffer[size] = Serial1.read();
            size++;
        }
        if(fullFunctionality && size > 12
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

void Network::GetImei(char* imeiBuffer) {
    memset(buffer, 0, RESPONSE_SIZE);
    uint8_t size = 0;
    char command[] = "AT+GSN\r";
    Serial1.write(command);
    Serial1.flush();
    unsigned long timeout = millis() + 2000;
    while (timeout > millis())
    {
        if(Serial1.available()) {
            buffer[size] = Serial1.read();
            size++;
        }
        if(size >= 6
                && buffer[size - 1] == 10 
                && buffer[size - 2] == 13
                && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K') // OK
        {
            buffer[size] = '\0';
            uint8_t imeiLen = size - strlen(command) - 10;
            strncpy(imeiBuffer, buffer + strlen(command) + 2, imeiLen);
            imeiBuffer[imeiLen] = '\0';
            return;
        }
    }
    
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