# ESP32-C6-DEV-KIT-N8 and DFRobot PM2.5 air quality sensor

- Waveshare board docs: https://docs.waveshare.com/ESP32-C6-DEV-KIT-N8
- DFRobot sensor docs: https://wiki.dfrobot.com/sen0460/

## Status

Working with:

- Waveshare ESP32-C6-DEV-KIT-N8
- DFRobot Gravity PM2.5 Air Quality Sensor / SEN0460
- ESP-IDF target `esp32c6`
- I2C address `0x19`
- I2C pins `SDA GPIO22`, `SCL GPIO23`
- Sensor low-power mode disabled

The sensor failed to run reliably on the initial LP-capable GPIO6/GPIO7 wiring because SCL was held low. GPIO22/GPIO23 are the current working pins.

## Wiring

DFRobot Gravity PM2.5 sensor I2C:

- `SDA` -> ESP32-C6 `GPIO22`
- `SCL` -> ESP32-C6 `GPIO23`
- `GND` -> `GND`
- `VCC` -> sensor supply per the DFRobot board documentation

The firmware enables internal pull-ups, but external I2C pull-ups are still recommended for reliability.

## Zigbee

The firmware registers one Zigbee end device with three measurement values:

- Endpoint `1`: standard PM2.5 Measurement cluster, PM2.5 value
- Endpoint `2`: Analog Input cluster, PM1.0 value
- Endpoint `3`: Analog Input cluster, PM10 value

The sensor is read every 60 seconds. Logs should look like:

```text
DFROBOT_AQS: I2C device detected at 0x19 (configured PM2.5 address)
ZB_PM25_SENSOR: DFRobot PM2.5 sensor firmware version: 32
ZB_PM25_SENSOR: PM1.0: 55 ug/m3  PM2.5: 95 ug/m3  PM10: 103 ug/m3
```

## Low Power

ESP/Zigbee end-device sleep is enabled, but the DFRobot sensor low-power command is disabled:

```c
#define SENSOR_LOW_POWER_ENABLE 0
```

The sensor accepted the low-power command but did not wake reliably afterward. Keeping it awake is the current stable configuration.

## Build

```sh
env IDF_TARGET=esp32c6 idf.py -B build_esp32c6 build
```

Flash with your serial port:

```sh
env IDF_TARGET=esp32c6 idf.py -B build_esp32c6 -p /dev/ttyACM0 flash monitor
```
