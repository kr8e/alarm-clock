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

// Relay pins (adjust to your wiring)
const int RELAY_TONE_1_PIN = 26;
const int RELAY_TONE_2_PIN = 27;

// Alarm pattern timing
const unsigned long CHIRP_1_MS = 1000;           // 1 second chirp
const unsigned long WAIT_AFTER_CHIRP_1_MS = 30000; // 30 seconds
const unsigned long CHIRP_2_MS = 5000;           // 5 second chirp
const unsigned long WAIT_AFTER_CHIRP_2_MS = 120000; // 2 minutes
const unsigned long CONTINUOUS_ALARM_MS = 30000; // 30 seconds
const unsigned long TONE_SWITCH_MS = 400;        // dual-tone alternation during continuous phase

// OLED (SF_16225-1 is typically SSD1306-compatible, 128x64, I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

enum AlarmPhase {
  ALARM_IDLE,
  ALARM_CHIRP_1,
  ALARM_WAIT_1,
  ALARM_CHIRP_2,
  ALARM_WAIT_2,
  ALARM_CONTINUOUS,
};

bool alarmTriggeredToday = false;
AlarmPhase alarmPhase = ALARM_IDLE;
unsigned long phaseStartedMs = 0;
unsigned long lastToneSwitchMs = 0;
bool toneState = false;
int lastDayOfYear = -1;

void setRelays(bool tone1, bool tone2) {
  // Many relay modules are active LOW. Flip logic if your module is active HIGH.
  digitalWrite(RELAY_TONE_1_PIN, tone1 ? LOW : HIGH);
  digitalWrite(RELAY_TONE_2_PIN, tone2 ? LOW : HIGH);
}

void stopAlarm() {
  alarmPhase = ALARM_IDLE;
  setRelays(false, false);
}

void beginPhase(AlarmPhase phase) {
  alarmPhase = phase;
  phaseStartedMs = millis();

  if (phase == ALARM_CHIRP_1 || phase == ALARM_CHIRP_2) {
    // Chirp on tone 1
    setRelays(true, false);
  } else if (phase == ALARM_WAIT_1 || phase == ALARM_WAIT_2 || phase == ALARM_IDLE) {
    setRelays(false, false);
  } else if (phase == ALARM_CONTINUOUS) {
    toneState = true;
    lastToneSwitchMs = millis();
    setRelays(true, false);
  }
}

void startAlarm() {
  beginPhase(ALARM_CHIRP_1);
}

void updateAlarmSequence() {
  if (alarmPhase == ALARM_IDLE) return;

  const unsigned long nowMs = millis();

  switch (alarmPhase) {
    case ALARM_CHIRP_1:
      if (nowMs - phaseStartedMs >= CHIRP_1_MS) {
        beginPhase(ALARM_WAIT_1);
      }
      break;

    case ALARM_WAIT_1:
      if (nowMs - phaseStartedMs >= WAIT_AFTER_CHIRP_1_MS) {
        beginPhase(ALARM_CHIRP_2);
      }
      break;

    case ALARM_CHIRP_2:
      if (nowMs - phaseStartedMs >= CHIRP_2_MS) {
        beginPhase(ALARM_WAIT_2);
      }
      break;

    case ALARM_WAIT_2:
      if (nowMs - phaseStartedMs >= WAIT_AFTER_CHIRP_2_MS) {
        beginPhase(ALARM_CONTINUOUS);
      }
      break;

    case ALARM_CONTINUOUS:
      if (nowMs - phaseStartedMs >= CONTINUOUS_ALARM_MS) {
        stopAlarm();
        break;
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
      break;

    case ALARM_IDLE:
      break;
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

const char* alarmPhaseLabel() {
  switch (alarmPhase) {
    case ALARM_IDLE:
      return "IDLE";
    case ALARM_CHIRP_1:
      return "CH1";
    case ALARM_WAIT_1:
      return "W1";
    case ALARM_CHIRP_2:
      return "CH2";
    case ALARM_WAIT_2:
      return "W2";
    case ALARM_CONTINUOUS:
      return "CONT";
    default:
      return "?";
  }
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
  display.printf("Alarm %02d:%02d", alarmHour, alarmMinute);

  display.setCursor(88, 52);
  display.print(alarmPhaseLabel());

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

  if (cmd == "START") {
    startAlarm();
    Serial.println("Alarm sequence started.");
    return;
  }

  Serial.println("Commands: ALARM HH:MM | START | STOP");
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
  display.println("START | STOP");
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

  updateAlarmSequence();
  drawDisplay(timeinfo);

  delay(100);
}
