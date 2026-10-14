# Apprentice-Test-Stand

# ESP32 Apprentice Stand (v3)

A wireless measurement system built with an ESP32, a SparkFun HX711 load cell amplifier, a Phidgets S-type load cell, and an MS4525DO pitot airspeed sensor. Once uploaded, the ESP32 creates its own WiFi hotspot. You connect to it from any phone or laptop, open a browser, and see a live graph of sensor weight and airspeed over time. No apps, no installs, no internet needed.

This is the follow-up to [DBF-Test-Stand](https://github.com/W0wza/DBF-Test-Stand). Same load cell setup, plus a pitot tube and a few upgrades.

## What it does

- Creates a WiFi hotspot called **ApprenticeStand**
- Hosts a live dashboard at **192.168.4.1**
- One graph, two lines: **weight (lb)** on the left axis and **airspeed (ft/s)** on the right axis
- Shows current weight, peak weight, current airspeed, peak airspeed, time, and raw counts
- Logs all data and exports it as **CSV** or **Excel (.xlsx)**
- Stopwatch that auto-starts when weight exceeds 0.25 lb
- **Tare Weight** button to re-zero the load cell at any time
- **Zero Airspeed** button to re-zero the pitot at any time
- Airspeed gate: airspeed reads 0 until it holds above 5 ft/s for 0.3 s, so idle noise stays off the graph
- Prints a status line to Serial Monitor every second for debugging

## Hardware you need

| Part | Link |
| --- | --- |
| ESP32 Dev Module (ESP-WROOM-32) | [Amazon](https://www.amazon.com/ESP-WROOM-32-Development-Microcontroller-Integrated-Compatible/dp/B08D5ZD528?th=1) |
| SparkFun HX711 Load Cell Amplifier | [SparkFun SEN-13879](https://www.sparkfun.com/sparkfun-load-cell-amplifier-hx711.html) |
| Phidgets S-Type Load Cell 10kg | [Phidgets 1305](https://www.phidgets.com/?prodid=1305) |
| MS4525DO I2C Airspeed Sensor + pitot tube | mRo PX4 Airspeed v1.8 or 3DR MS4525DO Airspeed (5V version, 1 psi, I2C address 0x28) |
| Silicone tubing | Pitot tube to sensor, keep it short with no kinks |
| Micro USB data cable | Must be a data cable, charge-only cables won't upload |

## Optional parts

| Part | Notes |
| --- | --- |
| USB power bank | Runs the stand away from a laptop |
| BSS138 level shifter | Only needed if your pitot board pulls SDA/SCL up to 5V. See the pitot check in [CALIBRATION_GUIDE.md](CALIBRATION_GUIDE.md) |

## Wiring

### Load cell → HX711

| Load cell wire | HX711 pad |
| --- | --- |
| Red | RED (E+) |
| Black | BLK (E-) |
| White | WHT (A-) |
| Green | GRN (A+) |
| Yellow | Not connected |

### HX711 → ESP32

| HX711 pin | Wire color | ESP32 pin |
| --- | --- | --- |
| VDD | Red | 3V3 |
| VCC | Blue | VIN (5V) |
| DAT | Yellow | D4 (GPIO 4) |
| CLK | Green | D5 (GPIO 5) |
| GND | Black | GND |

VDD sets the logic voltage on DAT and CLK. It has to go to **3V3**, not 5V, or the HX711 sends 5V signals into the ESP32.

### Pitot sensor → ESP32

| Sensor pad | Plug pin | ESP32 pin |
| --- | --- | --- |
| 5V | 1 (red) | VIN (5V), shared with HX711 VCC |
| SCL | 2 | D22 (GPIO 22) |
| SDA | 3 | D21 (GPIO 21) |
| GND | 4 | GND (the free one on the left side under 3V3) |

VIN only exists once, so the pitot 5V and the HX711 VCC share it. Solder both onto the pin or splice into the HX711 wire. All GND pins on the ESP32 are connected, so any GND works.

### Pitot tubes

The sensor has two ports. One goes to the pitot tip (total pressure), the other to the side holes (static pressure). If airspeed stays at 0 when you blow into the tip, the tubes are swapped. Swap them.

## Software setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software)
2. Add ESP32 board support: **Tools → Boards Manager → search "esp32" → install esp32 by Espressif Systems**
3. Install the HX711 library: **Tools → Manage Libraries → search "HX711" → install HX711 by Rob Tillaart** (Bogdan Necula's version also works)
4. Run `Calibration.ino` first and get your `counts_per_lb`. See [CALIBRATION_GUIDE.md](CALIBRATION_GUIDE.md)
5. Open `ApprenticeStand_v3.ino` and put your number into `counts_per_lb`
6. Select **Tools → Board → ESP32 Dev Module**
7. Select the correct port (on Mac it looks like `/dev/cu.usbserial-0001`)
8. Upload

Arduino wants each sketch in a folder with the same name, so it may offer to move `ApprenticeStand_v3.ino` into an `ApprenticeStand_v3` folder. Say yes.

If the upload fails with "Unable to verify flash chip connection", set **Tools → Upload Speed → 115200** and hold the **BOOT** button while it says "Connecting...".

## Usage

1. Power the ESP32 with USB or a power bank. **Cover the pitot tip and keep the load cell unloaded for about 3 seconds while it boots.** That's when it tares the weight and zeros the airspeed.
2. On your phone or laptop, connect to WiFi: **ApprenticeStand** / password: **apprentice26**
3. Open a browser and go to **192.168.4.1**
4. The dashboard loads automatically. The status bar under the graph should say **Pitot: OK**

### Dashboard buttons

| Button | What it does |
| --- | --- |
| Pause / Resume | Stops or restarts adding points to the graph and the log |
| Reset Peaks | Clears peak weight and peak airspeed |
| Tare Weight | Re-zeros the load cell, restarts the clock, clears the graph |
| Zero Airspeed | Re-zeros the pitot and re-checks that it's connected. Cover the tip first |
| Download CSV | Saves `time_s, weight_lb, airspeed_fps` |
| Download Excel | Saves a real `.xlsx` with a frozen header row, named with the date and time |
| Clear | Wipes the graph and the data log |

The bottom bar has a stopwatch that starts on its own when weight passes 0.25 lb, with manual Pause and Reset.

## Settings you might change

All of these are near the top of `ApprenticeStand_v3.ino`.

| Setting | Default | What it does |
| --- | --- | --- |
| `AP_SSID` | `ApprenticeStand` | WiFi name |
| `AP_PASS` | `apprentice26` | WiFi password, must be 8+ characters |
| `counts_per_lb` | `200955.5` | Load cell calibration from `Calibration.ino` |
| `RHO` | `1.056` | Air density in kg/m^3. 1.056 is about 5,000 ft (Prescott). Use 1.225 for sea level |
| `AS_ON_FPS` | `5.0` | Airspeed must pass this (ft/s) to activate |
| `AS_HOLD_MS` | `300` | ...for this long (ms) |
| `DP_DEADBAND_PA` | `3.0` | Pressure below this reads as 0 airspeed |
| `LOG_INTERVAL_MS` | `100` | Log rate, 100 ms = 10 rows per second |
| `YMX` (in the HTML) | `16` | Weight axis max (lb) |
| `SMX` (in the HTML) | `160` | Airspeed axis max (ft/s) |

## Files in this repo

| File | Description |
| --- | --- |
| `ApprenticeStand_v3.ino` | Main sketch, upload this to the ESP32 |
| `Calibration.ino` | Load cell calibration sketch, run this first |
| `pitot_test.ino` | Bare pitot test, prints raw pressure to Serial Monitor. Use it if airspeed isn't working |
| `CALIBRATION_GUIDE.md` | Step by step calibration for the load cell and pitot |
| `CODE_EXPLAINED.md` | Full walkthrough of how the code works |

## Version history

| Version | Changes |
| --- | --- |
| v1 | [DBF-Test-Stand](https://github.com/W0wza/DBF-Test-Stand): thrust only, CSV export |
| v2 | Added pitot airspeed as a second line on the graph, renamed to Apprentice Stand, navy/red/orange theme, thrust relabeled as sensor weight, Excel export, airspeed gate (10 ft/s for 1 s), streamed CSV with a 10 minute buffer |
| v3 | Faster airspeed gate (5 ft/s for 0.3 s) so short bursts register |

## MUST READ VERY IMPORTANT

- The load cell is rated to 10 kg (~22 lb). DO NOT EXCEED THIS.
- Always tare with no load on the cell, and cover the pitot tip when zeroing airspeed.
- If weight reads negative, swap the GRN and WHT wires on the HX711, or flip the sign of `counts_per_lb`.
- If airspeed stays at 0 when air is clearly moving, swap the two pitot tubes on the sensor.
- Mount the pitot tip ahead of the propeller and out of the prop wash, pointed straight into the oncoming air.
- The data log is stored in ESP32 RAM and resets when power is removed. DOWNLOAD DATA BEFORE UNPLUGGING.
