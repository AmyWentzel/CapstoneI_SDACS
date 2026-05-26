#include <string.h>

#include "esp_log.h"

#include "audio_input.h"
#include "ble_locator.h"
#include "capture_task.h"
#include "command_dispatcher.h"
#include "config_store.h"
#include "device_state.h"
#include "fft_metrics.h"
#include "fuel_gauge.h"
#include "network_provisioning.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "shared_i2c_bus.h"
#include "temp_humidity.h"
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
    shared_i2c_bus_config_t i2c_cfg = {
        .port = SDACS_TEMP_HUMIDITY_I2C_PORT,
        .sda_gpio = SDACS_TEMP_HUMIDITY_SDA_GPIO,
        .scl_gpio = SDACS_TEMP_HUMIDITY_SCL_GPIO,
        .freq_hz = SDACS_TEMP_HUMIDITY_FREQ_HZ,
    };

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());
    ESP_ERROR_CHECK(wifi_mqtt_start(NULL));

    time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);

    ESP_ERROR_CHECK(run_storage_init(&s_storage));
    ESP_ERROR_CHECK(run_storage_create_session(&s_storage, SDACS_NODE_ID));
    ESP_ERROR_CHECK(shared_i2c_bus_init(&i2c_cfg));

    bool th_ok = temp_humidity_start(
        SDACS_TEMP_HUMIDITY_I2C_PORT,
        SDACS_TEMP_HUMIDITY_ADDR,
        SDACS_TEMP_HUMIDITY_PERIOD_MS
    );
    if (!th_ok) {
        ESP_LOGW(TAG, "temp_humidity_start failed; metrics will show NAN");
    }

#if SDACS_FUEL_GAUGE_ENABLED
    bool fg_ok = fuel_gauge_start(
        SDACS_FUEL_GAUGE_I2C_PORT,
        SDACS_FUEL_GAUGE_ADDR,
        SDACS_FUEL_GAUGE_PERIOD_MS
    );
    if (!fg_ok) {
        ESP_LOGE(TAG, "fuel_gauge_start failed; battery metrics unavailable");
    } else {
        ESP_LOGI(TAG, "Fuel gauge started on I2C port %d addr 0x%02X",
                 SDACS_FUEL_GAUGE_I2C_PORT,
                 SDACS_FUEL_GAUGE_ADDR);
    }
#endif

    ESP_ERROR_CHECK(audio_input_init());
    ESP_ERROR_CHECK(fft_metrics_init());
    device_state_init();
    load_base_topic(base_topic, sizeof(base_topic));
    command_dispatcher_init(&s_storage, SDACS_NODE_ID, base_topic);
    ESP_ERROR_CHECK(wifi_mqtt_set_command_callback(command_dispatcher_handle));

    capture_context_t ctx = {
        .storage = &s_storage,
        .node_id = SDACS_NODE_ID,
        .base_topic = base_topic,
        .cal_offset_db = SDACS_CAL_OFFSET_DB,
        .record_seconds = SDACS_RECORD_SECONDS,
    };

    ESP_ERROR_CHECK(capture_task_start(&ctx));
    ESP_LOGI(TAG, "Capture started (%d s): MQTT stream + SD logging", SDACS_RECORD_SECONDS);
}
