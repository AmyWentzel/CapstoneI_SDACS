#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "audio_input.h"
#include "battery_leds.h"
#include "ble_locator.h"
#include "command_dispatcher.h"
#include "config_store.h"
#include "device_state.h"
#include "fft_metrics.h"
#include "fuel_gauge.h"
#include "network_provisioning.h"
#include "node_identity.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "shared_i2c_bus.h"
#include "temp_humidity.h"
#include "time_sync.h"
#include "wifi_mqtt.h"

static const char *TAG = "app_main";
static run_storage_t s_storage = {0};

static void log_identity_summary(void)
{
    char nvs_node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1] = {0};
    char ble_name[32] = {0};
    char mqtt_base[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1] = {0};

    ESP_LOGI(TAG, "Compiled Node ID: %s", SDACS_NODE_ID);
    ESP_LOGI(TAG, "Active Node ID: %s", sdacs_node_id());

    if (config_store_peek_deprecated_node_id(nvs_node_id, sizeof(nvs_node_id)) == ESP_OK &&
        nvs_node_id[0] != '\0') {
        ESP_LOGW(TAG,
                 "Deprecated NVS node_id found but ignored. Active Node ID comes from firmware build.");
        ESP_LOGW(TAG, "NVS Node ID: %s (ignored/deprecated)", nvs_node_id);
    } else {
        ESP_LOGI(TAG, "NVS Node ID: ignored/deprecated if present");
    }

    if (sdacs_get_ble_name(ble_name, sizeof(ble_name)) == ESP_OK) {
        ESP_LOGI(TAG, "BLE name: %s", ble_name);
    }
    if (sdacs_get_mqtt_base(mqtt_base, sizeof(mqtt_base)) == ESP_OK) {
        ESP_LOGI(TAG, "MQTT base: %s", mqtt_base);
    }
}

void app_main(void)
{
    char base_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1] = {0};
    const char *node_id = sdacs_node_id();
    shared_i2c_bus_config_t i2c_cfg = {
        .port = SDACS_TEMP_HUMIDITY_I2C_PORT,
        .sda_gpio = SDACS_TEMP_HUMIDITY_SDA_GPIO,
        .scl_gpio = SDACS_TEMP_HUMIDITY_SCL_GPIO,
        .freq_hz = SDACS_TEMP_HUMIDITY_FREQ_HZ,
    };

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());
    log_identity_summary();
    if (!sdacs_node_id_is_valid(node_id)) {
        ESP_LOGE(TAG, "Invalid compiled Node ID: %s", node_id ? node_id : "(null)");
        return;
    }

#if SDACS_BLE_LOCATOR_ENABLED
    ESP_LOGI(TAG, "PHASE 0: BLE node-awareness advertisement");
    esp_err_t ble_err = sdacs_ble_locator_advertise_for(
        node_id,
        SDACS_BLE_LOCATOR_DURATION_MS
    );
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "BLE locator phase failed: %s; continuing to Wi-Fi/MQTT",
                 esp_err_to_name(ble_err));
    }
#endif

    ESP_LOGI(TAG, "PHASE 1: Wi-Fi/MQTT startup");
    ESP_ERROR_CHECK(wifi_mqtt_start(NULL));

    time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);

    esp_err_t storage_err = run_storage_init(&s_storage);
    if (storage_err != ESP_OK) {
        ESP_LOGE(TAG,
                 "SD storage unavailable: %s. Node will stay online, but capture commands will be rejected.",
                 esp_err_to_name(storage_err));
    }
    ESP_ERROR_CHECK(shared_i2c_bus_init(&i2c_cfg));
    ESP_ERROR_CHECK(battery_leds_init());

    if (!temp_humidity_start(
            SDACS_TEMP_HUMIDITY_I2C_PORT,
            SDACS_TEMP_HUMIDITY_ADDR,
            SDACS_TEMP_HUMIDITY_PERIOD_MS)) {
        ESP_LOGW(TAG, "SHT41 temp/humidity task failed to start; continuing without periodic T/RH");
    }

#if SDACS_FUEL_GAUGE_ENABLED
    if (!fuel_gauge_start(
            SDACS_FUEL_GAUGE_I2C_PORT,
            SDACS_FUEL_GAUGE_ADDR,
            SDACS_FUEL_GAUGE_PERIOD_MS)) {
        ESP_LOGW(TAG, "MAX17048 fuel gauge task failed to start; continuing without periodic battery telemetry");
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(250));
    if (wifi_mqtt_wait_connected(1000) == ESP_OK) {
        (void)temp_humidity_publish_latest_once("boot");
#if SDACS_FUEL_GAUGE_ENABLED
        (void)fuel_gauge_publish_latest_once("boot");
#endif
    } else {
        ESP_LOGW(TAG, "MQTT not ready for boot sensor snapshot publish");
    }

    ESP_ERROR_CHECK(audio_input_init());
    ESP_ERROR_CHECK(fft_metrics_init());
    device_state_init();
    ESP_ERROR_CHECK(sdacs_get_mqtt_base(base_topic, sizeof(base_topic)));
    command_dispatcher_init(&s_storage, node_id, base_topic);
    ESP_ERROR_CHECK(wifi_mqtt_set_command_callback(command_dispatcher_handle));

    ESP_LOGI(TAG, "Node ready in IDLE mode; waiting for MQTT start_capture command");
}
