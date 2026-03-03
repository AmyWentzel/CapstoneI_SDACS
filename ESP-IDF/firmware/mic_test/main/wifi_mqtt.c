#include "wifi_mqtt.h"
#include "config_store.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_timer.h"

static const char *TAG = "WIFI_MQTT";

// --- WiFi event bits ---
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const int WIFI_MAX_RETRY = 10;

static QueueHandle_t s_feat_q = NULL;
static esp_mqtt_client_handle_t s_mqtt = NULL;

static bool s_mqtt_connected = false;

typedef struct {
    const char *ssid;
    const char *pass;
    const char *broker_uri;
    const char *topic;
} wifi_mqtt_runtime_cfg_t;

static wifi_mqtt_runtime_cfg_t s_cfg = {0};

// Keep queue small; we only publish ~1 msg/sec
#define FEATURES_QUEUE_LEN  8

static const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_ENTERPRISE: return "ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        default: return "UNKNOWN";
    }
}

static void log_sta_ap_info(const char *prefix)
{
    wifi_ap_record_t ap_info = {0};
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: esp_wifi_sta_get_ap_info failed: %s",
                 prefix, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "%s: AP='%s' RSSI=%d dBm CH=%u AUTH=%s",
             prefix,
             (const char *)ap_info.ssid,
             (int)ap_info.rssi,
             (unsigned)ap_info.primary,
             authmode_to_str(ap_info.authmode));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA start -> connect");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
        if (evt) {
            ESP_LOGI(TAG,
                     "WiFi connected: SSID='%.*s' channel=%u authmode=%s",
                     (int)evt->ssid_len,
                     (const char *)evt->ssid,
                     (unsigned)evt->channel,
                     authmode_to_str(evt->authmode));
        } else {
            ESP_LOGI(TAG, "WiFi connected");
        }
        log_sta_ap_info("WiFi link");
    
    }else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {

        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG, "WiFi disconnected (reason=%d)", disc ? disc->reason : -1);

        s_mqtt_connected = false;

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d",
                    s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        log_sta_ap_info("WiFi IP");
    }
}

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

static esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    /* ======================== ADDED FOR STREAMING STABILITY ======================== */
    /* Disable WiFi power save for stable continuous streaming */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    
    ESP_LOGI(TAG, "WiFi init done. SSID=%s", ssid);
    return ESP_OK;
}

static esp_err_t mqtt_start_client(const char *broker_uri)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        // you can add username/password here later if needed
    };

    s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));
    ESP_LOGI(TAG, "MQTT client start. Broker=%s", broker_uri);
    return ESP_OK;
}

// Builds JSON without cJSON to keep dependencies simple.
static int build_features_json(char *out, size_t out_sz, const sdacs_features_t *f)
{
    // Keep it compact; Node-RED can parse JSON easily.
    // Blutooth HERE???
    return snprintf(out, out_sz,
        "{"
          "\"node\":\"%s\","
          "\"seq\":%u,"
          "\"t_us\":%" PRIu64 ","
          "\"n\":%u,"
          "\"rms\":%.6f,"
          "\"dbfs\":%.2f,"
          "\"db_spl\":%.2f,"
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
        f->p2p_raw,
        f->zeros
    );
}

static void mqtt_publish_task(void *arg)
{
    // Wait for WiFi before starting MQTT
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi failed; MQTT task exiting");
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(mqtt_start_client(s_cfg.broker_uri));

    sdacs_features_t f;
    char payload[512];

    while (1) {
        if (xQueueReceive(s_feat_q, &f, portMAX_DELAY) == pdTRUE) {

            if (!s_mqtt || !s_mqtt_connected) {
                // Don’t block or retry here. Drop until connected.
                continue;
            }

            int len = build_features_json(payload, sizeof(payload), &f);
            if (len <= 0 || (size_t)len >= sizeof(payload)) {
                ESP_LOGW(TAG, "JSON build truncated; dropping");
                continue;
            }

            // QoS 0 is fine for live dashboards; you can bump later.
            int msg_id = esp_mqtt_client_publish(
                s_mqtt,
                s_cfg.topic,
                payload,
                0,    // length 0 -> treat as null-terminated
                0,    // qos
                0     // retain
            );

            (void)msg_id; // optional debug
        }
    }
}

esp_err_t wifi_mqtt_start(void)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    const char *broker_uri = NULL;
    const char *topic = NULL;
    esp_err_t err = ESP_OK;

    err = config_store_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return err;
    }

    err = config_store_get_mqtt(&broker_uri, &topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!ssid || !pass || !broker_uri || !topic ||
        ssid[0] == '\0' || broker_uri[0] == '\0' || topic[0] == '\0') {
        ESP_LOGE(TAG, "Missing WiFi/MQTT settings in config_store.");
        return ESP_ERR_INVALID_STATE;
    }

    s_cfg.ssid = ssid;
    s_cfg.pass = pass;
    s_cfg.broker_uri = broker_uri;
    s_cfg.topic = topic;

    if (!s_feat_q) {
        s_feat_q = xQueueCreate(FEATURES_QUEUE_LEN, sizeof(sdacs_features_t));
        if (!s_feat_q) return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(wifi_init_sta(s_cfg.ssid, s_cfg.pass));

    // Run publisher task on core 0 (leave core 1 for audio if you want)
    xTaskCreatePinnedToCore(
        mqtt_publish_task,
        "mqtt_publish_task",
        4096,
        NULL,
        5,
        NULL,
        0
    );

    return ESP_OK;
}

/* ======================== ADDED FOR STREAMING STABILITY ======================== */

esp_err_t wifi_mqtt_wait_connected(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    /* Wait for WiFi connected (got IP) */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        timeout_ticks
    );

    if (bits & WIFI_FAIL_BIT) return ESP_FAIL;
    if (!(bits & WIFI_CONNECTED_BIT)) return ESP_ERR_TIMEOUT;

    /* Then wait for MQTT connection */
    while (!s_mqtt_connected) {
        if ((xTaskGetTickCount() - start) > timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    return ESP_OK;
}
// This lets your mic task publish binary chunks 
// immediately without allocating new buffers or queueing.
esp_err_t wifi_mqtt_publish_raw(const char *topic,
                               const void *payload,
                               size_t len,
                               int qos,
                               int retain)
{
    if (!topic || !payload || len == 0) return ESP_ERR_INVALID_ARG;
    if (!s_mqtt || !s_mqtt_connected) return ESP_ERR_INVALID_STATE;

    int msg_id = esp_mqtt_client_publish(
        s_mqtt,
        topic,
        (const char *)payload,
        (int)len,   // IMPORTANT: binary length
        qos,
        retain
    );
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}


bool wifi_mqtt_try_send(const sdacs_features_t *f)
{
    if (!s_feat_q || !f) return false;
    // 0 tick wait => non-blocking
    return (xQueueSend(s_feat_q, f, 0) == pdTRUE);
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}
