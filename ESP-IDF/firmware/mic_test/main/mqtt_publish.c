#include "mqtt_publish.h"

#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "config_store.h"
#include "wifi_station.h"

static const char *TAG = "mqtt_publish";

#define FEATURES_QUEUE_LEN 8

static QueueHandle_t s_feat_q = NULL;
static esp_mqtt_client_handle_t s_mqtt = NULL;
static bool s_mqtt_connected = false;
static const char *s_broker_uri = NULL;
static const char *s_topic = NULL;
static bool s_started = false;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_ERROR:
            s_mqtt_connected = false;
            ESP_LOGE(TAG, "MQTT error");
            break;
        default:
            break;
    }
}

static esp_err_t mqtt_start_client(const char *broker_uri)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
    };

    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));

    ESP_LOGI(TAG, "MQTT client start. Broker=%s", broker_uri);
    return ESP_OK;
}

static int build_features_json(char *out, size_t out_sz, const sdacs_features_t *f)
{
    return snprintf(out, out_sz,
        "{"
          "\"node\":\"%s\","
          "\"seq\":%u,"
          "\"t_us\":%" PRIu64 ","
          "\"n\":%u,"
          "\"rms\":%.6f,"
          "\"dbfs\":%.2f,"
          "\"db_spl\":%.2f,"
          "\"fft_peak_Hz\":%.1f,"
          "\"f_peak_hz\":%.1f,"
          "\"p2p_raw\":%" PRId32 ","
          "\"zeros\":%d"
        "}",
        f->node_id,
        (unsigned)f->seq,
        (uint64_t)f->t_us,
        (unsigned)f->n,
        f->rms,
        f->dbfs,
        f->db_spl,
        f->f_peak_hz,
        f->f_peak_hz,
        f->p2p_raw,
        f->zeros
    );
}

static void mqtt_publish_task(void *arg)
{
    (void)arg;

    esp_err_t err = sdacs_wifi_station_wait_connected(UINT32_MAX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi unavailable; MQTT task exiting: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    err = mqtt_start_client(s_broker_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    sdacs_features_t f;
    char payload[512];

    while (1) {
        if (xQueueReceive(s_feat_q, &f, portMAX_DELAY) == pdTRUE) {
            if (!s_mqtt || !s_mqtt_connected) {
                continue;
            }

            int len = build_features_json(payload, sizeof(payload), &f);
            if (len <= 0 || (size_t)len >= sizeof(payload)) {
                ESP_LOGW(TAG, "JSON build truncated; dropping");
                continue;
            }

            (void)esp_mqtt_client_publish(
                s_mqtt,
                s_topic,
                payload,
                0,
                0,
                0
            );
        }
    }
}

esp_err_t mqtt_publish_start(void)
{
    const char *broker_uri = NULL;
    const char *topic = NULL;
    esp_err_t err = ESP_OK;

    if (s_started) {
        return ESP_OK;
    }

    err = config_store_get_mqtt(&broker_uri, &topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!broker_uri || !topic || broker_uri[0] == '\0' || topic[0] == '\0') {
        ESP_LOGW(TAG, "Missing MQTT settings in config_store; MQTT disabled.");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_feat_q) {
        s_feat_q = xQueueCreate(FEATURES_QUEUE_LEN, sizeof(sdacs_features_t));
        if (!s_feat_q) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_broker_uri = broker_uri;
    s_topic = topic;

    if (xTaskCreatePinnedToCore(
            mqtt_publish_task,
            "mqtt_publish_task",
            4096,
            NULL,
            5,
            NULL,
            0) != pdPASS) {
        return ESP_FAIL;
    }

    s_started = true;
    return ESP_OK;
}

esp_err_t mqtt_publish_wait_connected(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    esp_err_t err = sdacs_wifi_station_wait_connected(timeout_ms);
    if (err != ESP_OK) {
        return err;
    }

    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    while (!s_mqtt_connected) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    return ESP_OK;
}

bool mqtt_publish_try_send_features(const sdacs_features_t *f)
{
    if (!s_feat_q || !f) {
        return false;
    }
    return xQueueSend(s_feat_q, f, 0) == pdTRUE;
}

esp_err_t mqtt_publish_raw(const char *topic, const void *payload, size_t len, int qos, int retain)
{
    if (!topic || !payload || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mqtt || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(
        s_mqtt,
        topic,
        (const char *)payload,
        (int)len,
        qos,
        retain
    );
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

bool mqtt_publish_is_connected(void)
{
    return s_mqtt_connected;
}
