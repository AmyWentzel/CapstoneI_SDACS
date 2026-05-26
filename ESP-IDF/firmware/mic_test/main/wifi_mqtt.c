#include "wifi_mqtt.h"
#include "config_store.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_timer.h"

#include "fuel_gauge.h"
#include "sdacs_config.h"
#include "device_state.h"

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
static bool s_wifi_connected = false;
static wifi_mqtt_cmd_cb_t s_cmd_cb = NULL;

static wifi_mqtt_cfg_t s_cfg = {0};
static char s_node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1];
static char s_base_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1];
static char s_heartbeat_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 32];
static char s_lwt_payload[160];
static bool s_heartbeat_task_started = false;
static volatile bool s_ota_in_progress = false;
static volatile bool s_ota_ready = true;
static int64_t s_boot_time_us = 0;

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

static bool strip_topic_suffix(char *topic, const char *suffix)
{
    size_t topic_len = 0;
    size_t suffix_len = 0;

    if (!topic || !suffix) {
        return false;
    }

    topic_len = strlen(topic);
    suffix_len = strlen(suffix);
    if (topic_len < suffix_len) {
        return false;
    }

    if (strcmp(topic + topic_len - suffix_len, suffix) != 0) {
        return false;
    }

    topic[topic_len - suffix_len] = '\0';
    return true;
}

static void normalize_base_topic(const char *configured_topic, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    if (!configured_topic || configured_topic[0] == '\0') {
        return;
    }

    strncpy(out, configured_topic, out_sz - 1);
    out[out_sz - 1] = '\0';

    if (strip_topic_suffix(out, "/status/heartbeat")) return;
    if (strip_topic_suffix(out, "/status")) return;
    if (strip_topic_suffix(out, "/features")) return;
    if (strip_topic_suffix(out, "/audio")) return;
    if (strip_topic_suffix(out, "/temp_humidity")) return;
}

static void build_status_topic(char *out, size_t out_sz, const char *suffix)
{
    int len = 0;

    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    if (s_base_topic[0] == '\0') {
        return;
    }

    len = snprintf(out, out_sz, "%s%s", s_base_topic, suffix ? suffix : "");
    if (len <= 0 || len >= (int)out_sz) {
        out[0] = '\0';
    }
}

static int get_wifi_rssi_dbm(void)
{
    wifi_ap_record_t ap_info = {0};

    if (!s_wifi_connected) {
        return 0;
    }

    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }

    return (int)ap_info.rssi;
}

static esp_err_t publish_heartbeat_now(const char *status)
{
    char payload[512];
    char batt_soc_buf[24] = "null";
    char batt_voltage_buf[24] = "null";
    char batt_rate_buf[24] = "null";
    uint64_t uptime_s = 0;
    int payload_len = 0;
    int msg_id = -1;
    fuel_gauge_reading_t batt = {0};
    bool batt_valid = fuel_gauge_get_latest(&batt);

    if (!s_mqtt || !s_mqtt_connected || s_heartbeat_topic[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_boot_time_us <= 0) {
        s_boot_time_us = esp_timer_get_time();
    }
    uptime_s = (uint64_t)((esp_timer_get_time() - s_boot_time_us) / 1000000LL);

    if (batt_valid) {
        (void)snprintf(batt_soc_buf, sizeof(batt_soc_buf), "%.2f", (double)batt.soc_percent);
        (void)snprintf(batt_voltage_buf, sizeof(batt_voltage_buf), "%.4f", (double)batt.voltage_v);
        (void)snprintf(batt_rate_buf, sizeof(batt_rate_buf), "%.2f", (double)batt.charge_rate_percent_per_hr);
    }

    // Node-RED heartbeat parsing now includes batt_soc_percent, batt_voltage_v, and batt_charge_rate_pct_per_hr.
    payload_len = snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"node_id\":\"%s\","
        "\"status\":\"%s\","
        "\"fw_version\":\"%s\","
        "\"capture_state\":\"%s\","
        "\"uptime_s\":%" PRIu64 ","
        "\"rssi_dbm\":%d,"
        "\"free_heap\":%u,"
        "\"wifi_connected\":%s,"
        "\"mqtt_connected\":%s,"
        "\"ota_ready\":%s,"
        "\"ota_in_progress\":%s,"
        "\"batt_soc_percent\":%s,"
        "\"batt_voltage_v\":%s,"
        "\"batt_charge_rate_pct_per_hr\":%s,"
        "\"batt_sample_count\":%u,"
        "\"batt_error_count\":%u,"
        "\"batt_last_sample_time_us\":%" PRIi64 ","
        "\"batt_valid\":%s"
        "}",
        s_node_id[0] ? s_node_id : SDACS_NODE_ID,
        status ? status : "online",
        SDACS_FW_VERSION,
        device_state_to_str(device_state_get()),
        uptime_s,
        get_wifi_rssi_dbm(),
        (unsigned)esp_get_free_heap_size(),
        s_wifi_connected ? "true" : "false",
        s_mqtt_connected ? "true" : "false",
        s_ota_ready ? "true" : "false",
        s_ota_in_progress ? "true" : "false",
        batt_soc_buf,
        batt_voltage_buf,
        batt_rate_buf,
        (unsigned)batt.sample_count,
        (unsigned)batt.error_count,
        (int64_t)batt.last_sample_time_us,
        batt_valid ? "true" : "false"
    );
    if (payload_len <= 0 || payload_len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    msg_id = esp_mqtt_client_publish(s_mqtt, s_heartbeat_topic, payload, 0, 1, 1);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (1) {
        if (s_mqtt_connected) {
            (void)publish_heartbeat_now("online");
        }
        vTaskDelay(pdMS_TO_TICKS(SDACS_HEARTBEAT_INTERVAL_MS));
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
        s_wifi_connected = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA start -> connect");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        s_wifi_connected = true;
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

        s_wifi_connected = false;
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

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            if (s_mqtt) {
                char node_cmd_topic[160];

                snprintf(node_cmd_topic, sizeof(node_cmd_topic), "%s/cmd", s_base_topic);
                (void)esp_mqtt_client_subscribe(s_mqtt, node_cmd_topic, 1);
                (void)esp_mqtt_client_subscribe(s_mqtt, "sdacs/group/all/cmd", 1);
            }
            ESP_LOGI(TAG, "MQTT connected");
            ESP_LOGI(TAG, "Subscribed to command topics");
            (void)publish_heartbeat_now("online");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_DATA:
            if (s_cmd_cb && event && event->data && event->topic &&
                event->data_len >= 0 && event->topic_len > 0) {
                char *topic = calloc(1, (size_t)event->topic_len + 1U);
                char *payload = calloc(1, (size_t)event->data_len + 1U);

                if (!topic || !payload) {
                    ESP_LOGE(TAG, "Failed to allocate MQTT command buffers");
                    free(topic);
                    free(payload);
                    break;
                }

                memcpy(topic, event->topic, (size_t)event->topic_len);
                memcpy(payload, event->data, (size_t)event->data_len);
                s_cmd_cb(topic, payload, event->data_len);
                free(topic);
                free(payload);
            }
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
        .session.last_will.topic = s_heartbeat_topic,
        .session.last_will.msg = s_lwt_payload,
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
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
    char temp_c_buf[24] = "null";
    char rh_percent_buf[24] = "null";
    char batt_soc_buf[24] = "null";
    char batt_voltage_buf[24] = "null";
    char batt_rate_buf[24] = "null";
    const char *batt_valid_str = f->batt_valid ? "true" : "false";

    if (isfinite(f->temp_c)) {
        (void)snprintf(temp_c_buf, sizeof(temp_c_buf), "%.2f", (double)f->temp_c);
    }
    if (isfinite(f->rh_percent)) {
        (void)snprintf(rh_percent_buf, sizeof(rh_percent_buf), "%.2f", (double)f->rh_percent);
    }
    if (isfinite(f->batt_soc_percent)) {
        (void)snprintf(batt_soc_buf, sizeof(batt_soc_buf), "%.2f", (double)f->batt_soc_percent);
    }
    if (isfinite(f->batt_voltage_v)) {
        (void)snprintf(batt_voltage_buf, sizeof(batt_voltage_buf), "%.4f", (double)f->batt_voltage_v);
    }
    if (isfinite(f->batt_charge_rate_pct_per_hr)) {
        (void)snprintf(batt_rate_buf, sizeof(batt_rate_buf), "%.2f", (double)f->batt_charge_rate_pct_per_hr);
    }

    // Keep it compact; Node-RED can parse JSON easily.
    // Node-RED should also parse batt_soc_percent, batt_voltage_v, and batt_charge_rate_pct_per_hr.
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
          "\"zeros\":%d,"
          "\"temp_c\":%s,"
          "\"rh_percent\":%s,"
          "\"batt_soc_percent\":%s,"
          "\"batt_voltage_v\":%s,"
          "\"batt_charge_rate_pct_per_hr\":%s,"
          "\"batt_valid\":%s"
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
        f->zeros,
        temp_c_buf,
        rh_percent_buf,
        batt_soc_buf,
        batt_voltage_buf,
        batt_rate_buf,
        batt_valid_str
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

esp_err_t wifi_mqtt_start(const wifi_mqtt_cfg_t *cfg)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    const char *broker_uri = NULL;
    const char *topic = NULL;
    const char *node_id = NULL;
    esp_err_t err = ESP_OK;

    (void)cfg; // Runtime settings come from config_store.

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

    err = config_store_get_node_id(&node_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_node_id failed: %s", esp_err_to_name(err));
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
    strncpy(s_node_id, (node_id && node_id[0] != '\0') ? node_id : SDACS_NODE_ID, sizeof(s_node_id) - 1);
    s_node_id[sizeof(s_node_id) - 1] = '\0';
    normalize_base_topic(topic, s_base_topic, sizeof(s_base_topic));

    if (s_base_topic[0] == '\0') {
        ESP_LOGE(TAG, "Unable to derive MQTT base topic from '%s'", topic);
        return ESP_ERR_INVALID_STATE;
    }

    build_status_topic(s_heartbeat_topic, sizeof(s_heartbeat_topic), "/status/heartbeat");
    if (s_heartbeat_topic[0] == '\0') {
        ESP_LOGE(TAG, "Heartbeat topic is invalid");
        return ESP_ERR_INVALID_SIZE;
    }

    if (snprintf(s_lwt_payload,
                 sizeof(s_lwt_payload),
                 "{\"node_id\":\"%s\",\"status\":\"offline\",\"reason\":\"lwt\"}",
                 s_node_id) >= (int)sizeof(s_lwt_payload)) {
        ESP_LOGE(TAG, "LWT payload too long");
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_boot_time_us <= 0) {
        s_boot_time_us = esp_timer_get_time();
    }

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

    if (!s_heartbeat_task_started) {
        BaseType_t ok = xTaskCreate(
            heartbeat_task,
            "heartbeat_task",
            SDACS_HEARTBEAT_TASK_STACK_SIZE,
            NULL,
            SDACS_HEARTBEAT_TASK_PRIORITY,
            NULL
        );
        if (ok != pdPASS) {
            return ESP_FAIL;
        }
        s_heartbeat_task_started = true;
    }

    return ESP_OK;
}

/* ======================== ADDED FOR STREAMING STABILITY ======================== */

esp_err_t wifi_mqtt_wait_wifi(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & WIFI_FAIL_BIT) return ESP_FAIL;
    if (!(bits & WIFI_CONNECTED_BIT)) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t wifi_mqtt_wait_connected(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    /* Wait for WiFi connected (got IP) */
    esp_err_t err = wifi_mqtt_wait_wifi(timeout_ms);
    if (err != ESP_OK) return err;

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

esp_err_t wifi_mqtt_set_command_callback(wifi_mqtt_cmd_cb_t cb)
{
    s_cmd_cb = cb;
    return ESP_OK;
}

esp_err_t wifi_mqtt_publish_status_json(const char *topic, const char *json)
{
    if (!topic || !json) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt, topic, json, 0, 1, 1);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_mqtt_publish_heartbeat(const char *status)
{
    return publish_heartbeat_now(status);
}

esp_err_t wifi_mqtt_set_ota_state(bool ota_ready, bool ota_in_progress)
{
    s_ota_ready = ota_ready;
    s_ota_in_progress = ota_in_progress;
    return ESP_OK;
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

esp_err_t wifi_mqtt_get_base_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    if (s_base_topic[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(out, s_base_topic, out_sz - 1);
    out[out_sz - 1] = '\0';
    return ESP_OK;
}
