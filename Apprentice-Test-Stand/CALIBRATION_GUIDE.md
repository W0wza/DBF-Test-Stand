# Calibration Guide

There are two sensors to set up:

1. **Load cell:** needs a real calibration with a known weight. You get one number, `counts_per_lb`, and paste it into the main sketch.
2. **Pitot sensor:** factory calibrated. It only needs a wiring check and a zero with no airflow.

## Part 1: Load cell calibration

### What you need

- The stand fully assembled with the load cell mounted the way it will be used
- A known weight between 2 and 10 lb, weighed on a trusted scale
- String and a pulley if the cell is mounted horizontally
- A spreadsheet (Excel, Google Sheets, Numbers) to average readings

### Step 1: Upload the calibration sketch

1. Open `Calibration.ino` in Arduino IDE
2. Select **Tools → Board → ESP32 Dev Module** and your port
3. Upload. Wait for **"Hash of data verified"** at the bottom of the output
4. Open Serial Monitor (magnifying glass icon, top right) and set the baud to **115200**
5. Press the **EN** button on the ESP32 to restart it

You should see the calibration banner, then one number printed every second. These are raw HX711 counts. They are often negative with no load. That's normal, the zero gets subtracted out.

### Step 2: Let it warm up

Leave it running with nothing on the load cell for **5 to 10 minutes**. At first the numbers creep steadily in one direction. Wait until they just bounce around within about ±25 counts with no steady climb.

Example of still drifting (don't use these):
```
-19163
-19128
-19114
-19103
-19085
-19063
```

Example of settled (good to use):
```
-18129
-18114
-18092
-18117
-18105
-18139
```

### Step 3: Record the zero

With **no load**, copy 12 to 20 readings into your spreadsheet and average them. This is `zero_counts`.

### Step 4: Record the loaded value

1. Hang your known weight on the load cell in the same direction the load will act during tests
2. Wait a few seconds for it to settle
3. Copy 12 to 20 readings and average them. This is `loaded_counts`

### Step 5: Calculate

```
counts_per_lb = (loaded_counts - zero_counts) / weight_lb
```

Real example from this stand (Oct 2026):

| Value | Number |
| --- | --- |
| zero_counts | -18,113 |
| loaded_counts | 2,007,518 |
| weight_lb | 10.08 |
| difference | 2,025,632 |
| **counts_per_lb** | **200,955.5** |

Noise was about ±190 counts with the weight on, which is about ±0.001 lb.

### Step 6: Put it in the main sketch

In `ApprenticeStand_v3.ino`:

```cpp
float counts_per_lb = 200955.5;   // use YOUR number
```

If your number comes out **negative**, keep the minus sign. It just means the cell is wired or loaded opposite to the pull direction.

### Step 7: Verify

1. Upload `ApprenticeStand_v3.ino`
2. Connect to **ApprenticeStand** and open **192.168.4.1**
3. Press **Tare Weight** with nothing on the cell
4. Hang the known weight. It should read within about 1% (10.08 lb should read about 9.98 to 10.18)
5. Optional: try a second, different weight to check linearity

If it's off by a constant ratio, redo Steps 3 to 5 with more readings.

## Part 2: Pitot sensor setup

### Step 1: Voltage check (before connecting SDA and SCL)

The ESP32's pins are 3.3V only. The pitot sensor runs on 5V, and some boards pull SDA/SCL up to 5V.

1. Connect only the sensor's **5V** to **VIN** and **GND** to **GND**
2. Plug in USB
3. Meter on DC volts: black probe on GND, red probe on the SDA pad, then the SCL pad

| Reading | What to do |
| --- | --- |
| 0V or about 3.3V | Wire SDA to D21 and SCL to D22 directly |
| About 5V | Don't wire directly. Use a BSS138 level shifter: HV to 5V, LV to 3V3, GND shared, HV1/HV2 to sensor SDA/SCL, LV1/LV2 to D21/D22 |

### Step 2: Test with pitot_test.ino

1. Upload `pitot_test.ino`
2. Open Serial Monitor at **115200** and press **EN**
3. You should see:

```
--- Pitot test ---
Scanning I2C...
  device at 0x28
status 0 | counts  1638 | dp      0.4 Pa
status 0 | counts  1640 | dp      1.3 Pa
```

4. Blow into the pitot tip. `dp` should jump clearly **positive** (tens to hundreds of Pa)

| What you see | Fix |
| --- | --- |
| `dp` goes positive | Working, move on |
| `dp` goes negative | Swap the two silicone tubes on the sensor |
| `dp` doesn't change | Tube kinked, blocked, or popped off |
| `NOTHING FOUND` or `no response from 0x28` | Swap the D21 and D22 wires, check 5V at the sensor, check solder joints |
| Serial Monitor is blank | Wrong baud or port, upload didn't finish, or a short on 5V. Unplug the pitot and press EN to check |

### Step 3: Zero

There's no weight calibration for the pitot. It just needs a zero with no airflow.

- **At boot:** cover the pitot tip while the ESP32 powers up. It zeros automatically.
- **Any time after:** cover the tip and press **Zero Airspeed** on the dashboard.

Re-zero if the temperature changes a lot (moving from inside to outside, for example).

### Step 4: Set air density

Airspeed depends on air density. Set `RHO` in `ApprenticeStand_v3.ino`:

| Situation | RHO (kg/m^3) |
| --- | --- |
| About 5,000 ft standard day (Prescott) | 1.056 |
| Sea level standard day, or indicated airspeed | 1.225 |
| Best accuracy | station pressure (Pa) / (287.05 × temperature (K)) |

### Step 5: Check on the dashboard

1. Upload `ApprenticeStand_v3.ino` with the tip covered
2. Status bar should say **Pitot: OK**
3. Give a firm puff or point a hair dryer (cool setting) or fan at the tip
4. Airspeed should pop up after it holds above 5 ft/s for 0.3 s

Speeds under about 8 ft/s show as 0 on purpose because of the noise deadband (`DP_DEADBAND_PA`).
