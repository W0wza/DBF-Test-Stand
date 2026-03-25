// ── Calibration sketch for ESP32 + SparkFun HX711 + load cell ──────────────
//
// PURPOSE:
//   This sketch prints raw HX711 counts to Serial Monitor so you can
//   determine your calibration constant (counts_per_lb).
//   Run this BEFORE uploading ThrustStand.ino.
//
// HOW TO USE:
//   1. Upload this sketch to the ESP32.
//   2. Open Serial Monitor at 115200 baud.
//   3. With nothing on the load cell, record the zero reading.
//   4. Hang a known weight, let it settle, collect 20+ readings and average them.
//   5. Use the formula: counts_per_lb = (loaded_avg - zero_avg) / weight_lb
//   6. Put that number into ThrustStand.ino as counts_per_lb.
//
// See CALIBRATION_GUIDE.md for the full step by step instructions.
// ───────────────────────────────────────────────────────────────────────────

#include "HX711.h"

// ── Pin definitions — must match your physical wiring ───────────────────────
const int HX_DT  = 4;   // HX711 DAT pin → ESP32 GPIO 4
const int HX_SCK = 5;   // HX711 CLK pin → ESP32 GPIO 5

HX711 scale;

void setup() {
  Serial.begin(115200);

  // initialise the HX711
  scale.begin(HX_DT, HX_SCK);

  // set gain to 128 on channel A — must match ThrustStand.ino
  scale.set_gain(128);

  // wait for the HX711 to power up and stabilise
  delay(1000);

  // tare with 30 readings to establish a stable zero
  scale.tare(30);

  Serial.println("==============================================");
  Serial.println("  HX711 CALIBRATION MODE");
  Serial.println("==============================================");
  Serial.println("Step 1: With NO weight on the load cell,");
  Serial.println("        record the stable number below.");
  Serial.println("        This is your zero_counts.");
  Serial.println("");
  Serial.println("Step 2: Hang your known weight and record");
  Serial.println("        at least 20 readings. Average them.");
  Serial.println("        This is your loaded_counts.");
  Serial.println("");
  Serial.println("Formula:");
  Serial.println("  counts_per_lb = (loaded_counts - zero_counts) / weight_lb");
  Serial.println("==============================================");
  Serial.println("");
}

void loop() {
  // read_average(20): take 20 readings and print their average
  // this gives a stable number without too much noise
  long raw = scale.read_average(20);

  Serial.println(raw);

  // print once per second — enough to collect steady readings
  delay(1000);
}
```

---

That's all four files. In your GitHub repo create:
```
/README.md
/ThrustStand.ino
/Calibration.ino
/CALIBRATION_GUIDE.md
/CODE_EXPLAINED.md
