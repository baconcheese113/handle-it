#ifndef HUB_LOCATION_H
#define HUB_LOCATION_H

struct LocReading {
  bool hasFix = false;
  double lat = 0;
  double lng = 0;
  double kmph = 0;
  double deg = 0;
  double hdop = 0;
};

// Interval is the amount of time between checks
const unsigned long GPS_UPDATE_INTERVAL = 10 * 60 * 1000;
// When a check is ready to occur, the module is 
// powered on for this amount of time before reading
const unsigned long GPS_BUFFER_TIME = 20000;

class Location
{

private:
  static double getRadians(double degrees);

public:
  // The last time (in millis) that location was queried
  unsigned long lastGPSTime = 0;
  // // The last latitude sent to the server
  // double lastSentLat = 0;
  // // The last longitude sent to the server
  // double lastSentLng = 0;
  LocReading lastSentReading;

  // If the GPS module is powered on (should be off on init)
  bool isPowered = false;

  static void printLocReading(LocReading reading);

  /**
   * Returns the distance (in meters) between 2 locations
   */
  static double distance(double lat1, double lng1, double lat2, double lng2);

  /**
   * Returns the distance (in meters) between passed in point
   * and lastSent point
   */
  double distanceFromLastPoint(double lat, double lng) {
    return distance(lat, lng, lastSentReading.lat, lastSentReading.lng);
  }

  /**
   * Parse a line received from the AT+CGNSINF command
   * and return a LocReading struct
   */
  static LocReading parseInf(char* infBuffer);

  /**
   * Powers on/off GPS module
   */
  void setGPSPower(bool turnOn);



};

#endif