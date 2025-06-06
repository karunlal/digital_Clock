#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include "Fonts.h"
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include "esp_wifi.h"

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CS_PIN 5

#define BUZZER_PIN 4
const unsigned long chimeDelay = 400;
const unsigned long halfHourChimeDelay = 300;

const int NORMAL_BRIGHTNESS = 2;
const int CHIME_BRIGHTNESS = 15;
const int fadeDelay = 50;

char* ssid = "Premlal";
char* password = "62383349";
const long utcOffsetInSeconds = 19800;

WiFiUDP ntpUDP;
MD_Parola Display = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
NTPClient timeClient(ntpUDP, "asia.pool.ntp.org", utcOffsetInSeconds, 60000);

char Time[] = "00:00";
char Seconds[] = "00";
char Date[] = "00-00-2000";
byte last_second, second_, minute_, hour_, day_, month_;
int year_;

int lastHour = -1;
int lastMinute = -1;
uint8_t currentBrightness = NORMAL_BRIGHTNESS;
bool isChiming = false;

const char* DaysWeek[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// Buzzer functions using digitalWrite (works with passive buzzer)
void buzzOn() {
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzOff() {
  digitalWrite(BUZZER_PIN, HIGH);
}

void buzz(int duration) {
  if (duration > 0) {
    buzzOn();
    delay(duration);
    buzzOff();
  }
}

void buzzPause(int duration) {
  buzzOff();
  if (duration > 0) delay(duration);
}

void windChime() {
  buzz(150); buzzPause(150);
  buzz(100); buzzPause(200);
  buzz(200); buzzPause(100);
  buzz(100); buzzPause(300);
  buzz(300); buzzPause(200);
  buzz(150); buzzPause(100);
  buzz(250); buzzPause(100);
  buzzOff();
}

void halfHourChime() {
  buzz(200); buzzPause(100);
  buzz(300); buzzPause(100);
  buzz(200); buzzPause(100);
  buzz(400); buzzPause(100);
  buzzOff();
}

void fadeBrightness(uint8_t targetBrightness) {
  if (targetBrightness == currentBrightness) return;

  int step = (targetBrightness > currentBrightness) ? 1 : -1;
  for (int b = currentBrightness; b != targetBrightness; b += step) {
    Display.setIntensity(b);
    delay(fadeDelay);
  }
  Display.setIntensity(targetBrightness);
  currentBrightness = targetBrightness;
}

void playHourlyChime(int hour24) {
  int chimeCount = hour24 % 12;
  if (chimeCount == 0) chimeCount = 12;

  isChiming = true;
  fadeBrightness(CHIME_BRIGHTNESS);

  for (int i = 0; i < chimeCount; i++) {
    windChime();
    if (i < chimeCount - 1) {
      delay(chimeDelay);
    }
  }

  buzzOff();
  fadeBrightness(NORMAL_BRIGHTNESS);
  isChiming = false;
}

void playHalfHourChime() {
  isChiming = true;
  fadeBrightness(CHIME_BRIGHTNESS);
  halfHourChime();
  buzzOff();
  fadeBrightness(NORMAL_BRIGHTNESS);
  isChiming = false;
}

void handleTimeBasedChimes() {
  int currentHour = hour_;
  int currentMinute = minute_;

  if (currentMinute == 0 && lastMinute != 0) {
    lastHour = currentHour;
    lastMinute = currentMinute;
    playHourlyChime(currentHour);
  }
  else if (currentMinute == 30 && lastMinute != 30) {
    lastMinute = currentMinute;
    playHalfHourChime();
  }
  else if (currentMinute != lastMinute) {
    lastMinute = currentMinute;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  buzzOff();

  Display.begin(2);
  Display.setZone(0, 1, 3);
  Display.setZone(1, 0, 0);
  Display.setFont(0, SmallDigits);
  Display.setFont(1, SmallerDigits);
  Display.setIntensity(NORMAL_BRIGHTNESS);
  Display.setCharSpacing(0);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Display.displayZoneText(0, ".", PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
  }

  timeClient.begin();
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void loop() {
  timeClient.update();
  unsigned long unix_epoch = timeClient.getEpochTime();
  int Day = timeClient.getDay();

  second_ = second(unix_epoch);

  if (last_second != second_) {
    minute_ = minute(unix_epoch);
    hour_ = hour(unix_epoch);
    day_ = day(unix_epoch);
    month_ = month(unix_epoch);
    year_ = year(unix_epoch);

    if (!isChiming) {
      handleTimeBasedChimes();
    }

    Seconds[1] = second_ % 10 + '0';
    Seconds[0] = second_ / 10 + '0';
    
    Time[4] = minute_ % 10 + '0';
    Time[3] = minute_ / 10 + '0';

    int displayHour = hour_ % 12;
    if (displayHour == 0) displayHour = 12;

    Time[1] = displayHour % 10 + '0';
    Time[0] = (displayHour >= 10) ? (displayHour / 10 + '0') : ' ';

    Date[0] = day_ / 10 + '0';
    Date[1] = day_ % 10 + '0';
    Date[3] = month_ / 10 + '0';
    Date[4] = month_ % 10 + '0';
    Date[8] = (year_ / 10) % 10 + '0';
    Date[9] = year_ % 10 + '0';

    if (!isChiming) {
      if (second_ < 3) {
        Display.displayZoneText(0, DaysWeek[Day], PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
        Display.displayZoneText(1, Date, PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
      } else {
        Display.displayZoneText(0, Time, PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
        Display.displayZoneText(1, Seconds, PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
      }
      Display.displayAnimate();
    }

    last_second = second_;
  }

  if (!isChiming) {
    buzzOff();
  }

  delay(500);
}
