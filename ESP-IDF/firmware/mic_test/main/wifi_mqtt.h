#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char node_id[17];        // "node01"
    uint32_t seq;            // incrementing window counter
    uint64_t t_us;           // timestamp (esp_timer_get_time)
    float rms;               // normalized RMS (~0..1)
    float dbfs;              // 20*log10(rms)
    float db_spl;            // dbfs + calibration offset
    float f_peak_hz;         // FFT peak frequency estimate
    int32_t p2p_raw;         // peak-to-peak raw counts
    int zeros;               // count of exact zero samples in window
    uint32_t n;              // number of samples in the window
    float temp_c;
    float rh_percent;
    float batt_soc_percent;
    float batt_voltage_v;
    float batt_charge_rate_pct_per_hr;
    bool batt_valid;
} sdacs_features_t;

typedef struct {
    const char *ssid;
    const char *pass;
    const char *broker_uri;  // e.g. "mqtt://192.168.1.50"
    const char *topic;       // e.g. "sdacs/node/node01/features"
} wifi_mqtt_cfg_t;

typedef void (*wifi_mqtt_cmd_cb_t)(const char *topic, const char *payload, int len);

// Start WiFi + MQTT tasks and create internal queue
esp_err_t wifi_mqtt_start(const wifi_mqtt_cfg_t *cfg);

// Non-blocking enqueue (drops if queue is full)
bool wifi_mqtt_try_send(const sdacs_features_t *f);

// Publish binary payload directly (returns error if MQTT is not connected)
esp_err_t wifi_mqtt_publish_raw(const char *topic, const void *payload, size_t len, int qos, int retain);

esp_err_t wifi_mqtt_set_command_callback(wifi_mqtt_cmd_cb_t cb);
esp_err_t wifi_mqtt_publish_status_json(const char *topic, const char *json);
esp_err_t wifi_mqtt_publish_heartbeat(const char *status);
esp_err_t wifi_mqtt_set_ota_state(bool ota_ready, bool ota_in_progress);
esp_err_t wifi_mqtt_get_base_topic(char *out, size_t out_sz);

// Optional: check if MQTT is connected (for debug/UI)
bool wifi_mqtt_is_connected(void);

/* ======================== ADDED FOR STREAMING STABILITY ======================== */

/**
 * @brief Block until WiFi got IP, or timeout.
 */
esp_err_t wifi_mqtt_wait_wifi(uint32_t timeout_ms);

/**
 * @brief Block until WiFi got IP and MQTT connected, or timeout.
 */
esp_err_t wifi_mqtt_wait_connected(uint32_t timeout_ms);
