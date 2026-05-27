#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "audio_input.h"
#include "ble_locator.h"
#include "command_dispatcher.h"
#include "config_store.h"
#include "device_state.h"
#include "fft_metrics.h"
#include "network_provisioning.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "shared_i2c_bus.h"
#include "time_sync.h"
#include "wifi_mqtt.h"

static const char *TAG = "app_main";
static run_storage_t s_storage = {0};

static void load_base_topic(char *out, size_t out_sz)
{
    const char *broker_uri = NULL;
    const char *configured_topic = NULL;
    const char *suffixes[] = {
        "/status/heartbeat",
        "/status",
        "/features",
        "/audio",
        "/temp_humidity",
    };
    size_t i = 0;

    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    if (config_store_get_mqtt(&broker_uri, &configured_topic) != ESP_OK) {
        return;
    }
    (void)broker_uri;

    if (!configured_topic || configured_topic[0] == '\0') {
        return;
    }

    strncpy(out, configured_topic, out_sz - 1);
    out[out_sz - 1] = '\0';

    for (i = 0; i < (sizeof(suffixes) / sizeof(suffixes[0])); ++i) {
        size_t topic_len = strlen(out);
        size_t suffix_len = strlen(suffixes[i]);

        if (topic_len >= suffix_len &&
            strcmp(out + topic_len - suffix_len, suffixes[i]) == 0) {
            out[topic_len - suffix_len] = '\0';
            break;
        }
    }
}

void app_main(void)
{
    char base_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1] = {0};
    const char *configured_node_id = NULL;
    const char *node_id = SDACS_NODE_ID;
    shared_i2c_bus_config_t i2c_cfg = {
        .port = SDACS_TEMP_HUMIDITY_I2C_PORT,
        .sda_gpio = SDACS_TEMP_HUMIDITY_SDA_GPIO,
        .scl_gpio = SDACS_TEMP_HUMIDITY_SCL_GPIO,
        .freq_hz = SDACS_TEMP_HUMIDITY_FREQ_HZ,
    };

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());
    if (config_store_get_node_id(&configured_node_id) == ESP_OK &&
        configured_node_id && configured_node_id[0] != '\0') {
        node_id = configured_node_id;
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

    ESP_ERROR_CHECK(run_storage_init(&s_storage));
    ESP_ERROR_CHECK(shared_i2c_bus_init(&i2c_cfg));

    ESP_ERROR_CHECK(audio_input_init());
    ESP_ERROR_CHECK(fft_metrics_init());
    device_state_init();
    load_base_topic(base_topic, sizeof(base_topic));
    command_dispatcher_init(&s_storage, node_id, base_topic);
    ESP_ERROR_CHECK(wifi_mqtt_set_command_callback(command_dispatcher_handle));

    ESP_LOGI(TAG, "Node ready in IDLE mode; waiting for MQTT start_capture command");
}
