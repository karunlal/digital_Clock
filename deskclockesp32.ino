/*
  ========================================
  📟 ESP32 DIGITAL CLOCK with BUZZER CHIMES
  ========================================

  🔧 FEATURES:
  -------------------------
  ✅ Wi-Fi connected 4-in-1 MAX7219 LED Clock (12-hour format)
  ✅ Synchronizes time via NTP (internet time)
  ✅ Hourly chime using Active Buzzer (Pattern 2: Triple Beep)
  ✅ Half-hour chime (Pattern 5: Fast Burst)
  ✅ Automatic brightness ramp during chime
  ✅ Buzzer silenced during display updates
  ✅ 12-hour display format with leading space (e.g., " 9:05")
  ✅ Date shown for first 3 seconds each minute
  ✅ Weekday display (e.g., MON, TUE)
  ✅ Fallback to manual time via Serial input (no NTP required)
  ✅ Type `reset` in Serial Monitor to return to real-time sync

  🔌 SERIAL COMMANDS:
  -------------------------
  > set DD-MM-YYYY HH:MM:SS
    - Example: set 06-06-2025 14:58:58
    - Sets manual date/time (used for testing chimes)
  
  > reset
    - Switches back to internet (NTP) time

  📢 CHIME TESTING (for buzzer):
  -------------------------
  1️⃣ HOURLY CHIME:
     - Set time to 2 minutes before an hour mark:
       Example: `set 06-06-2025 14:58:58`
       ⏰ At 15:00:00, 3 beeps will occur (Pattern 2 × 3 → total 9 short beeps)

  2️⃣ HALF-HOUR CHIME:
     - Set time to 2 minutes before a half-hour:
       Example: `set 06-06-2025 15:28:58`
       ⏰ At 15:30:00, fast burst beep (Pattern 5) will sound

  🛠 NOTES:
  -------------------------
  - Display blinks or scrolls text for Time/Date/Weekday
  - ESP32 stays connected to WiFi with power save disabled
  - Active buzzer is connected to GPIO 4 (LOW = ON)

  🔁 VERSION HISTORY:
  -------------------------
  v1.0  Basic clock with buzzer
  v1.1  Added pattern-based chime system
  v1.2  Manual date/time entry via Serial + reset feature

  🧠 Author: Your Name
  📅 Last Updated: June 6, 2025
*/

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

const int NORMAL_BRIGHTNESS = 0;
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

bool manualMode = false;  // True if manual time set via Serial
time_t manualEpoch = 0;   // Stores manual epoch time
unsigned long lastManualMillis = 0;

String inputString = "";  // To hold incoming serial commands
bool stringComplete = false;

// -------------------- Buzzer Patterns --------------------

// Using your buzzer patterns from pattern 2 (triple beep) and 5 (fast burst)

void beep(int duration) {
  digitalWrite(BUZZER_PIN, LOW);  // Active buzzer ON (LOW)
  delay(duration);
  digitalWrite(BUZZER_PIN, HIGH); // OFF
}

void pause(int duration) {
  delay(duration);
}

void tripleBeep() {
  for (int i = 0; i < 3; i++) {
    beep(100);
    pause(100);
  }
}

void fastBurst() {
  for (int i = 0; i < 6; i++) {
    beep(50);
    pause(50);
  }
}

// -------------------- Chime Functions --------------------

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
    tripleBeep();
    if (i < chimeCount - 1) {
      delay(chimeDelay);
    }
  }

  fadeBrightness(NORMAL_BRIGHTNESS);
  isChiming = false;
}

void playHalfHourChime() {
  isChiming = true;
  fadeBrightness(CHIME_BRIGHTNESS);
  fastBurst();
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

// -------------------- Serial Command Processing --------------------

void processSerialInput() {
  inputString.trim();
  inputString.toLowerCase();

  if (inputString.startsWith("set ")) {
    // Expected format: set DD-MM-YYYY HH:MM:SS
    // Example: set 06-06-2025 14:58:58
    int d1 = inputString.indexOf(' ');
    int d2 = inputString.indexOf(' ', d1 + 1);
    if (d2 == -1) {
      Serial.println("Invalid command format.");
      return;
    }
    String datePart = inputString.substring(d1 + 1, d2);
    String timePart = inputString.substring(d2 + 1);

    int day = datePart.substring(0, 2).toInt();
    int month = datePart.substring(3, 5).toInt();
    int year = datePart.substring(6, 10).toInt();

    int hour = timePart.substring(0, 2).toInt();
    int minute = timePart.substring(3, 5).toInt();
    int second = timePart.substring(6, 8).toInt();

    if (day == 0 || month == 0 || year == 0 || hour > 23 || minute > 59 || second > 59) {
      Serial.println("Invalid date/time values.");
      return;
    }

    tmElements_t tm;
    tm.Day = day;
    tm.Month = month;
    tm.Year = year - 1970;  // TimeLib uses years since 1970
    tm.Hour = hour;
    tm.Minute = minute;
    tm.Second = second;

    manualEpoch = makeTime(tm);
    manualMode = true;
    lastManualMillis = millis();

    Serial.print("Manual time set to: ");
    Serial.print(day); Serial.print("-");
    Serial.print(month); Serial.print("-");
    Serial.print(year); Serial.print(" ");
    Serial.print(hour); Serial.print(":");
    Serial.print(minute); Serial.print(":");
    Serial.println(second);
  }
  else if (inputString == "reset") {
    manualMode = false;
    Serial.println("Switched back to NTP (real) time.");
  }
  else {
    Serial.println("Unknown command.");
  }

  inputString = "";
  stringComplete = false;
}

// -------------------- Setup & Loop --------------------

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);  // Buzzer OFF initially

  Display.begin(2);
  Display.setZone(0, 1, 3);
  Display.setZone(1, 0, 0);
  Display.setFont(0, SmallDigits);
  Display.setFont(1, SmallerDigits);
  Display.setIntensity(NORMAL_BRIGHTNESS);
  Display.setCharSpacing(0);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    Display.displayZoneText(0, ".", PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
  }
  Serial.println("\nWiFi connected");

  timeClient.begin();
  esp_wifi_set_ps(WIFI_PS_NONE);

  Serial.println("Type commands in Serial:");
  Serial.println("set DD-MM-YYYY HH:MM:SS  --> Set manual time");
  Serial.println("reset                    --> Switch back to NTP time");
}

void loop() {
  if (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        stringComplete = true;
      }
    }
    else {
      inputString += inChar;
    }
  }

  if (stringComplete) {
    processSerialInput();
  }

  time_t currentEpoch;

  if (manualMode) {
    // Increment manual time every second
    if (millis() - lastManualMillis >= 1000) {
      manualEpoch++;
      lastManualMillis += 1000;
    }
    currentEpoch = manualEpoch;
  }
  else {
    timeClient.update();
    currentEpoch = timeClient.getEpochTime();
  }

  second_ = second(currentEpoch);

  if (last_second != second_) {
    minute_ = minute(currentEpoch);
    hour_ = hour(currentEpoch);
    day_ = day(currentEpoch);
    month_ = month(currentEpoch);
    year_ = year(currentEpoch);

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

    int currentDayOfWeek = timeClient.getDay();  // 0=Sun...6=Sat

    if (!isChiming) {
      if (second_ < 3) {
        Display.displayZoneText(0, DaysWeek[currentDayOfWeek], PA_LEFT, Display.getSpeed(), Display.getPause(), PA_NO_EFFECT);
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
    digitalWrite(BUZZER_PIN, HIGH);  // Buzzer off when not chiming
  }

  delay(100);
}
