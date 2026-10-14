# Code Explained

A walkthrough of how `ApprenticeStand_v3.ino` works, plus the two helper sketches. The main sketch is one file with three parts: the ESP32 code that reads the sensors, an HTML dashboard stored as a string, and web handlers that connect the two.

## Quick start: connecting and using the stand

### 1. Power it up

1. Plug the ESP32 into USB (laptop or power bank).
2. **For the first 3 seconds, keep the load cell unloaded and cover the pitot tip.** During boot it tares the weight and zeros the airspeed, so anything on it gets treated as zero.
3. The ESP32 starts its own WiFi hotspot. It doesn't need internet or any other network.

### 2. Join the WiFi

| | |
| --- | --- |
| **Network name** | `ApprenticeStand` |
| **Password** | `apprentice26` |

- **iPhone:** Settings → Wi-Fi → ApprenticeStand → enter the password. It may say **"No Internet Connection"**. That's normal, stay connected.
- **Android:** Settings → Wi-Fi → ApprenticeStand. If it asks **"Keep Wi-Fi connection?"** or says the network has no internet, tap **Keep** or **Stay connected**.
- **Mac/PC:** Click the WiFi icon → ApprenticeStand → enter the password.

If your phone keeps jumping back to cell data, turn off mobile data while you're using the stand.

### 3. Open the dashboard

Open any browser (Safari, Chrome) and go to:

```
192.168.4.1
```

Type it exactly like that, with no `www` and no `.com`. The dashboard loads in a second or two. The status bar under the graph should say **Logging: ON** and **Pitot: OK**.

More than one phone or laptop can connect and watch at the same time.

### 4. Run a test

1. Press **Tare Weight** right before you start, with nothing on the cell.
2. If the pitot was exposed to wind at boot, cover the tip and press **Zero Airspeed**.
3. Run the test. The red line is weight (left axis) and the orange-yellow line is airspeed (right axis).
4. The stopwatch at the bottom starts on its own when weight passes 0.25 lb.

### 5. Export your data

Press **Download Excel** or **Download CSV** before unplugging anything.

| Button | You get | Best for |
| --- | --- | --- |
| **Download Excel** | `apprentice_stand_v3_YYYYMMDD_HHMM.xlsx` with columns Time (s), Weight (lb), Airspeed (ft/s) and a frozen header row | Opening straight in Excel or Google Sheets |
| **Download CSV** | `apprentice_stand_log.csv` with columns `time_s, weight_lb, airspeed_fps` | MATLAB, Python, or anything else |

Where the file ends up:

- **iPhone:** Safari asks to download it. Tap **Download**, then find it in the **Files** app → **Downloads**.
- **Android:** Chrome saves it to the **Downloads** folder.
- **Mac/PC:** your normal **Downloads** folder.

**Export before you unplug.** The data lives in the ESP32's memory and is gone the moment it loses power.

### 6. How exporting works

- The ESP32 records a row (time, weight, airspeed) **10 times per second, the whole time it's powered on**, whether or not anyone is connected. Closing the browser or walking out of WiFi range doesn't stop it.
- It holds the most recent **10 minutes** (6,000 rows). After that, the oldest rows get overwritten.
- **Download CSV** asks the ESP32 for the whole log, and it streams the file to your browser in small chunks.
- **Download Excel** grabs that same CSV, then your browser converts it into a real `.xlsx` file right on your device. That's why it works with no internet connection.
- **Pause** stops new rows from being added. **Clear** erases the log. **Tare Weight** restarts the clock, so time in the file starts over from 0 after a tare.
- The live graph only shows the last 600 points (about 2.5 minutes). The downloaded file has everything in the 10 minute log.

The rest of this document explains how the code makes all of that happen.

## How it fits together

```
Load cell → HX711 ─┐
                   ├─→ ESP32 reads both in loop() ─→ ring buffer log (RAM)
Pitot → MS4525DO ──┘            │
                                └─→ WiFi hotspot + web server
                                            │
                     Phone/laptop browser ←─┘
                     polls /sample 4x per second, draws the graph,
                     downloads /csv or builds the Excel file itself
```

Everything runs on the ESP32. The browser does the drawing and the Excel file building, so the ESP32 stays fast.

## 1. Includes and settings

```cpp
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HX711.h"
```

- `WiFi.h` and `WebServer.h` create the hotspot and serve the dashboard
- `Wire.h` is I2C, used to talk to the pitot sensor
- `HX711.h` reads the load cell amplifier

The settings block near the top holds everything you'd normally change: WiFi name and password, pins, `counts_per_lb`, air density `RHO`, pitot filtering, and logging rate. See the settings table in the [README](README.md).

## 2. Reading the pitot sensor

### readPitotRaw()

The MS4525DO sends 4 bytes over I2C:

| Byte | Contents |
| --- | --- |
| 0 | Top 2 bits = status, bottom 6 bits = high part of pressure |
| 1 | Low part of pressure |
| 2, 3 | Temperature (not used here) |

```cpp
if ((b0 >> 6) != 0) return false;   // 0 = fresh, 2 = stale, 3 = fault
uint16_t p = ((uint16_t)(b0 & 0x3F) << 8) | b1;
```

Status 0 means fresh data. Anything else gets skipped. The pressure is a 14-bit number (0 to 16383). This sensor is "type A, ±1 psi", so 10% of full scale (1638 counts) is -1 psi and 90% (14745 counts) is +1 psi:

```cpp
float psi = ((float)p - 0.1f * 16383.0f) * 2.0f / (0.8f * 16383.0f) - 1.0f;
dPa = psi * 6894.757f;   // psi → Pa
```

### zeroPitot()

Checks that something answers at address 0x28, then averages up to 100 readings over 2 seconds. That average is the zero offset that gets subtracted from every reading after. Runs at boot and when you press **Zero Airspeed**. It also resets the airspeed gate.

### updatePitot()

Called about 50 times a second from `loop()`. Three steps:

**1. Smooth it.** An exponential moving average cuts out jitter:

```cpp
dpFilt += DP_SMOOTH * (dp - dpFilt);   // DP_SMOOTH = 0.2
```

Lower `DP_SMOOTH` is smoother but slower to react.

**2. Convert to airspeed.** Pitot pressure is dynamic pressure, so:

```
dp = ½ × rho × v²   →   v = √(2 × dp / rho)
```

```cpp
float use = (dpFilt > DP_DEADBAND_PA) ? dpFilt : 0.0f;
float rawFps = sqrtf(2.0f * use / RHO) * 3.28084f;   // m/s → ft/s
```

Anything under `DP_DEADBAND_PA` (3 Pa, about 8 ft/s) counts as 0, which kills sensor noise at rest. Negative pressure (tubes backwards) also reads as 0.

**3. Gate it.** Airspeed only turns on after it stays above `AS_ON_FPS` for `AS_HOLD_MS`, and only turns off after staying below for the same time:

```cpp
if (rawFps >= AS_ON_FPS) {
  belowSince = 0;
  if (aboveSince == 0) aboveSince = now;
  if (!asActive && now - aboveSince >= AS_HOLD_MS) asActive = true;
} else {
  aboveSince = 0;
  if (belowSince == 0) belowSince = now;
  if (asActive && now - belowSince >= AS_HOLD_MS) asActive = false;
}
lastFps = asActive ? rawFps : 0.0f;
```

This stops random spikes or someone walking past from showing up, and keeps it from flickering on and off right at the threshold. v3 uses 5 ft/s for 300 ms.

## 3. Reading the load cell

### updateLoadCell()

```cpp
if (!scale.is_ready()) return;
float c = (float)scale.read();
```

Non-blocking: it only reads when the HX711 has a new sample ready (10 per second on most boards), so the web server never has to wait on it.

The last 3 readings get averaged for smoothing, then converted to pounds:

```cpp
lastThrustRaw = (lastCounts - (float)scale.get_offset()) / counts_per_lb;
lastThrust = (lastThrustRaw < 0.0f && lastThrustRaw > -0.20f) ? 0.0f : lastThrustRaw;
```

`get_offset()` is the tare value. Tiny negatives between 0 and -0.2 lb get clamped to 0 so the graph sits clean at rest. Bigger negatives still show, so you'd notice a wiring or sign problem. The variable names still say "thrust" from v1, but this is the sensor weight shown on the dashboard.

## 4. Data logging

### logRecord()

Every `LOG_INTERVAL_MS` (100 ms), the time, weight, and airspeed get saved into a ring buffer:

```cpp
struct Rec { uint32_t t_ms; float thrust; float fps; };
Rec logBuf[LOG_MAX];   // 6000 rows
```

Each row is 12 bytes, so 6000 rows is 72 KB. That's 10 minutes at 10 Hz. When it's full, the oldest row gets overwritten. It lives in RAM, so it's gone when power is cut.

v1 stored the log as one giant text string. The ring buffer uses fixed memory and can't fragment or run out halfway through a test.

## 5. The dashboard (HTML)

The whole web page is stored in `INDEX_HTML` as a raw string:

```cpp
const char INDEX_HTML[] PROGMEM = R"HTMLEND( ... )HTMLEND";
```

`PROGMEM` keeps it in flash instead of RAM. `R"HTMLEND(...)HTMLEND"` is a raw string literal, so quotes and slashes inside the HTML don't need escaping.

### Styling

Colors are CSS variables at the top: navy background, red for weight, orange-yellow for airspeed, orange for peaks and the Tare button. Change them there to restyle everything.

### Polling

```js
setInterval(poll,250);
```

Four times a second, the browser fetches `/sample`, updates the cards, adds a point to the graph, and redraws. The graph keeps the last 600 points (about 2.5 minutes on screen). The full log on the ESP32 is longer.

### The graph

Drawn by hand on an HTML canvas, no libraries, because the hotspot has no internet to load them from. Two separate y-scales share one plot:

```js
function ymT(v){ ... return PT+(YMX-v)*h/YMX; }   // weight, left axis
function ymS(v){ ... return PT+(SMX-v)*h/SMX; }   // airspeed, right axis
```

The gridlines line up with both axes: 8 divisions of `YMX` (16 lb) and `SMX` (160 ft/s). The airspeed line only draws when the pitot is detected.

### Stopwatch

Runs in the browser. It starts on its own when weight goes over 0.25 lb, and has manual Pause and Reset. Tare and Clear reset it.

### Excel export

The browser builds a real `.xlsx` file itself, no internet or libraries needed. An `.xlsx` is a zip file of a few XML files, so the code:

1. Fetches `/csv` from the ESP32
2. Writes the sheet XML with headers **Time (s)**, **Weight (lb)**, **Airspeed (ft/s)** and a frozen top row
3. Packs the XML files into a zip with `makeZip()` (store mode, with a CRC32 checksum per file)
4. Downloads it as `apprentice_stand_v3_YYYYMMDD_HHMM.xlsx`

## 6. Web handlers

| URL | Handler | What it does |
| --- | --- | --- |
| `/` | `handleRoot()` | Sends the dashboard page |
| `/sample` | `handleSample()` | Sends the latest readings as JSON |
| `/csv` | `handleCSV()` | Streams the full log as CSV |
| `/clear` | `handleClear()` | Empties the log |
| `/log?enabled=0` or `1` | `handleLogToggle()` | Pauses or resumes logging |
| `/tare` | `handleTare()` | Re-tares the load cell (20 readings) and restarts the clock |
| `/zero` | `handleZero()` | Re-zeros and re-detects the pitot |

`/sample` returns something like:

```json
{"time_s":12.34,"thrust_lb":3.512,"thrust_lb_raw":3.512,"counts":687412,
 "airspeed_fps":42.10,"pitot_ok":true,"log_rows":123}
```

`handleCSV()` sends the file in about 1 KB chunks instead of building it all at once, so even a full 6000-row log doesn't run the ESP32 out of memory.

## 7. setup()

1. Starts Serial at 115200
2. Starts the HX711 at gain 128 and tares with 30 readings (keep the cell unloaded)
3. Starts I2C on D21/D22 at 100 kHz and zeros the pitot (keep the tip covered)
4. Starts the WiFi hotspot and registers the web handlers
5. Prints the WiFi name and IP to Serial Monitor

## 8. loop()

```cpp
server.handleClient();          // answer browser requests
updateLoadCell();               // new HX711 sample, if ready
updatePitot();                  // every 20 ms (~50 Hz)
logRecord();                    // every 100 ms, if logging is on
Serial.printf(...);             // status line every 1 s
```

Nothing in the loop blocks, so the website stays responsive while both sensors keep reading.

## Calibration.ino

Prints the average of 20 raw HX711 readings once a second. No tare is applied to the printed number, so you subtract the zero yourself in the formula. See [CALIBRATION_GUIDE.md](CALIBRATION_GUIDE.md).

## pitot_test.ino

A bare-bones check with no website or load cell:

1. Scans the I2C bus and lists everything it finds (the pitot should be at 0x28)
2. Prints status, raw counts, and pressure in Pa 5 times a second

Use it when the dashboard says **Pitot: NOT FOUND** or airspeed isn't moving.
