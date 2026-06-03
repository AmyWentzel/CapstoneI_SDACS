#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"

#include "audio_input.h"
#include "capture_task.h"
#include "config_store.h"
#include "fft_metrics.h"
#include "network_provisioning.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "temp_humidity.h"

static const char *TAG = "app_main";
static run_storage_t s_storage = {0};

void app_main(void)
{
    const char *base_topic = NULL;
    const char *broker_uri = NULL;

    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());

    ESP_ERROR_CHECK(run_storage_init(&s_storage));
    ESP_ERROR_CHECK(run_storage_create_session(&s_storage, SDACS_NODE_ID));

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

    capture_context_t ctx = {
        .storage = &s_storage,
        .node_id = SDACS_NODE_ID,
        .base_topic = base_topic,
        .cal_offset_db = SDACS_CAL_OFFSET_DB,
        .record_seconds = SDACS_RECORD_SECONDS,
    };

    ESP_ERROR_CHECK(capture_task_start(&ctx));
    ESP_LOGI(TAG, "Capture started (%d s): local record, analyze, then post-file MQTT", SDACS_RECORD_SECONDS);

    if (capture_task_wait_complete(UINT32_MAX)) {
        ESP_LOGI(TAG, "Capture sequence complete; exiting with code 0");
    } else {
        ESP_LOGW(TAG, "Capture task did not complete cleanly");
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
