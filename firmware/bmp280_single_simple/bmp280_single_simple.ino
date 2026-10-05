/*
  Single BMP280 - serial only, no WiFi, no HTML.
  Wiring: 3V3->VCC, GND->GND, GPIO25->SDA, GPIO26->SCL, SDO->GND (0x76)
  Serial Monitor: 115200

  1) Power on with the fan OFF -> it stores the baseline.
  2) Turn the fan ON -> deltaP = baseline - current pressure.
  3) Type 't' in the Serial Monitor to re-take the baseline (fan OFF).
*/
#include <Wire.h>
#include <Adafruit_BMP280.h>

const int SDA_PIN = 25;   // change these two numbers to use other pins
const int SCL_PIN = 26;

Adafruit_BMP280 bmp;
float baseline = 0;

float averagePressure(int n) {
  float sum = 0;
  for (int i = 0; i < n; i++) { sum += bmp.readPressure(); delay(40); }
  return sum / n;
}

void takeBaseline() {
  Serial.println("Taking baseline (fan must be OFF)...");
  baseline = averagePressure(60);
  Serial.printf("Baseline = %.1f Pa\n", baseline);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);

  bool ok = false;
  const uint8_t ids[] = {0x58, 0x60, 0x56, 0x57};
  for (uint8_t id : ids) {
    if (bmp.begin(0x76, id)) { ok = true; break; }
  }
  if (!ok) {
    Serial.printf("Sensor NOT found at 0x76. Check wiring (3V3, GND, SDA=%d, SCL=%d, SDO=GND).\n", SDA_PIN, SCL_PIN);
    while (true) delay(1000);
  }

  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_63);
  Serial.println("Sensor found.");
  takeBaseline();
}

void loop() {
  if (Serial.available() && Serial.read() == 't') takeBaseline();

  float p = averagePressure(10);        // ~0.4 s average
  float deltaP = baseline - p;          // sensor downstream of filter, fan suction side

  const char* status = deltaP >= 500 ? "CRITICAL" : deltaP >= 400 ? "WARNING" : "CLEAN";
  Serial.printf("P=%.1f Pa | Baseline=%.1f Pa | DeltaP=%.1f Pa | %s\n", p, baseline, deltaP, status);
}
