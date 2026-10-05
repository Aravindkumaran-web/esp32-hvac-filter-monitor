/*
  I2C checker for BMP280 / BME280 modules.
  Checks both usual addresses (0x76, 0x77) and scans the whole bus.
  Serial Monitor: 115200 baud.

  Expected: "0x76 -> chip ID 0x58 (BMP280 OK)" for a module with SDO tied to GND,
            or the same at 0x77 for a module with SDO tied to 3V3.
*/
#include <Wire.h>

const int SDA_PIN = 25;   // change to match your wiring
const int SCL_PIN = 26;

uint8_t readChipID(uint8_t addr, bool &ok) {
  Wire.beginTransmission(addr);
  Wire.write(0xD0);                                   // chip ID register
  if (Wire.endTransmission(false) != 0) { ok = false; return 0; }
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) { ok = false; return 0; }
  ok = true;
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(100);
  Serial.printf("I2C checker started (SDA=GPIO%d, SCL=GPIO%d)\n", SDA_PIN, SCL_PIN);
}

void loop() {
  const uint8_t addrs[] = {0x76, 0x77};
  for (uint8_t a : addrs) {
    bool ok;
    uint8_t id = readChipID(a, ok);
    if (ok) {
      Serial.printf("0x%02X -> chip ID 0x%02X %s\n", a, id,
                    id == 0x58 ? "(BMP280 OK)" :
                    id == 0x60 ? "(BME280)" : "(unexpected)");
    } else {
      Serial.printf("0x%02X -> no response\n", a);
    }
  }

  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Bus scan: device at 0x%02X\n", a);
      found++;
    }
  }
  if (found == 0) Serial.println("Bus scan: nothing found");
  Serial.println("-----");
  delay(2000);
}
