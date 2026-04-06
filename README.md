# ESP32-WROOM-32 Alarm Clock (Dual-Tone Siren)

This project implements a network-synchronized alarm clock using:

- **ESP32-WROOM-32**
- **2-channel relay module**
- **MG 335 dual-tone siren**
- **SF_16225-1 OLED display** (SSD1306-compatible I2C assumed)
- **LM2596 DC-DC converter**

## 1) Safety + Power Architecture

The siren current is too high for ESP32 GPIO directly. Use relays and separate power rails as needed.

Recommended power flow:

1. Main DC input -> **LM2596 input**.
2. LM2596 output set to **5V** -> ESP32 5V/VIN + relay module VCC.
3. Siren supply uses its rated voltage/current.
4. **Common GND** between ESP32, relay module, and siren supply.

> Verify MG 335 electrical ratings before final wiring.

## 2) Wiring

### ESP32 -> OLED (I2C)

- GPIO 21 -> SDA
- GPIO 22 -> SCL
- 3V3 (or module-rated VCC) -> VCC
- GND -> GND

### ESP32 -> Relay module

- GPIO 26 -> IN1 (Tone 1)
- GPIO 27 -> IN2 (Tone 2)
- 5V -> VCC (module dependent)
- GND -> GND

### Relay module -> MG 335 siren

Use relay contacts based on your siren tone wiring:

- Relay 1: tone path A
- Relay 2: tone path B

## 3) Firmware features

Main sketch: `alarm_clock_esp32.ino`

- Wi-Fi + NTP time sync.
- OLED time display with next upcoming enabled alarm day/time.
- Alarm sequence:
  - 1 sec chirp
  - wait 30 sec
  - 5 sec chirp
  - wait 2 min
  - 30 sec continuous alarm
- Per-day scheduling with separate time per weekday (Sun..Sat), each day independently enabled/disabled.
- Built-in web app to edit alarm time + weekdays and start/stop alarm.

## 4) Web app

After boot, open the ESP32 IP shown on OLED in your browser.

The web UI lets you:

- set independent `hour` and `minute` for each weekday
- enable/disable each weekday (Sun..Sat)
- manually `Start Alarm` / `Stop Alarm`

Routes:

- `GET /` -> configuration page
- `POST /save` -> save hour/minute/weekday config
- `GET /action?cmd=start|stop` -> manual control

## 5) Serial commands

At 115200 baud:

- `ALARM HH:MM` -> set alarm time for all days
- `DAY D HH:MM` -> set one day time (`D`: 0=Sun..6=Sat)
- `START` -> manually start sequence
- `STOP` -> stop active alarm

## 6) Libraries

Install in Arduino IDE:

- `Adafruit GFX Library`
- `Adafruit SSD1306`

Used from ESP32 core:

- `WiFi.h`
- `WebServer.h`
- `time.h`
- `Wire.h`

## 7) Config to edit

In the sketch:

- `WIFI_SSID`, `WIFI_PASSWORD`
- `GMT_OFFSET_SEC`, `DAYLIGHT_OFFSET_SEC`
- per-day defaults in `alarmHours[7]`, `alarmMinutes[7]`, `alarmWeekdays[7]`
- relay pins if different from GPIO 26/27

## 8) Flashing

1. Open `alarm_clock_esp32.ino` in Arduino IDE.
2. Board: **ESP32 Dev Module** (or your specific ESP32-WROOM-32 profile).
3. Install required libraries.
4. Upload and open Serial Monitor at 115200.
5. Read the device IP from OLED and open it in a browser.
