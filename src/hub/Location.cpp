#include <./hub/Location.h>
#include <Arduino.h>

double Location::getRadians(double degrees) {
  return degrees * PI / 180;
}

void Location::printLocReading(LocReading reading) {
  Serial.print("Latitude: ");
  Serial.print(reading.lat, 5);
  Serial.print(", Longitude: ");
  Serial.print(reading.lng, 5);
  Serial.print(", HDOP(m): ");
  Serial.print(reading.hdop);
  Serial.print(", Speed(kmph): ");
  Serial.print(reading.kmph);
  Serial.print(", Course(deg): ");
  Serial.println(reading.deg);
}

double Location::distance(double lat1, double lng1, double lat2, double lng2) { 
  lat1 = Location::getRadians(lat1);
  lng1 = Location::getRadians(lng1);
  lat2 = Location::getRadians(lat2);
  lng2 = Location::getRadians(lng2);

  double lngDistance = lng2 - lng1;
  double latDistance = lat2 - lat1;

  double dist = pow(sin(latDistance / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(lngDistance / 2), 2);
  dist = 2 * asin(sqrt(dist));
  // Radius of Earth in KM, 6371 or 3956 miles. Multiplied by 100 to convert from km to m
  return dist *= 6371.0 * 1000.0;
}

LocReading Location::parseInf(char* infBuffer) {
  uint8_t paramNum = 0;
  uint8_t paramStart = 0;
  char tempBuf[20]{};
  LocReading reading;
  memset(tempBuf, 0, 20);
  for(uint8_t idx = 0; idx < strlen(infBuffer); idx++) {
    if(infBuffer[idx] == ',') {
      tempBuf[idx - paramStart] = '\0';
      if(paramStart < idx) {
        if(paramNum == 1) {
          reading.hasFix = tempBuf[0] == '1';
        } else if(paramNum == 3) { // Lat
          reading.lat = atof(tempBuf);
        } else if(paramNum == 4) { // Lng
          reading.lng = atof(tempBuf);
        } else if(paramNum == 6) { // kmph
          reading.kmph = atof(tempBuf);
        } else if(paramNum == 7) { // deg
          reading.deg = atof(tempBuf);
        } else if(paramNum == 10) { // HDOP
          reading.hdop = atof(tempBuf);
        }
      }
      paramStart = idx + 1;
      paramNum++;
      memset(tempBuf, 0, 20);
    } else {
      tempBuf[idx - paramStart] = infBuffer[idx];
    }
  }
  return reading;
}