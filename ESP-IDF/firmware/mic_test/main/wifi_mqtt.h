/*
 * SDACS module: Public Wi-Fi/MQTT transport interface
 *
 * Purpose:
 *   Defines telemetry record structures, connection control, publishers, command callbacks, and storage-status integration.
 *
 * Design note:
 *   This is the firmware boundary between local sensing state and Raspberry Pi middleware.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_input.h"
#include "fft_metrics.h"
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
    float f_peak_full_hz;
    float f_peak_acoustic_hz;
    float f_peak_acoustic_energy;
    float tone_1khz_energy;
    float tone_1khz_ratio;
    float tone_1khz_peak_hz;
    float tone_1khz_peak_energy;
    float tone_1khz_local_total_energy;
    float tone_1khz_local_noise_energy;
    float tone_1khz_local_noise_avg_energy;
    float tone_1khz_local_ratio;
    float tone_1khz_contrast_db;
    bool tone_1khz_ratio_hit;
    bool tone_1khz_peak_hit;
    bool tone_1khz_contrast_hit;
    bool tone_1khz_detected;
    int tone_1khz_local_noise_bins;
    int tone_1khz_band_bins;
    float acoustic_band_energy;
    float room_band_total_energy;
    float low_rumble_energy;
    float low_rumble_ratio;
    float band_sub_energy;
    float band_bass_energy;
    float band_low_mid_energy;
    float band_mid_energy;
    float band_presence_energy;
    float band_high_energy;
    float band_sub_ratio;
    float band_bass_ratio;
    float band_low_mid_ratio;
    float band_mid_ratio;
    float band_presence_ratio;
    float band_high_ratio;
    float band_sub_peak_hz;
    float band_bass_peak_hz;
    float band_low_mid_peak_hz;
    float band_mid_peak_hz;
    float band_presence_peak_hz;
    float band_high_peak_hz;
    float band_sub_peak_energy;
    float band_bass_peak_energy;
    float band_low_mid_peak_energy;
    float band_mid_peak_energy;
    float band_presence_peak_energy;
    float band_high_peak_energy;
    float dominant_band_ratio;
    char dominant_band_name[24];
    float dominant_band_peak_hz;
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
    char i2s_rx_mode[16];
    char i2s_selected_slot[8];
    char i2s_slot_mask[8];
    uint32_t i2s_sample_rate_hz;
    uint32_t i2s_data_bits;
    uint32_t i2s_valid_bits;
    char i2s_sample_conversion[16];
    uint32_t i2s_sample_conversion_mode;
    bool scene_metrics_valid;
    audio_metrics_t scene_metrics;
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
    const char *broker_uri;  // e.g. "mqtt://<broker-host>:1883"
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
