# ESP32 HVAC Filter Health Monitor

Monitors how clogged an HVAC filter is, using one BMP280 pressure sensor and an ESP32. The ESP32 reports status on a web dashboard. It does not control the fan; switching it is manual.

> Status: the sketches have not all been tested on hardware yet. Calibrate the thresholds against your own filter before relying on them.

## How it works

A dirty filter resists airflow, so the air pressure after it drops further below room pressure.

1. With the fan **off**, the ESP32 averages 60 readings and stores them as the room-pressure **baseline**.
2. With the fan **on**, the sensor sits on a tube just downstream of the filter on the fan's suction side.
3. **ΔP = baseline - current pressure.** A higher ΔP means a dirtier filter.

| ΔP | Status | Action |
|---|---|---|
| below 400 Pa | CLEAN | Normal operation |
| 400 to 500 Pa | WARNING | Inspect the filter |
| 500 Pa and above | CRITICAL | Switch the fan off and inspect the filter |

A 20 Pa hysteresis stops the status flickering at a threshold. If your sensor is on the pressure side of the fan, set `SENSOR_BELOW_AMBIENT = false`.

## Repository layout

| Path | Purpose |
|---|---|
| `firmware/i2c_checker` | Confirms the sensor and wiring work. Run this first. |
| `firmware/bmp280_single_simple` | Serial-only demo of the baseline and ΔP idea |
| `firmware/hvac_monitor_demo` | Full firmware with WiFi dashboard and a Demo mode switch |
| `hardware/` | Parts list and wiring |
| `docs/` | Screenshots (add yours) |

## Hardware

See [hardware/parts-list.md](hardware/parts-list.md) and [hardware/wiring.md](hardware/wiring.md). Default I2C pins are **SDA = GPIO25** and **SCL = GPIO26**, sensor address **0x76**.

## Software setup

1. Install the Arduino IDE and add ESP32 board support. Select **ESP32 Dev Module**.
2. Install these libraries from the Library Manager:
   - Adafruit BMP280 Library
   - Adafruit BusIO
   - Adafruit Unified Sensor
3. Open `firmware/i2c_checker`, upload it, and open the Serial Monitor at 115200 baud. You should see `0x76 -> chip ID 0x58 (BMP280 OK)`.
4. In `firmware/hvac_monitor_demo`, copy `secrets.example.h` to `secrets.h` and enter your WiFi name and password.
5. Make sure the fan is **off**, then upload `hvac_monitor_demo`.
6. Open the IP address printed in the Serial Monitor in a browser on the same WiFi.

## Dashboard

- Differential pressure, downstream pressure, baseline and its age, status badge and a live chart.
- **Re-tare** button: stores a new baseline. Press it only with the fan off.
- **Demo mode** checkbox: replaces the real readings with simulated dusty values (WARNING and CRITICAL range) so the dashboard can be shown without airflow. A banner appears while it is on.
- JSON data at `/api/data`.

Add a screenshot to `docs/` and link it here.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| "Sensor NOT found" | Wiring, power, loose header pins, or a dead module. Run `i2c_checker`. |
| Small negative ΔP with no airflow | Normal noise or drift. Re-tare with the fan off. |
| Large negative ΔP with the fan on | Tap is on the pressure side. Move it, or set `SENSOR_BELOW_AMBIENT = false`. |
| ΔP creeps over hours | Weather changes room pressure. Re-tare with the fan off. |
| WiFi drops | Use a stable 5 V USB supply. |

## Limitations

- One absolute sensor cannot measure a true difference at the same moment. Weather moves room pressure by roughly 100-300 Pa a day, which is large next to the thresholds. Re-tare regularly.
- The BMP280 has about 1-2 Pa of noise.
- Thresholds are defaults, not calibrated values.

## Possible upgrades

- A differential pressure sensor (for example Sensirion SDP810), which removes the baseline drift.
- Logging to flash, SD card or MQTT.
- Alerts by Telegram or email at CRITICAL.
- A relay for automatic fan cutoff.

## Credits

Uses the Adafruit BMP280, BusIO and Unified Sensor libraries.

## License

MIT, see [LICENSE](LICENSE). Replace `YOUR NAME` in the license before publishing.
