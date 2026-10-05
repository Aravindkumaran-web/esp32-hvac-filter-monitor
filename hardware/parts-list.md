# Parts list

| Qty | Part | Notes |
|---|---|---|
| 1 | ESP32 DevKit (30 or 38 pin) | Any ESP32 Dev Module works |
| 1 | BMP280 breakout module | Test it with `i2c_checker` before building. Buy a spare. |
| 4-6 | Jumper wires | Short (under about 20 cm). Solder the header pins for the final build. |
| 1 | Breadboard or perfboard | |
| 1 | Silicone tubing (small bore) | From the duct tap to the sensor |
| 1 | Small sealed chamber or enclosure | Holds the sensor, tube enters through a sealed fitting |
| 1 | 5 V USB supply, 1 A or more | A weak laptop port can cause WiFi brownouts |

## Optional
- A second BMP280 (address 0x77) for a true two-point reading. The older `hvac_monitor` design used two sensors.
- A differential pressure sensor such as the Sensirion SDP810, which removes the baseline drift problem.
