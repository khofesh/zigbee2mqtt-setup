# ESP32-C6-DEV-KIT-N8 and dfrobot pm2.5 air quality sensor

- https://docs.waveshare.com/ESP32-C6-DEV-KIT-N8
- https://wiki.dfrobot.com/sen0460/

## Wiring

DFRobot Gravity PM2.5 sensor I2C:

- `SDA` -> ESP32-C6 `GPIO6`
- `SCL` -> ESP32-C6 `GPIO7`
- `GND` -> `GND`
- `VCC` -> sensor supply per the DFRobot board documentation

GPIO6/GPIO7 are LP-capable pins on the Waveshare ESP32-C6 board. The app reads
I2C while awake, then puts the PM2.5 sensor and Zigbee end device back into
low-power operation between one-minute reports.

## Build

```sh
env IDF_TARGET=esp32c6 idf.py -B build_esp32c6 build
```

Flash with your serial port:

```sh
env IDF_TARGET=esp32c6 idf.py -B build_esp32c6 -p /dev/ttyACM0 flash monitor
```

failed to wake up

```
I (5718) DFROBOT_AQS: Scanning I2C bus...
W (5728) DFROBOT_AQS: No I2C devices detected
W (5768) ZB_PM25_SENSOR: initial sensor wake failed (ESP_ERR_INVALID_RESPONSE), trying address probe
E (5768) ZB_PM25_SENSOR: sensor_init_once(125): sensor address probe failed, check wiring and address 0x19
I (10768) DFROBOT_AQS: Scanning I2C bus...
W (10778) DFROBOT_AQS: No I2C devices detected
W (10778) ZB_PM25_SENSOR: initial sensor wake failed (ESP_ERR_INVALID_RESPONSE), trying address probe
E (10778) ZB_PM25_SENSOR: sensor_init_once(125): sensor address probe failed, check wiring and address 0x19
I (15788) DFROBOT_AQS: Scanning I2C bus...
I (15788) DFROBOT_AQS: I2C device detected at 0x19 (configured PM2.5 address)
I (20798) ZB_PM25_SENSOR: DFRobot PM2.5 sensor firmware version: 32
E (25898) DFROBOT_AQS: dfrobot_aqs_read_concentration_ugm3(150): read concentration failed
E (25998) DFROBOT_AQS: dfrobot_aqs_read_concentration_ugm3(150): read concentration failed
E (26098) DFROBOT_AQS: dfrobot_aqs_read_concentration_ugm3(150): read concentration failed
W (26198) ZB_PM25_SENSOR: sensor low-power command failed
E (26198) ZB_PM25_SENSOR: PM concentration read failed
W (75888) ZB_PM25_SENSOR: sensor wake failed
W (80988) ZB_PM25_SENSOR: sensor wake failed
W (86088) ZB_PM25_SENSOR: sensor wake failed
W (91188) ZB_PM25_SENSOR: sensor wake failed
W (96288) ZB_PM25_SENSOR: sensor wake failed
W (101388) ZB_PM25_SENSOR: sensor wake failed
```
