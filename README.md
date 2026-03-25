# DBF-Test-Stand
# ESP32 Thrust Stand

A wireless thrust measurement system for RC motors and propellers built with an ESP32, SparkFun HX711 load cell amplifier, and a Phidgets S-type load cell. Once uploaded, the ESP32 creates its own WiFi hotspot. You connect to it from any phone or laptop, open a browser, and see a live thrust graph with no apps or installs required.

## What it does

- Creates a WiFi hotspot called **ThrustStand**
- Hosts a live dashboard at **192.168.4.1**
- Displays current thrust, peak thrust, and a live graph
- Logs all data as a CSV you can download
- Stopwatch that auto-starts when thrust exceeds 0.25 lb
- Tare button to re-zero the scale at any time

## Hardware you need

| Part | Link |
|------|------|
| ESP32 Dev Module (ESP-WROOM-32) | [Amazon](https://www.amazon.com/ESP-WROOM-32-Development-Microcontroller-Integrated-Compatible/dp/B08D5ZD528?th=1) |
| SparkFun HX711 Load Cell Amplifier | [SparkFun SEN-13879](https://www.sparkfun.com/sparkfun-load-cell-amplifier-hx711.html) |
| Phidgets S-Type Load Cell 10kg | [Phidgets 1305](https://www.phidgets.com/?prodid=1305) |
| 6ft Micro USB Cable 2-Pack | [Amazon](https://www.amazon.com/Ruaeoda-Pet-Proof-Charging-Lifespan-Controller/dp/B0D7RZJSPS/ref=sr_1_4?crid=3G2WVE9Y4596D&dib=eyJ2IjoiMSJ9._mH12OXXLPe8yOEf8iz2XxJXTuBfTR_JVlGjT5444wUlWP1c1utRN32tFcMsKpxSN7TXyqtFq8LeBGblg_BKpd9tnxRG-ba5Dpr_yZ5A2Q-z5xramcQ_8u1kYZakBuT2aAypEF8jWMCFl5IiNE22gP-_0RROEEjClhMFI-mNPx3nAW-gqBiAh4Dyj4UNt45HYBQhvz5DURDaKl3mPzSdk0ARwy7656IQdVT4pj_KHo8.6Z7EJ4P_ug0nMhr-gHK_aI3Iyh9R0qLOvUvQp-MzUu8&dib_tag=se&keywords=usb%2Ba%2Bto%2Bmicro%2Busb%2B6ft&qid=1774474694&sprefix=usb%2Ba%2Bto%2Bmicro%2Busb%2B6ft%2Caps%2C215&sr=8-4&th=1)|



## Optional Parts
| Part | Link |
|------|------|
| Anker Power Bank | [Amazon](https://www.amazon.com/Anker-Travel-Ready-Technology-High-Speed-Output%EF%BC%88Black%EF%BC%89%EF%BC%8C1pack/dp/B0D5CLSMFB/ref=sr_1_4?crid=3CHMQSVQL48E5&dib=eyJ2IjoiMSJ9.SHsRVlUQTmF5onGaXYOGwIv8Qd9j8DFnXBLfmJUuxqWF9JZaFxj0gkmiGCxsVop7lF6G5CnYpR8rQR203sqbOLGl5SXRXvqjYoE50TuD-vweMTETBZKxf0f1gAAZP-P2K5DryNjhkgS-o8z6R2PAkHj6fKzPFOICns6ow0klvm000ZLyWS1eoDxevTQN-P4w8xWQwgbEvQyS7i-n33u0dmTrlsZxp-d3jYXLEVOIY2c.SyWUk5itz6tJoHcFuAlTHX2axGv1IlExHrFk8fey8uM&dib_tag=se&keywords=anker%2Bbattery%2Bpack&qid=1774474629&sprefix=anker%2Bbattery%2Bpack%2Caps%2C211&sr=8-4&th=1) |



## Wiring

### Load cell → HX711
| Load cell wire | HX711 pad |
|----------------|-----------|
| Red | RED (E+) |
| Black | BLK (E−) |
| Green | GRN (A+) |
| White | WHT (A−) |
| Yellow | Not connected |

### HX711 → ESP32
| HX711 pin | ESP32 pin |
|-----------|-----------|
| VCC | VIN (5V) |
| VDD | 3V3 |
| GND | GND |
| DAT | GPIO 4 |
| CLK | GPIO 5 |

## Software setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add ESP32 board support: **Tools → Boards Manager → search "esp32" → install Espressif Systems ESP32**
3. Install the HX711 library: **Tools → Manage Libraries → search "HX711" → install HX711 by Rob Tillaart**
4. Open `ThrustStand.ino`
5. Select **Tools → Board → ESP32 Dev Module**
6. Select the correct COM port
7. Upload

## Usage

1. Power the ESP32 Via the battery pack
2. On your phone or laptop, connect to WiFi: **ThrustStand** / password: **thrust1234**
3. Open a browser and go to **192.168.4.1**
4. The dashboard loads automatically

## Files in this repo

| File | Description |
|------|-------------|
| `ThrustStand.ino` | Main sketch — upload this to the ESP32 |
| `Calibration.ino` | Calibration sketch — run this first before using the main sketch |
| `CALIBRATION_GUIDE.md` | Step by step calibration instructions |
| `CODE_EXPLAINED.md` | Full walkthrough of how the code works |

## MUST READ VERY IMPORTANT

- The load cell is rated to 10 kg (~22 lb). DO NOT EXCEED THIS.
- Always tare the scale before a test run with no load on the cell.
- If thrust reads negative, swap the GRN and WHT wires on the HX711.
- The CSV log is stored in ESP32 RAM and resets when power is removed. DOWNLOAD DATA BEFORE UNPLUGGING.
