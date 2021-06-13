#ifndef NETWORK_H
#define NETWORK_h

#include <ArduinoJson.h>

// Needs to be large enough for error messages
const uint16_t RESPONSE_SIZE = 2000;

class Network {
    private:
        /**
         * Static memory used for reading network response, might need to increase size if expecting larger responses
        **/
        char buffer[RESPONSE_SIZE]{};

    public:
        /**
         * Mutatable token to access API_URL as Hub, set once registration is successful
        **/
        char accessToken[100]{};

        void InitializeAccessToken();

        void SetAccessToken(const char newAccessToken[100]);

        /**
         * Sends a request containing query to API_URL, returns a json document with 
         * response in the "data" field if no errors, otherwise errors will be in "errors" 
        **/
        DynamicJsonDocument SendRequest(char* query);
};

#endif