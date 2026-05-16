#include "dfrobot_air_quality_sensor.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "DFROBOT_AQS";

#define DFROBOT_AQS_REG_VERSION 0x1D
#define DFROBOT_AQS_REG_MODE    0x01
#define DFROBOT_AQS_MODE_SLEEP  0x01
#define DFROBOT_AQS_MODE_WAKE   0x02
#define I2C_TIMEOUT_MS          100

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_i2c_dev;

static esp_err_t dfrobot_aqs_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_i2c_dev != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C device not initialized");
    return i2c_master_transmit_receive(s_i2c_dev, &reg, 1, data, len, I2C_TIMEOUT_MS);
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
    ESP_RETURN_ON_FALSE(s_i2c_bus == NULL && s_i2c_dev == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "already initialized");

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

    return ret;
}

esp_err_t dfrobot_aqs_probe(void)
{
    uint8_t version;
    return dfrobot_aqs_get_version(&version);
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
