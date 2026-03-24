#include "esp_log.h"

#include "audio_input.h"
#include "capture_task.h"
#include "command_dispatcher.h"
#include "config_store.h"
#include "device_state.h"
#include "fft_metrics.h"
#include "network_provisioning.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "temp_humidity.h"
#include "time_sync.h"
#include "wifi_mqtt.h"

static const char *TAG = "app_main";
static run_storage_t s_storage = {0};

static void publish_boot_status(const char *base_topic)
{
    char topic[160];
    char payload[256];

    if (!base_topic || base_topic[0] == '\0') {
        ESP_LOGW(TAG, "Skipping boot status publish; base topic missing");
        return;
    }

    if (snprintf(topic, sizeof(topic), "%s/status", base_topic) >= (int)sizeof(topic)) {
        ESP_LOGW(TAG, "Status topic too long");
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{\"node\":\"%s\",\"mode\":\"%s\",\"fw_version\":\"%s\",\"ota_capable\":true}",
                 SDACS_NODE_ID,
                 device_state_to_str(device_state_get()),
                 SDACS_FW_VERSION) >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Boot status payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Boot status publish failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    const char *base_topic = NULL;
    const char *broker_uri = NULL;

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());
    ESP_ERROR_CHECK(wifi_mqtt_start(NULL));
    time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);

    ESP_ERROR_CHECK(run_storage_init(&s_storage));

    bool th_ok = temp_humidity_start(
        SDACS_TEMP_HUMIDITY_I2C_PORT,
        SDACS_TEMP_HUMIDITY_SDA_GPIO,
        SDACS_TEMP_HUMIDITY_SCL_GPIO,
        SDACS_TEMP_HUMIDITY_FREQ_HZ,
        SDACS_TEMP_HUMIDITY_ADDR,
        SDACS_TEMP_HUMIDITY_PERIOD_MS
    );
    if (!th_ok) {
        ESP_LOGW(TAG, "temp_humidity_start failed; metrics will show NAN");
    }

    ESP_ERROR_CHECK(audio_input_init());
    ESP_ERROR_CHECK(fft_metrics_init());
    ESP_ERROR_CHECK(config_store_get_mqtt(&broker_uri, &base_topic));
    (void)broker_uri;

    device_state_init();
    command_dispatcher_init(&s_storage, SDACS_NODE_ID, base_topic);
    ESP_ERROR_CHECK(wifi_mqtt_set_command_callback(command_dispatcher_handle));

    if (wifi_mqtt_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS) == ESP_OK) {
        publish_boot_status(base_topic);
    } else {
        ESP_LOGW(TAG, "MQTT not connected yet; boot status will be published after a status request");
    }

    ESP_LOGI(TAG, "System initialized. Entering IDLE state and waiting for commands.");
}
