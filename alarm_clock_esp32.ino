#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================
// User configuration
// =========================
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

// UTC offset in seconds (example: UTC-5 = -18000)
const long GMT_OFFSET_SEC = -18000;
const int DAYLIGHT_OFFSET_SEC = 3600;

// Per-day alarm configuration: 0=Sun,1=Mon,...6=Sat
bool alarmWeekdays[7] = {false, true, true, true, true, true, false};
int alarmHours[7] = {7, 7, 7, 7, 7, 7, 7};
int alarmMinutes[7] = {0, 0, 0, 0, 0, 0, 0};

// Relay pins (adjust to your wiring)
const int RELAY_TONE_1_PIN = 26;
const int RELAY_TONE_2_PIN = 27;

// Alarm pattern timing
const unsigned long CHIRP_1_MS = 1000;              // 1 second chirp
const unsigned long WAIT_AFTER_CHIRP_1_MS = 30000;  // 30 seconds
const unsigned long CHIRP_2_MS = 5000;              // 5 second chirp
const unsigned long WAIT_AFTER_CHIRP_2_MS = 120000; // 2 minutes
const unsigned long CONTINUOUS_ALARM_MS = 30000;    // 30 seconds
const unsigned long TONE_SWITCH_MS = 400;           // dual-tone alternation during continuous phase

// OLED (SF_16225-1 is typically SSD1306-compatible, 128x64, I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

WebServer server(80);
const char* DAY_LABELS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

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
      if (nowMs - phaseStartedMs >= CHIRP_1_MS) beginPhase(ALARM_WAIT_1);
      break;
    case ALARM_WAIT_1:
      if (nowMs - phaseStartedMs >= WAIT_AFTER_CHIRP_1_MS) beginPhase(ALARM_CHIRP_2);
      break;
    case ALARM_CHIRP_2:
      if (nowMs - phaseStartedMs >= CHIRP_2_MS) beginPhase(ALARM_WAIT_2);
      break;
    case ALARM_WAIT_2:
      if (nowMs - phaseStartedMs >= WAIT_AFTER_CHIRP_2_MS) beginPhase(ALARM_CONTINUOUS);
      break;
    case ALARM_CONTINUOUS:
      if (nowMs - phaseStartedMs >= CONTINUOUS_ALARM_MS) {
        stopAlarm();
        break;
      }
      if (nowMs - lastToneSwitchMs >= TONE_SWITCH_MS) {
        lastToneSwitchMs = nowMs;
        toneState = !toneState;
        setRelays(toneState, !toneState);
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
  if (!getLocalTime(&timeinfo)) return false;
  return true;
}

bool isWeekdayEnabled(int wday) {
  if (wday < 0 || wday > 6) return false;
  return alarmWeekdays[wday];
}

bool getNextAlarmFromNow(const struct tm& now, int& nextDay, int& nextHour, int& nextMinute) {
  for (int dayOffset = 0; dayOffset <= 7; dayOffset++) {
    int day = (now.tm_wday + dayOffset) % 7;
    if (!alarmWeekdays[day]) continue;

    int h = alarmHours[day];
    int m = alarmMinutes[day];

    if (dayOffset == 0) {
      bool isStillAhead = (now.tm_hour < h) || (now.tm_hour == h && now.tm_min < m) || (now.tm_hour == h && now.tm_min == m && now.tm_sec == 0);
      if (!isStillAhead) continue;
    }

    nextDay = day;
    nextHour = h;
    nextMinute = m;
    return true;
  }

  return false;
}

void drawDisplay(const struct tm& t) {
  char timeBuf[16];
  char dateBuf[24];
  int today = (t.tm_wday >= 0 && t.tm_wday <= 6) ? t.tm_wday : 0;
  int nextDay = today;
  int nextHour = alarmHours[today];
  int nextMinute = alarmMinutes[today];
  bool hasNextAlarm = getNextAlarmFromNow(t, nextDay, nextHour, nextMinute);
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
  strftime(dateBuf, sizeof(dateBuf), "%a %d %b %Y", &t);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  //display.setTextSize(1);
  //display.setCursor(0, 0);
  //display.println("Alarm Clock");

  display.setTextSize(3);
  display.setCursor(0, 2);
  display.println(timeBuf);

  display.setTextSize(2);
  display.setCursor(0, 40);
  if (hasNextAlarm) {
    display.printf("%s %02d:%02d", DAY_LABELS[nextDay], nextHour, nextMinute);
  } else {
    display.print("Alarm OFF");
  }
  //display.println(dateBuf);

  //display.setCursor(0, 40);
  //display.printf("%02d:%02d", alarmHour, alarmMinute);

  display.display();
}

String dayScheduleRow(int dayIndex) {
  String checked = alarmWeekdays[dayIndex] ? "checked" : "";
  String row = "<tr><td>";
  row += DAY_LABELS[dayIndex];
  row += "</td><td><input type='checkbox' name='d";
  row += dayIndex;
  row += "' value='1' ";
  row += checked;
  row += "></td><td><input type='number' min='0' max='23' name='h";
  row += dayIndex;
  row += "' value='";
  row += alarmHours[dayIndex];
  row += "'></td><td><input type='number' min='0' max='59' name='m";
  row += dayIndex;
  row += "' value='";
  row += alarmMinutes[dayIndex];
  row += "'></td></tr>";
  return row;
}

String buildWebPage() {
  String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<style>body{font-family:Arial;margin:18px}input,button{font-size:1rem;padding:6px;margin:4px}fieldset{margin-top:10px}table{border-collapse:collapse}th,td{padding:6px 8px;border-bottom:1px solid #ddd;text-align:left}</style>";
  page += "</head><body><h2>Michael's Alarm Clock</h2>";
  page += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  page += "<form method='POST' action='/save'>";
  page += "<fieldset><legend>Per-day alarm schedule</legend>";
  page += "<table><tr><th>Day</th><th>Enable</th><th>Hour</th><th>Minute</th></tr>";
  for (int i = 0; i < 7; i++) {
    page += dayScheduleRow(i);
  }
  page += "</table></fieldset><button type='submit'>Save</button></form>";
  page += "<p><a href='/action?cmd=start'><button>Start Alarm</button></a> ";
  page += "<a href='/action?cmd=stop'><button>Stop Alarm</button></a></p>";
  page += "</body></html>";
  return page;
}

void handleRoot() {
  server.send(200, "text/html", buildWebPage());
}

void handleSave() {
  for (int i = 0; i < 7; i++) {
    alarmWeekdays[i] = server.hasArg(String("d") + i);
    String hourArgName = String("h") + i;
    String minuteArgName = String("m") + i;

    if (server.hasArg(hourArgName)) {
      int h = server.arg(hourArgName).toInt();
      if (h >= 0 && h <= 23) alarmHours[i] = h;
    }
    if (server.hasArg(minuteArgName)) {
      int m = server.arg(minuteArgName).toInt();
      if (m >= 0 && m <= 59) alarmMinutes[i] = m;
    }
  }

  alarmTriggeredToday = false;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Saved");
}

void handleAction() {
  String cmd = server.arg("cmd");
  if (cmd == "start") {
    startAlarm();
  } else if (cmd == "stop") {
    stopAlarm();
  }
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "OK");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/action", HTTP_GET, handleAction);
  server.begin();
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.startsWith("ALARM ")) {
    String hhmm = cmd.substring(6);
    int sep = hhmm.indexOf(':');
    if (sep > 0) {
      int hh = hhmm.substring(0, sep).toInt();
      int mm = hhmm.substring(sep + 1).toInt();
      if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
        for (int i = 0; i < 7; i++) {
          alarmHours[i] = hh;
          alarmMinutes[i] = mm;
        }
        alarmTriggeredToday = false;
        Serial.printf("Alarm set for all days to %02d:%02d\n", hh, mm);
        return;
      }
    }
  }

  if (cmd.startsWith("DAY ")) {
    String rest = cmd.substring(4);
    int firstSpace = rest.indexOf(' ');
    if (firstSpace > 0) {
      int day = rest.substring(0, firstSpace).toInt();
      String hhmm = rest.substring(firstSpace + 1);
      int sep = hhmm.indexOf(':');
      if (day >= 0 && day <= 6 && sep > 0) {
        int hh = hhmm.substring(0, sep).toInt();
        int mm = hhmm.substring(sep + 1).toInt();
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          alarmHours[day] = hh;
          alarmMinutes[day] = mm;
          alarmTriggeredToday = false;
          Serial.printf("%s set to %02d:%02d\n", DAY_LABELS[day], hh, mm);
          return;
        }
      }
    }
  }

  if (cmd == "START") {
    startAlarm();
    Serial.println("Alarm sequence started.");
    return;
  }

  if (cmd == "STOP") {
    stopAlarm();
    Serial.println("Alarm stopped.");
    return;
  }

  Serial.println("Commands: ALARM HH:MM | DAY D HH:MM | START | STOP");
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_TONE_1_PIN, OUTPUT);
  pinMode(RELAY_TONE_2_PIN, OUTPUT);
  setRelays(false, false);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    while (true) delay(1000);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting Wi-Fi...");
  display.display();

  connectWiFi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  setupWebServer();

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Time synced.");
  display.println("Web UI ready.");
  display.println(WiFi.localIP());
  display.display();
  delay(1200);
}

void loop() {
  server.handleClient();
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
      isWeekdayEnabled(timeinfo.tm_wday) &&
      timeinfo.tm_hour == alarmHours[timeinfo.tm_wday] &&
      timeinfo.tm_min == alarmMinutes[timeinfo.tm_wday] &&
      timeinfo.tm_sec < 2) {
    startAlarm();
    alarmTriggeredToday = true;
  }

  updateAlarmSequence();
  drawDisplay(timeinfo);

  delay(100);
}
