# ESP32-WROOM-32 Alarm Clock (Dual-Tone Siren)

This project implements a network-synchronized alarm clock using:

- **ESP32-WROOM-32**
- **2-channel relay module**
- **MG 335 dual-tone siren**
- **SF_16225-1 OLED display** (SSD1306-compatible I2C assumed)
- **LM2596 DC-DC converter**

The alarm time is displayed on OLED and triggered once per day. During alarm, the ESP32 alternates relay channels to produce a dual-tone effect on the siren.

## 1) Safety + Power Architecture

The siren current is too high for ESP32 GPIO directly. Use separate power rail and relays.

Recommended power flow:

1. Main input supply (for example 12V DC) -> **LM2596 input**.
2. LM2596 output adjusted to **5V** -> ESP32 5V/VIN pin + relay module VCC.
3. Siren supply uses its rated voltage (check MG 335 datasheet).
4. **Common GND** between ESP32, relay module, and siren supply.

> Always verify the siren voltage/current rating before wiring.

## 2) Wiring

## ESP32 -> OLED (I2C)

- GPIO 21 -> SDA
- GPIO 22 -> SCL
- 3V3 (or module-rated VCC) -> VCC
- GND -> GND

## ESP32 -> Relay module

- GPIO 26 -> IN1 (Tone 1)
- GPIO 27 -> IN2 (Tone 2)
- 5V -> VCC (module dependent)
- GND -> GND

## Relay module -> MG 335 siren

Use relay contacts to route power path(s) for tone selection according to siren wiring requirements:

- Relay 1 controls Tone A path
- Relay 2 controls Tone B path

If your siren has dedicated wires for different tones, connect each relay to the corresponding wire.
If your siren expects polarity or pattern switching, adapt relay contact wiring accordingly.

## 3) Firmware

Main sketch: `alarm_clock_esp32.ino`

Libraries needed in Arduino IDE:

- `Adafruit GFX Library`
- `Adafruit SSD1306`

Built-in ESP32/Arduino libs used:

- `WiFi.h`
- `time.h`
- `Wire.h`

### Config you must edit

In the sketch:

- `WIFI_SSID`, `WIFI_PASSWORD`
- `GMT_OFFSET_SEC`, `DAYLIGHT_OFFSET_SEC`
- `alarmHour`, `alarmMinute`
- `RELAY_TONE_1_PIN`, `RELAY_TONE_2_PIN` (if different)

### Serial commands

At 115200 baud:

- `ALARM HH:MM` -> set alarm time (24-hour)
- `STOP` -> stop active alarm

Example:

```text
ALARM 06:30
```

## 4) Behavior

- Syncs time from NTP (`pool.ntp.org`, `time.nist.gov`).
- Shows current date/time on OLED.
- Triggers alarm **once per day** when current time equals alarm time.
- Alternates relay channels every 400ms to create dual-tone siren effect.
- Auto-stops after 5 minutes (or via `STOP`).

## 5) Notes for SF_16225-1 OLED

This sketch assumes:

- 128x64 resolution
- SSD1306 controller
- I2C address `0x3C`

If your module differs, update:

- `SCREEN_WIDTH` / `SCREEN_HEIGHT`
- `OLED_I2C_ADDR`

## 6) Flashing

1. Open `alarm_clock_esp32.ino` in Arduino IDE.
2. Select board: **ESP32 Dev Module** (or your exact WROOM-32 profile).
3. Install required libraries.
4. Upload.
5. Open Serial Monitor at 115200 baud.

---

If you want, I can also provide a version with:

- physical buttons to set alarm without Serial,
- buzzer pre-alarm,
- multiple alarms,
- weekday-only scheduling.
