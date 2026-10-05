# Wiring (single BMP280)

Use 3V3 only. Never connect the sensor to 5V or VIN.

| ESP32 | BMP280 |
|---|---|
| 3V3 | VCC |
| GND | GND |
| GPIO25 | SDA |
| GPIO26 | SCL |
| GND | SDO (sets address 0x76) |
| 3V3 | CSB (if the pin is exposed, selects I2C mode) |

```
        ESP32                     BMP280
   +-------------+          +---------------+
   |         3V3 |----------| VCC           |
   |         GND |----+-----| GND           |
   |      GPIO25 |----|-----| SDA           |
   |      GPIO26 |----|-----| SCL           |
   |             |    +-----| SDO  (to GND) |
   +-------------+          +---------------+
```

If your pins differ, change `SDA_PIN` and `SCL_PIN` at the top of each sketch.
Avoid GPIO6-11 (flash), GPIO34-39 (input only) and the boot pins 0, 2, 5, 12, 15.

## Air connection

```
 room air --> [FILTER] --> [tap]--tube--[BMP280] ... --> [FAN] -->
```

- Drill the tap hole perpendicular to the airflow so it reads static pressure.
- Place the tap just after the filter, on the suction side of the fan.
- Seal every joint. A leak lowers the reading.
