// Pitot raw test - MS4525DO on ESP32 (SDA 21, SCL 22)
// Serial Monitor 115200. Prints raw pressure 5x per second.
// Blow into the tip: dp should go clearly POSITIVE (tens to hundreds of Pa).
// If it goes NEGATIVE, swap the two silicone tubes on the sensor.

#include <Wire.h>
#define ADDR 0x28

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(21, 22);
  Wire.setClock(100000);

  Serial.println("\n--- Pitot test ---");
  Serial.println("Scanning I2C...");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  device at 0x%02X\n", a); found++; }
  }
  if (found == 0) Serial.println("  NOTHING FOUND -> check 5V/GND, swap SDA/SCL, check joints");
}

void loop() {
  if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)4) != 4) {
    Serial.println("no response from 0x28");
    delay(500);
    return;
  }
  uint8_t b0 = Wire.read(), b1 = Wire.read(); Wire.read(); Wire.read();
  uint8_t status = b0 >> 6;
  uint16_t p = ((uint16_t)(b0 & 0x3F) << 8) | b1;
  float psi = ((float)p - 0.1f * 16383.0f) * 2.0f / (0.8f * 16383.0f) - 1.0f;
  float pa = psi * 6894.757f;
  Serial.printf("status %u | counts %5u | dp %8.1f Pa\n", status, p, pa);
  delay(200);
}
