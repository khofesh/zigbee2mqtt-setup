#include "dfrobot_air_quality_sensor.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DFROBOT_AQS";

#define DFROBOT_AQS_REG_VERSION 0x1D
#define DFROBOT_AQS_REG_MODE    0x01
#define DFROBOT_AQS_MODE_SLEEP  0x01
#define DFROBOT_AQS_MODE_WAKE   0x02
#define I2C_TIMEOUT_MS          100

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_i2c_dev;
static dfrobot_aqs_config_t s_i2c_cfg;
static bool s_i2c_cfg_valid;
static uint8_t s_i2c_addr;

static esp_err_t dfrobot_aqs_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_i2c_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C device not initialized");

    /*
     * Match the Arduino driver:
     *   beginTransmission(addr) -> write(reg) -> endTransmission()
     *   requestFrom(addr, len)
     *
     * Some small sensor MCUs do not like combined write/read transactions with
     * a repeated START, even when simple registers sometimes appear to work.
     */
    esp_err_t err = i2c_master_transmit(s_i2c_dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write register address 0x%02x failed: %s", reg, esp_err_to_name(err));
        return err;
    }

    err = i2c_master_receive(s_i2c_dev, data, len, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read register 0x%02x (%u bytes) failed: %s",
                 reg, (unsigned)len, esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t dfrobot_aqs_write_reg(uint8_t reg, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_i2c_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C device not initialized");
    ESP_RETURN_ON_FALSE(len <= 8, ESP_ERR_INVALID_ARG, TAG, "write too long");

    uint8_t buf[9] = { reg };
    for (size_t i = 0; i < len; i++) {
        buf[i + 1] = data[i];
    }

    return i2c_master_transmit(s_i2c_dev, buf, len + 1, I2C_TIMEOUT_MS);
}

esp_err_t dfrobot_aqs_init(const dfrobot_aqs_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");

    if (s_i2c_bus != NULL && s_i2c_dev != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(s_i2c_bus == NULL && s_i2c_dev == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "partially initialized");
    s_i2c_cfg = *config;
    s_i2c_cfg_valid = true;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = config->i2c_port,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "create I2C bus failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = config->scl_speed_hz,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return err;
    }
    s_i2c_addr = config->i2c_addr;

    ESP_LOGI(TAG, "I2C ready: addr 0x%02x, SDA GPIO%d, SCL GPIO%d, %lu Hz",
             config->i2c_addr, config->sda_gpio, config->scl_gpio,
             (unsigned long)config->scl_speed_hz);
    return ESP_OK;
}

esp_err_t dfrobot_aqs_deinit(void)
{
    esp_err_t ret = ESP_OK;

    if (s_i2c_dev != NULL) {
        ret = i2c_master_bus_rm_device(s_i2c_dev);
        s_i2c_dev = NULL;
    }
    if (s_i2c_bus != NULL) {
        esp_err_t err = i2c_del_master_bus(s_i2c_bus);
        if (ret == ESP_OK) {
            ret = err;
        }
        s_i2c_bus = NULL;
    }
    s_i2c_addr = 0;

    return ret;
}

static void dfrobot_aqs_log_bus_levels(const char *prefix)
{
    if (!s_i2c_cfg_valid) {
        return;
    }

    ESP_LOGW(TAG, "%s: SDA GPIO%d=%d, SCL GPIO%d=%d",
             prefix,
             s_i2c_cfg.sda_gpio, gpio_get_level(s_i2c_cfg.sda_gpio),
             s_i2c_cfg.scl_gpio, gpio_get_level(s_i2c_cfg.scl_gpio));
}

esp_err_t dfrobot_aqs_scan_bus(void)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");

    bool found = false;
    ESP_LOGI(TAG, "Scanning I2C bus around configured sensor address...");
    dfrobot_aqs_log_bus_levels("Before I2C scan");

    esp_err_t sensor_err = i2c_master_probe(s_i2c_bus, s_i2c_addr, I2C_TIMEOUT_MS);
    if (sensor_err == ESP_OK) {
        ESP_LOGI(TAG, "I2C device detected at 0x%02x (configured PM2.5 address)", s_i2c_addr);
        return ESP_OK;
    }
    if (sensor_err == ESP_ERR_TIMEOUT) {
        dfrobot_aqs_log_bus_levels("I2C probe timed out");
        return sensor_err;
    }

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (addr == s_i2c_addr) {
            continue;
        }
        esp_err_t err = i2c_master_probe(s_i2c_bus, addr, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C device detected at 0x%02x", addr);
            found = true;
        } else if (err == ESP_ERR_TIMEOUT) {
            dfrobot_aqs_log_bus_levels("I2C scan timed out");
            return err;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No I2C devices detected");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

esp_err_t dfrobot_aqs_reset_bus(void)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");
    ESP_LOGW(TAG, "Resetting I2C bus");
    return i2c_master_bus_reset(s_i2c_bus);
}

esp_err_t dfrobot_aqs_recover_bus(void)
{
    ESP_RETURN_ON_FALSE(s_i2c_cfg_valid, ESP_ERR_INVALID_STATE, TAG, "I2C config not initialized");

    ESP_LOGW(TAG, "Recovering I2C bus by clocking SCL manually");
    dfrobot_aqs_deinit();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_i2c_cfg.sda_gpio) | (1ULL << s_i2c_cfg.scl_gpio),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config for I2C recovery failed");

    gpio_set_level(s_i2c_cfg.sda_gpio, 1);
    gpio_set_level(s_i2c_cfg.scl_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    for (int i = 0; i < 9; i++) {
        gpio_set_level(s_i2c_cfg.scl_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(s_i2c_cfg.scl_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* Generate a STOP condition: SDA low while SCL high, then SDA high. */
    gpio_set_level(s_i2c_cfg.sda_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(s_i2c_cfg.scl_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(s_i2c_cfg.sda_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    dfrobot_aqs_log_bus_levels("After manual I2C recovery");
    return dfrobot_aqs_init(&s_i2c_cfg);
}

esp_err_t dfrobot_aqs_probe(void)
{
    ESP_RETURN_ON_FALSE(s_i2c_bus != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C bus not initialized");
    return i2c_master_probe(s_i2c_bus, s_i2c_addr, I2C_TIMEOUT_MS);
}

esp_err_t dfrobot_aqs_get_version(uint8_t *version)
{
    ESP_RETURN_ON_FALSE(version != NULL, ESP_ERR_INVALID_ARG, TAG, "version is NULL");
    return dfrobot_aqs_read_reg(DFROBOT_AQS_REG_VERSION, version, 1);
}

esp_err_t dfrobot_aqs_set_low_power(void)
{
    const uint8_t mode = DFROBOT_AQS_MODE_SLEEP;
    return dfrobot_aqs_write_reg(DFROBOT_AQS_REG_MODE, &mode, 1);
}

esp_err_t dfrobot_aqs_wake(void)
{
    const uint8_t mode = DFROBOT_AQS_MODE_WAKE;
    return dfrobot_aqs_write_reg(DFROBOT_AQS_REG_MODE, &mode, 1);
}

esp_err_t dfrobot_aqs_read_concentration_ugm3(dfrobot_aqs_pm_concentration_reg_t type,
                                              uint16_t *concentration)
{
    ESP_RETURN_ON_FALSE(concentration != NULL, ESP_ERR_INVALID_ARG, TAG, "concentration is NULL");

    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(dfrobot_aqs_read_reg((uint8_t)type, buf, sizeof(buf)), TAG,
                        "read concentration failed");
    *concentration = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}

esp_err_t dfrobot_aqs_read_particle_count(dfrobot_aqs_particle_count_reg_t type,
                                          uint16_t *count)
{
    ESP_RETURN_ON_FALSE(count != NULL, ESP_ERR_INVALID_ARG, TAG, "count is NULL");

    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(dfrobot_aqs_read_reg((uint8_t)type, buf, sizeof(buf)), TAG,
                        "read particle count failed");
    *count = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}
