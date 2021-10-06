#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include "SoftwareSerial.h"
#include "TinyGPS++.h"

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// October 26th 4 PM JST
#define TARGET_UTC_MONTH 10
#define TARGET_UTC_DAY 26
#define TARGET_UTC_HOUR 7

RTC_DS1307 rtc;
SoftwareSerial ss;
TinyGPSPlus gps;

uint32_t targetUnixTime;
uint32_t lastSeenUnixTime;

void setup() {
  DateTime targetTime("2021-10-26 07:00:00");
  targetUnixTime = targetTime.unixtime();

  while (!Serial);
  Serial.begin(9600);
  if (!rtc.begin()) {
    Serial.println("Failed to start RTC");
  }
  Serial.println(targetTime.hour());
  ss.begin(9600, SWSERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println("Finished setup");
}

void printRemainingTime() {
  uint32_t remainingTotalSeconds = targetUnixTime - rtc.now().unixtime();
  uint32_t remainingTotalMinutes = remainingTotalSeconds / 60;
  uint32_t remainingTotalHours = remainingTotalMinutes / 60;
  uint32_t remainingTotalDays = remainingTotalHours / 24;

  uint32_t remainingSeconds = remainingTotalSeconds % 60;
  uint32_t remainingMinutes = remainingTotalMinutes % 60;
  uint32_t remainingHours = remainingTotalHours % 24;

  Serial.printf("%2d days, %02d:%02d:%02d remaining\n", remainingTotalDays, remainingHours, remainingMinutes, remainingSeconds);
}

//#define PRINT_RAW_GPS

void loop() {
  while (ss.available()) {
    char c = ss.read();
    gps.encode(c);

#ifdef PRINT_RAW_GPS
    // Some fun stuff here to poke at to see raw NMEA data, but let's let the library handle parsing this for us
    Serial.print(c);
#endif
  }

  if (gps.time.isValid() && gps.time.isUpdated()) {
    // Hacky but for now, avoid crazy GPS values... this code is just for quick reference anyway!
    if (gps.date.year() == 2021) {
      char buf[128];
      sprintf(buf, "%4d-%02d-%02d %02d:%02d:%02d", gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second());
      DateTime gpsTime(buf);
      uint32_t diff = gpsTime.unixtime() - rtc.now().unixtime();

      if (diff > 1) {
        Serial.println("Time updated...");
        Serial.println(buf);
        rtc.adjust(gpsTime);
      }
    }
  }

  DateTime now = rtc.now();

  if (now.unixtime() != lastSeenUnixTime) {
    printRemainingTime();
    lastSeenUnixTime = now.unixtime();
  }
}