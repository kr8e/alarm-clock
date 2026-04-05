#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================
// User configuration
// =========================
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// UTC offset in seconds (example: UTC-5 = -18000)
const long GMT_OFFSET_SEC = -18000;
const int DAYLIGHT_OFFSET_SEC = 3600;

// Default alarm time (24-hour format)
int alarmHour = 7;
int alarmMinute = 0;

// Alarm duration and tone switching
const unsigned long ALARM_DURATION_MS = 5UL * 60UL * 1000UL;  // 5 minutes
const unsigned long TONE_SWITCH_MS = 400;

// Relay pins (adjust to your wiring)
const int RELAY_TONE_1_PIN = 26;
const int RELAY_TONE_2_PIN = 27;

// OLED (SF_16225-1 is typically SSD1306-compatible, 128x64, I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool alarmTriggeredToday = false;
bool alarmActive = false;
unsigned long alarmStartMs = 0;
unsigned long lastToneSwitchMs = 0;
bool toneState = false;
int lastDayOfYear = -1;

void setRelays(bool tone1, bool tone2) {
  // Many relay modules are active LOW. Flip logic if your module is active HIGH.
  digitalWrite(RELAY_TONE_1_PIN, tone1 ? LOW : HIGH);
  digitalWrite(RELAY_TONE_2_PIN, tone2 ? LOW : HIGH);
}

void stopAlarm() {
  alarmActive = false;
  setRelays(false, false);
}

void startAlarm() {
  alarmActive = true;
  alarmStartMs = millis();
  lastToneSwitchMs = millis();
  toneState = false;
  setRelays(true, false);
}

void updateAlarmTone() {
  if (!alarmActive) return;

  unsigned long nowMs = millis();
  if (nowMs - alarmStartMs >= ALARM_DURATION_MS) {
    stopAlarm();
    return;
  }

  if (nowMs - lastToneSwitchMs >= TONE_SWITCH_MS) {
    lastToneSwitchMs = nowMs;
    toneState = !toneState;
    if (toneState) {
      setRelays(true, false);
    } else {
      setRelays(false, true);
    }
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

bool getLocalTimeSafe(struct tm& timeinfo) {
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  return true;
}

void drawDisplay(const struct tm& t) {
  char timeBuf[16];
  char dateBuf[24];
  strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &t);
  strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", &t);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP32 Alarm Clock");

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println(timeBuf);

  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println(dateBuf);

  display.setCursor(0, 52);
  display.printf("Alarm: %02d:%02d", alarmHour, alarmMinute);

  if (alarmActive) {
    display.setCursor(88, 52);
    display.print("ON");
  }

  display.display();
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  // Format: ALARM HH:MM
  if (cmd.startsWith("ALARM ")) {
    String hhmm = cmd.substring(6);
    int sep = hhmm.indexOf(':');
    if (sep > 0) {
      int hh = hhmm.substring(0, sep).toInt();
      int mm = hhmm.substring(sep + 1).toInt();
      if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
        alarmHour = hh;
        alarmMinute = mm;
        alarmTriggeredToday = false;
        Serial.printf("Alarm set to %02d:%02d\n", alarmHour, alarmMinute);
        return;
      }
    }
  }

  if (cmd == "STOP") {
    stopAlarm();
    Serial.println("Alarm stopped.");
    return;
  }

  Serial.println("Commands: ALARM HH:MM | STOP");
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_TONE_1_PIN, OUTPUT);
  pinMode(RELAY_TONE_2_PIN, OUTPUT);
  setRelays(false, false);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting Wi-Fi...");
  display.display();

  connectWiFi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Time synced.");
  display.println("Serial cmds:");
  display.println("ALARM HH:MM");
  display.println("STOP");
  display.display();
  delay(1200);
}

void loop() {
  handleSerialCommands();

  struct tm timeinfo;
  if (!getLocalTimeSafe(timeinfo)) {
    delay(200);
    return;
  }

  // Reset one-shot alarm trigger each new day
  if (timeinfo.tm_yday != lastDayOfYear) {
    lastDayOfYear = timeinfo.tm_yday;
    alarmTriggeredToday = false;
  }

  if (!alarmTriggeredToday &&
      timeinfo.tm_hour == alarmHour &&
      timeinfo.tm_min == alarmMinute &&
      timeinfo.tm_sec < 2) {
    startAlarm();
    alarmTriggeredToday = true;
  }

  updateAlarmTone();
  drawDisplay(timeinfo);

  delay(100);
}
