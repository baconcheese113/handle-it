#include <./conf.cpp>
#include <./hub/Network.h>

uint8_t AT_HTTPDATA_LEN_IDX = 7;
uint8_t AT_HTTPACTION_IDX = 8;
uint8_t AT_HTTPREAD_IDX = 9;

StaticJsonDocument<400> Network::SendRequest(char* query) {
    Serial.write("Sending request\n");

    char authCommand[55 + 75];
    if(strlen(accessToken)) {
        sprintf(authCommand, "AT+HTTPPARA=\"USERDATA\",\"Authorization:Bearer %s\"\n", accessToken);
    } else {
        strcpy(authCommand, "AT+HTTPPARA=\"USERDATA\",\"\"\n");
    }

    char urlCommand[30 + strlen(API_URL)];
    sprintf(urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"\n", API_URL);

    char lenCommand[30];
    sprintf(lenCommand, "AT+HTTPDATA=%d,%d\n", strlen(query), 5000);

    static const char* const commands[] = {
        "AT+SAPBR=3,1,\"Contype\",\"GPRS\"\n",
        "AT+SAPBR=1,1\n",
        "AT+HTTPINIT\n",
        "AT+HTTPPARA=\"CID\",1\n",
        authCommand,
        urlCommand,
        "AT+HTTPPARA=\"CONTENT\",\"application/json\"\n",
        lenCommand,
        "AT+HTTPACTION=1\n",
        "AT+HTTPREAD\n",
        "AT+HTTPTERM\n",
        "AT+SAPBR=0,1\n",
    };

    char response[200];
    int size;
    unsigned int commandsLen = sizeof commands / sizeof *commands;
    unsigned long timeout;
    Serial.print("Commands to iterate through: ");
    Serial.println(commandsLen);
    for(uint8_t i = 0; i < commandsLen; i++) {
        memset(buffer, 0, 500);
        size = 0;

        Serial1.write(commands[i]);
        Serial1.flush();
        if(i == AT_HTTPDATA_LEN_IDX) { // send query to HTTPDATA command
            delay(10);
            Serial1.write(query);
            Serial1.flush();
        }
        timeout = millis() + 10000;
        while(millis() < timeout) {
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
                    while(Serial1.available() < 1 && millis() < timeout);
                    while(Serial1.available() > 0 && millis() < timeout) {
                        buffer[size] = Serial1.read();
                        Serial.write(buffer[size]);
                        size++;
                    }
                }
                buffer[size] = '\0';
                if(i == AT_HTTPREAD_IDX) { // special case for AT+HTTPREAD to extract the response
                    int16_t responseIdxStart = -1;
                    for(int idx = 0; idx < size - 7; idx++) {
                        if(responseIdxStart == -1 && buffer[idx] == '{') responseIdxStart = idx;
                        if(responseIdxStart > -1) response[idx - responseIdxStart] = buffer[idx];
                        if(idx == size - 8) response[idx - responseIdxStart + 1] = '\0';
                    }
                }
                break;
            }
        }
    }
    Serial.print("Request complete\nResponse is: ");
    Serial.println(response);

    StaticJsonDocument<400> doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
    }
    return doc;
}

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