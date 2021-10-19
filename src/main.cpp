#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include "RTClib.h"
#include "SoftwareSerial.h"
#include "TinyGPS++.h"

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define SEGMENT_CLOCK 33
#define SEGMENT_LATCH 27
#define SEGMENT_DATA 32

#define BLE_SERVICE_UUID                 "3000df4c-06f3-464a-b2ea-58d624db1abd"
#define BLE_DATETIME_CHARACTERISTIC_UUID "5b919181-93a6-4abc-9aa6-c9a9b6c06760"

// October 26th 4 PM JST
#define TARGET_UTC_MONTH 10
#define TARGET_UTC_DAY 26
#define TARGET_UTC_HOUR 7

RTC_DS1307 rtc;
SoftwareSerial ss;
TinyGPSPlus gps;

uint32_t targetUnixTime;
uint32_t lastSeenUnixTime;

class CountdownCallbacks: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    std::string value = pCharacteristic->getValue();

    if (value.length() > 0)
    {
      Serial.println("*********");
      Serial.print("New value: ");

      DateTime updated(value.c_str());

      Serial.printf("%d-%d-%d %d:%d:%d (UTC)", updated.year(), updated.month(), updated.day(), updated.hour(), updated.minute(), updated.second());
      Serial.println();
      Serial.println("Time updated...");
      rtc.adjust(updated);
      Serial.println("*********");
    }
  }
};

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
  pinMode(SEGMENT_CLOCK, OUTPUT);
  pinMode(SEGMENT_DATA, OUTPUT);
  pinMode(SEGMENT_LATCH, OUTPUT);

  digitalWrite(SEGMENT_CLOCK, LOW);
  digitalWrite(SEGMENT_DATA, LOW);
  digitalWrite(SEGMENT_LATCH, LOW);

  BLEDevice::init("Countdown");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         BLE_DATETIME_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE |
                                         BLECharacteristic::PROPERTY_WRITE_NR
                                       );
  pCharacteristic->setCallbacks(new CountdownCallbacks());
  pCharacteristic->setValue("UNSET");
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Finished setup");
}

//Given a number, or '-', shifts it out to the display
void postNumber(byte number, boolean decimal)
{
  //    -  A
  //   / / F/B
  //    -  G
  //   / / E/C
  //    -. D/DP

#define a  1<<0
#define b  1<<6
#define c  1<<5
#define d  1<<4
#define e  1<<3
#define f  1<<1
#define g  1<<2
#define dp 1<<7

  byte segments;

  switch (number)
  {
    case 1: segments = b | c; break;
    case 2: segments = a | b | d | e | g; break;
    case 3: segments = a | b | c | d | g; break;
    case 4: segments = f | g | b | c; break;
    case 5: segments = a | f | g | c | d; break;
    case 6: segments = a | f | g | e | c | d; break;
    case 7: segments = a | b | c; break;
    case 8: segments = a | b | c | d | e | f | g; break;
    case 9: segments = a | b | c | d | f | g; break;
    case 0: segments = a | b | c | d | e | f; break;
    case ' ': segments = 0; break;
    case 'c': segments = g | e | d; break;
    case '-': segments = g; break;
  }

  if (decimal) segments |= dp;

  //Clock these bits out to the drivers
  for (byte x = 0 ; x < 8 ; x++)
  {
    digitalWrite(SEGMENT_CLOCK, LOW);
    digitalWrite(SEGMENT_DATA, segments & 1 << (7 - x));
    // ESP32 is too fast, slow down
    delay(1);
    digitalWrite(SEGMENT_CLOCK, HIGH); //Data transfers to the register on the rising edge of SRCK
  }
}

void showChunk(int twoDigits, bool trailingDot)
{
  postNumber(twoDigits%10, trailingDot);
  postNumber(twoDigits/10, false);
}

// Displays a date as DD.HH.MM.SS
void showDate(int days, int hours, int minutes, int seconds)
{
  //Serial.print("number: ");
  //Serial.println(number);

  showChunk(seconds, false);
  showChunk(minutes, true);
  showChunk(hours, true);
  showChunk(days, true);

  //Latch the current segment data
  digitalWrite(SEGMENT_LATCH, LOW);
  // ESP32 is too fast, slow down
  delay(1);
  digitalWrite(SEGMENT_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
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

  showDate(remainingTotalDays, remainingHours, remainingMinutes, remainingSeconds);
}

//#define PRINT_RAW_GPS

void loop() {
  while (ss.available()) {
    char readC = ss.read();
    gps.encode(readC);

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