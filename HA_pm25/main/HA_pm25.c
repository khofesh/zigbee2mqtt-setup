/*
 * Zigbee HA PM2.5 Sensor (End Device)
 * Target: Waveshare ESP32-C6-DEV-KIT-N8
 *
 * Sensor: DFRobot Gravity PM2.5 Air Quality Sensor (SEN0460)
 * I2C:    SDA GPIO6, SCL GPIO7 (LP-capable pins on the Waveshare board)
 *
 * The sensor and Zigbee stack sleep between samples. I2C transactions happen
 * only while the CPU is awake; the sensor is returned to its own low-power mode
 * after each report.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_types.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "zcl_utility.h"
#include "dfrobot_air_quality_sensor.h"

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig (Component config -> Zboss -> Zigbee End Device)
#endif

static const char *TAG = "ZB_PM25_SENSOR";

#define INSTALLCODE_POLICY_ENABLE      false
#define SENSOR_ENDPOINT                1
#define ESP_ZB_PRIMARY_CHANNEL_MASK    (1l << 11)

#define ESP_MANUFACTURER_NAME          "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER           "\x07"CONFIG_IDF_TARGET

#define PM25_I2C_PORT                  I2C_NUM_0
#define PM25_I2C_SDA_GPIO              6
#define PM25_I2C_SCL_GPIO              7
#define PM25_I2C_SPEED_HZ              100000

#define SENSOR_UPDATE_INTERVAL_MS      60000
#define SENSOR_WAKE_STABILIZE_MS       5000
#define ZIGBEE_SLEEP_THRESHOLD_MS      1000
/* Keep disabled during hardware bring-up so the sensor fan/laser stays on. */
#define SENSOR_LOW_POWER_ENABLE        0

#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                      \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zed_cfg = {                                        \
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,           \
            .keep_alive = 3000,                                     \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                               \
    {                                                               \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                        \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                                \
    {                                                               \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,      \
    }

static bool s_sensor_ready;
static bool s_reporting_configured;

static void configure_attribute_reporting(void)
{
    if (s_reporting_configured) {
        return;
    }

    esp_zb_zcl_reporting_info_t pm25_report = {
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .ep = SENSOR_ENDPOINT,
        .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_PM2_5_MEASUREMENT,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attr_id = ESP_ZB_ZCL_ATTR_PM2_5_MEASUREMENT_MEASURED_VALUE_ID,
        .u.send_info = {
            .min_interval = 10,
            .max_interval = 60,
            .delta.f32 = 1.0f,
        },
        .dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC,
    };
    esp_zb_zcl_update_reporting_info(&pm25_report);

    s_reporting_configured = true;
    ESP_LOGI(TAG, "PM2.5 attribute reporting configured, max interval 60 s");
}

static esp_err_t sensor_init_once(void)
{
    if (s_sensor_ready) {
        return ESP_OK;
    }

    const dfrobot_aqs_config_t cfg = {
        .i2c_port = PM25_I2C_PORT,
        .sda_gpio = PM25_I2C_SDA_GPIO,
        .scl_gpio = PM25_I2C_SCL_GPIO,
        .scl_speed_hz = PM25_I2C_SPEED_HZ,
        .i2c_addr = DFROBOT_AQS_DEFAULT_I2C_ADDR,
    };
    ESP_RETURN_ON_ERROR(dfrobot_aqs_init(&cfg), TAG, "sensor I2C init failed");
    dfrobot_aqs_scan_bus();

    /*
     * If a previous firmware run put the sensor into low-power mode, wake it
     * before doing register reads. The wake command itself is also an I2C ACK
     * check against the configured address.
     */
    esp_err_t wake_err = dfrobot_aqs_wake();
    if (wake_err != ESP_OK) {
        ESP_LOGW(TAG, "initial sensor wake failed (%s), trying address probe",
                 esp_err_to_name(wake_err));
    } else {
        vTaskDelay(pdMS_TO_TICKS(SENSOR_WAKE_STABILIZE_MS));
    }

    ESP_RETURN_ON_ERROR(dfrobot_aqs_probe(), TAG,
                        "sensor address probe failed, check wiring and address 0x19");

    uint8_t version = 0;
    esp_err_t version_err = dfrobot_aqs_get_version(&version);
    if (version_err == ESP_OK) {
        ESP_LOGI(TAG, "DFRobot PM2.5 sensor firmware version: %u", version);
    } else {
        ESP_LOGW(TAG, "DFRobot PM2.5 version read failed (%s), continuing after address ACK",
                 esp_err_to_name(version_err));
    }

    s_sensor_ready = true;
    return ESP_OK;
}

static void sensor_task(void *pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (sensor_init_once() != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            last_wake = xTaskGetTickCount();
            continue;
        }

        if (dfrobot_aqs_wake() != ESP_OK) {
            ESP_LOGW(TAG, "sensor wake failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            last_wake = xTaskGetTickCount();
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_WAKE_STABILIZE_MS));

        uint16_t pm1_0 = 0;
        uint16_t pm2_5 = 0;
        uint16_t pm10 = 0;
        esp_err_t err = dfrobot_aqs_read_concentration_ugm3(DFROBOT_AQS_PM1_0_ATMOSPHERE, &pm1_0);
        err |= dfrobot_aqs_read_concentration_ugm3(DFROBOT_AQS_PM2_5_ATMOSPHERE, &pm2_5);
        err |= dfrobot_aqs_read_concentration_ugm3(DFROBOT_AQS_PM10_ATMOSPHERE, &pm10);

#if SENSOR_LOW_POWER_ENABLE
        if (dfrobot_aqs_set_low_power() != ESP_OK) {
            ESP_LOGW(TAG, "sensor low-power command failed");
        }
#endif

        if (err == ESP_OK) {
            float pm2_5_zcl = (float)pm2_5;

            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_zcl_set_attribute_val(
                SENSOR_ENDPOINT,
                ESP_ZB_ZCL_CLUSTER_ID_PM2_5_MEASUREMENT,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_PM2_5_MEASUREMENT_MEASURED_VALUE_ID,
                &pm2_5_zcl,
                false);
            esp_zb_lock_release();

            ESP_LOGI(TAG, "PM1.0: %u ug/m3  PM2.5: %u ug/m3  PM10: %u ug/m3",
                     pm1_0, pm2_5, pm10);
        } else {
            ESP_LOGE(TAG, "PM concentration read failed");
            dfrobot_aqs_scan_bus();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_UPDATE_INTERVAL_MS));
    }
}

static void start_sensor_reporting(void)
{
    static bool started = false;
    if (started) {
        return;
    }

    started = true;
    configure_attribute_reporting();
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}

static void bdb_start_steering_cb(uint8_t param)
{
    esp_zb_bdb_start_top_level_commissioning(param);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack (%s), factory reset",
                     esp_err_to_name(err_status));
            esp_zb_factory_reset();
            break;
        }
        ESP_LOGI(TAG, "Device started (%s factory-reset mode)",
                 esp_zb_bdb_is_factory_new() ? "" : "non ");
        if (esp_zb_bdb_is_factory_new()) {
            ESP_LOGI(TAG, "Starting network steering...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "Rejoined saved network");
            start_sensor_reporting();
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t ext_pan_id;
            esp_zb_get_extended_pan_id(ext_pan_id);
            ESP_LOGI(TAG,
                     "Joined network: PAN ID 0x%04hx, channel %d, short addr 0x%04hx",
                     esp_zb_get_pan_id(),
                     esp_zb_get_current_channel(),
                     esp_zb_get_short_address());
            start_sensor_reporting();
        } else {
            ESP_LOGW(TAG, "Network steering failed (%s), retrying",
                     esp_err_to_name(err_status));
            esp_zb_scheduler_alarm(bdb_start_steering_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING,
                                   1000);
        }
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    (void)pvParameters;

    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = ESP_ZB_ZCL_BASIC_POWER_SOURCE_BATTERY,
    };
    esp_zb_cluster_list_add_basic_cluster(cluster_list,
                                          esp_zb_basic_cluster_create(&basic_cfg),
                                          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_identify_cluster_cfg_t identify_cfg = { .identify_time = 0 };
    esp_zb_cluster_list_add_identify_cluster(cluster_list,
                                             esp_zb_identify_cluster_create(&identify_cfg),
                                             ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_pm2_5_measurement_cluster_cfg_t pm25_cfg = {
        .measured_value = ESP_ZB_ZCL_PM2_5_MEASUREMENT_MEASURED_VALUE_DEFAULT,
        .min_measured_value = 0.0f,
        .max_measured_value = 1000.0f,
    };
    esp_zb_cluster_list_add_pm2_5_measurement_cluster(
        cluster_list,
        esp_zb_pm2_5_measurement_cluster_create(&pm25_cfg),
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_endpoint_config_t ep_config = {
        .endpoint = SENSOR_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_config);

    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = ESP_MANUFACTURER_NAME,
        .model_identifier = ESP_MODEL_IDENTIFIER,
    };
    esp_zcl_utility_add_ep_basic_manufacturer_info(ep_list, SENSOR_ENDPOINT, &info);

    esp_zb_device_register(ep_list);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_THRESHOLD_MS));
    esp_zb_sleep_enable(true);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
