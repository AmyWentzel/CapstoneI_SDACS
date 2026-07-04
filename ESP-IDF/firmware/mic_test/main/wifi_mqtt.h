#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_input.h"
#include "esp_err.h"
#include "run_storage.h"

typedef struct {
    char node_id[17];        // "node01"
    uint32_t seq;            // incrementing window counter
    uint64_t t_us;           // timestamp (esp_timer_get_time)
    uint64_t timestamp_us;   // clearer duplicate of t_us for new consumers
    char capture_state[24];
    uint32_t record_seconds;
    float rms;               // normalized RMS (~0..1)
    float dbfs;              // 20*log10(rms)
    float db_spl;            // depends on the active calibration offset
    float peak_db_spl;       // depends on the active calibration offset
    float cal_offset_db;
    float f_peak_hz;          // FFT peak frequency estimate
    float fft_low_ratio;
    float fft_mid_ratio;
    float fft_high_ratio;
    float fft_total_energy;
    int32_t p2p_raw;         // peak-to-peak raw counts
    int zeros;               // count of exact zero samples in window
    uint32_t n;              // number of samples in the window
    uint32_t window_elapsed_ms;
    uint32_t expected_samples;
    float effective_sample_rate_hz;
    bool sample_rate_ok;
    float temp_c;
    float rh_percent;
    float batt_soc_percent;
    float batt_voltage_v;
    float batt_charge_rate_pct_per_hr;
    bool batt_valid;
    char storage_mode[24];
    bool sd_enabled;
    bool sd_writes_enabled;
    bool storage_mounted;
    char i2s_frame_mode[12];
    char i2s_selected_slot[8];
    char i2s_slot_mask[8];
    uint32_t i2s_sample_rate_hz;
    uint32_t i2s_data_bits;
    uint32_t i2s_valid_bits;
    audio_input_raw_diagnostics_t raw_diag;
    char storage_error[32];
    char storage_error_detail[128];
    char audio_error[32];
    uint32_t audio_read_errors;
    uint32_t audio_read_timeouts;
    uint32_t consecutive_timeouts;
    uint32_t total_i2s_reads;
    uint32_t successful_i2s_reads;
    uint32_t err;
} sdacs_features_t;

typedef struct {
    const char *ssid;
    const char *pass;
    const char *broker_uri;  // e.g. "mqtt://192.168.1.50"
    const char *topic;       // built from compiled node ID, e.g. "sdacs/node/<node_id>/features"
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
void wifi_mqtt_set_storage_status_provider(run_storage_t *storage);
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
