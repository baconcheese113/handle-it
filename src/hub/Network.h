#ifndef NETWORK_H
#define NETWORK_h

#include <ArduinoJson.h>

class Network {
    private:
        /**
         * Static memory used for reading network response, might need to increase size if expecting larger responses
        **/
        char buffer[500];

    public:
        // TODO transfer to simulated EEPROM
        /**
         * Mutatable token to access API_URL as Hub, set once registration is successful
        **/
        const char* accessToken;

        /**
         * Sends a request containing query to API_URL, returns a json document with 
         * response in the "data" field if no errors, otherwise errors will be in "errors" 
        **/
        StaticJsonDocument<400> SendRequest(char* query);
};

#endif