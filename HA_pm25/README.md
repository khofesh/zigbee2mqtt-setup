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
