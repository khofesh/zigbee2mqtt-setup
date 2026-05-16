/*
 * ESP-IDF driver for DFRobot Gravity PM2.5 Air Quality Sensor (SEN0460).
 *
 * The original Arduino driver exposes simple register reads over I2C. This
 * version keeps the same register map and uses ESP-IDF's i2c_master driver.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DFROBOT_AQS_DEFAULT_I2C_ADDR 0x19

typedef enum {
    DFROBOT_AQS_PM1_0_STANDARD = 0x05,
    DFROBOT_AQS_PM2_5_STANDARD = 0x07,
    DFROBOT_AQS_PM10_STANDARD = 0x09,
    DFROBOT_AQS_PM1_0_ATMOSPHERE = 0x0B,
    DFROBOT_AQS_PM2_5_ATMOSPHERE = 0x0D,
    DFROBOT_AQS_PM10_ATMOSPHERE = 0x0F,
} dfrobot_aqs_pm_concentration_reg_t;

typedef enum {
    DFROBOT_AQS_PARTICLES_0_3_UM_PER_0_1L = 0x11,
    DFROBOT_AQS_PARTICLES_0_5_UM_PER_0_1L = 0x13,
    DFROBOT_AQS_PARTICLES_1_0_UM_PER_0_1L = 0x15,
    DFROBOT_AQS_PARTICLES_2_5_UM_PER_0_1L = 0x17,
    DFROBOT_AQS_PARTICLES_5_0_UM_PER_0_1L = 0x19,
    DFROBOT_AQS_PARTICLES_10_UM_PER_0_1L = 0x1B,
} dfrobot_aqs_particle_count_reg_t;

typedef struct {
    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint32_t scl_speed_hz;
    uint8_t i2c_addr;
} dfrobot_aqs_config_t;

esp_err_t dfrobot_aqs_init(const dfrobot_aqs_config_t *config);
esp_err_t dfrobot_aqs_deinit(void);
esp_err_t dfrobot_aqs_scan_bus(void);
esp_err_t dfrobot_aqs_probe(void);
esp_err_t dfrobot_aqs_get_version(uint8_t *version);
esp_err_t dfrobot_aqs_set_low_power(void);
esp_err_t dfrobot_aqs_wake(void);
esp_err_t dfrobot_aqs_read_concentration_ugm3(dfrobot_aqs_pm_concentration_reg_t type,
                                              uint16_t *concentration);
esp_err_t dfrobot_aqs_read_particle_count(dfrobot_aqs_particle_count_reg_t type,
                                          uint16_t *count);

#ifdef __cplusplus
}
#endif
