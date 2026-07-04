#include "capture_task.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "audio_input.h"
#include "config_store.h"
#include "device_state.h"
#include "fft_metrics.h"
#include "fuel_gauge.h"
#include "sdacs_config.h"
#include "temp_humidity.h"
#include "time_sync.h"
#include "wifi_mqtt.h"

typedef struct {
    capture_context_t ctx;
    char node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1];
    char base_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1];
    char request_id[64];
    char status_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
    char complete_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 32];
} capture_task_state_t;

static const char *TAG = "capture_task";

typedef struct {
    uint32_t audio_read_errors;
    uint32_t audio_read_timeouts;
    uint32_t consecutive_timeouts;
    uint32_t total_i2s_reads;
    uint32_t successful_i2s_reads;
    uint32_t mqtt_feature_drops;
} capture_io_stats_t;

typedef struct {
    uint32_t attempt;
    uint32_t samples;
    uint32_t expected_samples;
    uint32_t elapsed_ms;
    float effective_sample_rate_hz;
    uint32_t read_calls;
    uint32_t successful_reads;
    uint32_t timeouts;
    uint32_t errors;
    uint64_t raw_words_read;
    float effective_raw_word_rate_hz;
    float avg_read_elapsed_us;
    float avg_timeout_elapsed_us;
    uint32_t timeout_immediate_count;
    bool pass;
} i2s_preflight_result_t;

static void capture_publish_status(capture_task_state_t *state,
                                   sdacs_mode_t mode,
                                   const char *message)
{
    char payload[448];
    int len = 0;

    if (!state || state->status_topic[0] == '\0') {
        return;
    }

    len = snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"node_id\":\"%s\","
        "\"record_type\":\"capture_status\","
        "\"timestamp\":%" PRIi64 ","
        "\"fw_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"state\":\"%s\","
        "\"delay_ms\":%u,"
        "\"record_seconds\":%u,"
        "\"cal_offset_db\":%.2f,"
        "\"storage_mode\":\"%s\","
        "\"sd_enabled\":%s,"
        "\"sd_writes_enabled\":%s,"
        "\"message\":\"%s\""
        "}",
        state->node_id,
        (int64_t)esp_timer_get_time(),
        SDACS_FW_VERSION,
        state->request_id,
        device_state_to_str(mode),
        (unsigned)state->ctx.delay_ms,
        (unsigned)state->ctx.record_seconds,
        (double)state->ctx.cal_offset_db,
        state->ctx.storage_mode,
        state->ctx.sd_enabled ? "true" : "false",
        state->ctx.sd_writes_enabled ? "true" : "false",
        message ? message : ""
    );

    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Capture status payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(state->status_topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Capture status publish failed: %s", esp_err_to_name(err));
    }
}

static void capture_set_state(capture_task_state_t *state,
                              sdacs_mode_t mode,
                              const char *message)
{
    device_state_set(mode);
    ESP_LOGI(TAG, "Capture state: %s (%s)",
             device_state_to_str(mode),
             message ? message : "");
    capture_publish_status(state, mode, message);
}

static void capture_publish_preflight_failed(capture_task_state_t *state,
                                             const i2s_preflight_result_t *result,
                                             uint32_t attempts,
                                             const char *audio_error,
                                             const char *message)
{
    char payload[1500];
    int len = 0;
    audio_input_counters_t counters = {0};

    if (!state || state->status_topic[0] == '\0') {
        return;
    }
    audio_input_get_counters(&counters);

    len = snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"node_id\":\"%s\","
        "\"record_type\":\"capture_status\","
        "\"timestamp\":%" PRIi64 ","
        "\"fw_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"state\":\"idle\","
        "\"status\":\"error\","
        "\"storage_mode\":\"%s\","
        "\"sd_enabled\":%s,"
        "\"sd_writes_enabled\":%s,"
        "\"audio_error\":\"%s\","
        "\"preflight_attempts\":%u,"
        "\"preflight_samples\":%u,"
        "\"preflight_expected_samples\":%u,"
        "\"preflight_elapsed_ms\":%u,"
        "\"preflight_effective_sample_rate_hz\":%.2f,"
        "\"preflight_effective_raw_word_rate_hz\":%.2f,"
        "\"preflight_raw_words_read\":%" PRIu64 ","
        "\"preflight_selected_samples_read\":%u,"
        "\"preflight_read_calls\":%u,"
        "\"preflight_successful_reads\":%u,"
        "\"preflight_timeouts\":%u,"
        "\"preflight_errors\":%u,"
        "\"preflight_avg_read_elapsed_us\":%.2f,"
        "\"preflight_avg_timeout_elapsed_us\":%.2f,"
        "\"preflight_timeout_immediate_count\":%u,"
        "\"expected_sample_rate_hz\":%u,"
        "\"i2s_rx_restarts\":%u,"
        "\"i2s_rx_recreates\":%u,"
        "\"i2s_rx_mode\":\"%s\","
        "\"i2s_selected_slot\":\"%s\","
        "\"i2s_sample_conversion\":\"%s\","
        "\"message\":\"%s\""
        "}",
        state->node_id,
        (int64_t)esp_timer_get_time(),
        SDACS_FW_VERSION,
        state->request_id,
        state->ctx.storage_mode,
        state->ctx.sd_enabled ? "true" : "false",
        state->ctx.sd_writes_enabled ? "true" : "false",
        audio_error ? audio_error : "i2s_preflight_failed",
        (unsigned)attempts,
        result ? (unsigned)result->samples : 0U,
        result ? (unsigned)result->expected_samples : 0U,
        result ? (unsigned)result->elapsed_ms : 0U,
        result ? (double)result->effective_sample_rate_hz : 0.0,
        result ? (double)result->effective_raw_word_rate_hz : 0.0,
        result ? result->raw_words_read : 0ULL,
        result ? (unsigned)result->samples : 0U,
        result ? (unsigned)result->read_calls : 0U,
        result ? (unsigned)result->successful_reads : 0U,
        result ? (unsigned)result->timeouts : 0U,
        result ? (unsigned)result->errors : 0U,
        result ? (double)result->avg_read_elapsed_us : 0.0,
        result ? (double)result->avg_timeout_elapsed_us : 0.0,
        result ? (unsigned)result->timeout_immediate_count : 0U,
        (unsigned)SDACS_SAMPLE_RATE_HZ,
        (unsigned)counters.rx_restarts,
        (unsigned)counters.rx_recreates,
        SDACS_I2S_RX_MODE_LABEL,
        SDACS_I2S_SELECTED_SLOT_LABEL,
        SDACS_I2S_SAMPLE_CONVERSION_LABEL,
        message ? message : "capture rejected: I2S preflight failed after recovery"
    );

    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "I2S preflight failure payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(state->status_topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S preflight failure publish failed: %s", esp_err_to_name(err));
    }
}

static void capture_publish_complete(capture_task_state_t *state,
                                     size_t raw_bytes,
                                     size_t wav_bytes,
                                     size_t csv_bytes,
                                     uint32_t total_samples,
                                     uint32_t expected_total_samples,
                                     int64_t elapsed_ms,
                                     float effective_sample_rate_hz,
                                     bool sample_rate_ok,
                                     bool capture_valid,
                                     const capture_io_stats_t *io_stats,
                                     const char *audio_error,
                                     const char *status,
                                     const char *message)
{
    char payload[1800];
    char timestamp[32];
    const char *raw_path = "";
    const char *wav_path = "";
    const char *csv_path = "";
    const char *status_text = status ? status : (capture_valid ? "complete" : "error");
    int len = 0;

    if (!state || state->complete_topic[0] == '\0') {
        return;
    }

    if (state->ctx.sd_writes_enabled && state->ctx.storage) {
        raw_path = state->ctx.storage->raw_path;
        wav_path = state->ctx.storage->wav_path;
        csv_path = state->ctx.storage->csv_path;
    }

    time_sync_get_iso8601(timestamp, sizeof(timestamp));
    len = snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"node_id\":\"%s\","
        "\"record_type\":\"capture_complete\","
        "\"fw_version\":\"%s\","
        "\"request_id\":\"%s\","
        "\"state\":\"%s\","
        "\"status\":\"%s\","
        "\"capture_valid\":%s,"
        "\"storage_mode\":\"%s\","
        "\"sd_enabled\":%s,"
        "\"sd_writes_enabled\":%s,"
        "\"i2s_frame_mode\":\"stereo\","
        "\"i2s_rx_mode\":\"%s\","
        "\"i2s_selected_slot\":\"%s\","
        "\"i2s_sample_rate_hz\":%u,"
        "\"i2s_data_bits\":32,"
        "\"i2s_valid_bits\":%u,"
        "\"i2s_sample_conversion\":\"%s\","
        "\"i2s_sample_conversion_mode\":%u,"
        "\"record_seconds\":%u,"
        "\"total_samples\":%u,"
        "\"expected_total_samples\":%u,"
        "\"elapsed_ms\":%" PRIi64 ","
        "\"effective_sample_rate_hz\":%.2f,"
        "\"sample_rate_ok\":%s,"
        "\"audio_read_errors\":%u,"
        "\"audio_read_timeouts\":%u,"
        "\"successful_i2s_reads\":%u,"
        "\"total_i2s_reads\":%u,"
        "\"mqtt_feature_drops\":%u,"
        "\"audio_error\":\"%s\","
        "\"raw_path\":\"%s\","
        "\"wav_path\":\"%s\","
        "\"metrics_path\":\"%s\","
        "\"raw_bytes\":%u,"
        "\"wav_bytes\":%u,"
        "\"metrics_bytes\":%u,"
        "\"timestamp\":\"%s\","
        "\"message\":\"%s\""
        "}",
        state->node_id,
        SDACS_FW_VERSION,
        state->request_id,
        status_text,
        status_text,
        capture_valid ? "true" : "false",
        state->ctx.storage_mode,
        state->ctx.sd_enabled ? "true" : "false",
        state->ctx.sd_writes_enabled ? "true" : "false",
        SDACS_I2S_RX_MODE_LABEL,
        SDACS_I2S_SELECTED_SLOT_LABEL,
        (unsigned)SDACS_SAMPLE_RATE_HZ,
        (unsigned)SDACS_MIC_VALID_BITS,
        SDACS_I2S_SAMPLE_CONVERSION_LABEL,
        (unsigned)SDACS_I2S_SAMPLE_CONVERSION_MODE,
        (unsigned)state->ctx.record_seconds,
        (unsigned)total_samples,
        (unsigned)expected_total_samples,
        (int64_t)elapsed_ms,
        (double)effective_sample_rate_hz,
        sample_rate_ok ? "true" : "false",
        io_stats ? (unsigned)io_stats->audio_read_errors : 0U,
        io_stats ? (unsigned)io_stats->audio_read_timeouts : 0U,
        io_stats ? (unsigned)io_stats->successful_i2s_reads : 0U,
        io_stats ? (unsigned)io_stats->total_i2s_reads : 0U,
        io_stats ? (unsigned)io_stats->mqtt_feature_drops : 0U,
        audio_error ? audio_error : "",
        raw_path,
        wav_path,
        csv_path,
        (unsigned)raw_bytes,
        (unsigned)wav_bytes,
        (unsigned)csv_bytes,
        timestamp,
        message ? message : ""
    );

    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Capture complete payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(state->complete_topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "capture_complete publish failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "capture_complete published");
    }
}

static bool capture_run_i2s_preflight(capture_task_state_t *state,
                                      int32_t *read_buf,
                                      uint32_t attempt,
                                      i2s_preflight_result_t *out_result)
{
    size_t samples_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t elapsed_us = 0;
    audio_input_counters_t counters = {0};
    i2s_preflight_result_t result = {
        .attempt = attempt,
    };

    audio_input_reset_counters();
    while ((esp_timer_get_time() - start_us) < ((int64_t)SDACS_I2S_PREFLIGHT_MS * 1000LL)) {
        esp_err_t err = audio_input_read_s24(
            read_buf,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S preflight read failed: %s", esp_err_to_name(err));
            continue;
        }
        result.samples += (uint32_t)samples_read;
    }

    elapsed_us = esp_timer_get_time() - start_us;
    audio_input_get_counters(&counters);
    result.elapsed_ms = (uint32_t)(elapsed_us / 1000LL);
    result.expected_samples = elapsed_us > 0
        ? (uint32_t)(((int64_t)SDACS_SAMPLE_RATE_HZ * elapsed_us) / 1000000LL)
        : 0U;
    result.effective_sample_rate_hz = elapsed_us > 0
        ? ((float)result.samples * 1000000.0f) / (float)elapsed_us
        : 0.0f;
    result.raw_words_read = counters.total_raw_words;
    result.effective_raw_word_rate_hz = elapsed_us > 0
        ? ((float)counters.total_raw_words * 1000000.0f) / (float)elapsed_us
        : 0.0f;
    result.avg_read_elapsed_us = counters.total_read_calls > 0U
        ? (float)counters.total_read_elapsed_us / (float)counters.total_read_calls
        : 0.0f;
    result.avg_timeout_elapsed_us = counters.timeouts > 0U
        ? (float)counters.total_timeout_elapsed_us / (float)counters.timeouts
        : 0.0f;
    result.timeout_immediate_count = counters.timeout_immediate_count;
    result.read_calls = counters.total_read_calls;
    result.successful_reads = counters.successful_reads;
    result.timeouts = counters.timeouts;
    result.errors = counters.errors;
    result.pass = result.errors == 0 &&
        result.effective_sample_rate_hz >= ((float)SDACS_SAMPLE_RATE_HZ * SDACS_CAPTURE_MIN_EFFECTIVE_SR_RATIO);

    ESP_LOGI(TAG,
             "I2S preflight attempt=%u rx_mode=%s selected_samples=%u expected=%u elapsed_ms=%u effective_selected_sr=%.2f raw_words=%" PRIu64 " effective_raw_word_rate=%.2f read_calls=%u successful_reads=%u timeouts=%u immediate_timeouts=%u errors=%u avg_read_us=%.2f avg_timeout_us=%.2f pass=%s",
             (unsigned)result.attempt,
             SDACS_I2S_RX_MODE_LABEL,
             (unsigned)result.samples,
             (unsigned)result.expected_samples,
             (unsigned)result.elapsed_ms,
             (double)result.effective_sample_rate_hz,
             result.raw_words_read,
             (double)result.effective_raw_word_rate_hz,
             (unsigned)result.read_calls,
             (unsigned)result.successful_reads,
             (unsigned)result.timeouts,
             (unsigned)result.timeout_immediate_count,
             (unsigned)result.errors,
             (double)result.avg_read_elapsed_us,
             (double)result.avg_timeout_elapsed_us,
             result.pass ? "true" : "false");

    if (out_result) {
        *out_result = result;
    }

    (void)state;
    return result.pass;
}

static void capture_task_run(void *arg)
{
    capture_task_state_t *state = (capture_task_state_t *)arg;
    size_t samples_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = 0;
    int64_t window_start_us = 0;
    int64_t next_publish_us = 0;
    const int64_t feature_interval_us = 1000000LL;
    uint32_t samples_written = 0;
    uint32_t total_samples = 0;
    uint32_t feature_seq = 0;
    uint32_t window_audio_read_timeouts = 0;
    uint32_t window_audio_read_errors = 0;
    capture_io_stats_t io_stats = {0};

    static int32_t read_buf[SDACS_I2S_FRAMES_PER_READ];
    static int32_t chunk[SDACS_AUDIO_CHUNK_SAMPLES];
    size_t chunk_fill = 0;
    bool fatal_error = false;
    char verify_reason[96] = {0};
    size_t raw_bytes = 0;
    size_t wav_bytes = 0;
    size_t csv_bytes = 0;
    esp_err_t err = ESP_OK;
    bool capture_valid = true;
    bool capture_sample_rate_ok = true;
    char capture_audio_error[32] = "";

    capture_set_state(state, SDACS_MODE_ARMED, "capture command accepted");

    ESP_LOGI(TAG, "Waiting for WiFi+MQTT before delayed capture...");
    esp_err_t werr = wifi_mqtt_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not ready (%s). Capture will still drain I2S.", esp_err_to_name(werr));
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Starting delayed capture.");
    }

    ESP_LOGI(TAG, "Delay countdown: %u ms", (unsigned)state->ctx.delay_ms);
    if (state->ctx.delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(state->ctx.delay_ms));
    }

    ESP_LOGI(TAG, "Capture start request=%s record_seconds=%u storage_mode=%s sd_writes=%s",
             state->request_id,
             (unsigned)state->ctx.record_seconds,
             state->ctx.storage_mode,
             state->ctx.sd_writes_enabled ? "true" : "false");

    if (state->ctx.sd_writes_enabled) {
        err = run_storage_create_session(state->ctx.storage, state->node_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SD capture session: %s", esp_err_to_name(err));
            capture_set_state(state, SDACS_MODE_ERROR, "failed to create SD capture session");
            goto done;
        }

        ESP_LOGI(TAG, "SD write started: %s", state->ctx.storage->run_dir);
    } else {
        ESP_LOGI(TAG, "SD writes disabled; running MQTT/features-only capture");
    }

    fft_metrics_reset();
    audio_input_reset_counters();

#if SDACS_I2S_PREFLIGHT_ENABLED
    esp_err_t prep_err = audio_input_prepare_for_capture();
    if (prep_err != ESP_OK) {
        i2s_preflight_result_t prep_result = {0};
        device_state_set(SDACS_MODE_ERROR);
        capture_publish_preflight_failed(
            state,
            &prep_result,
            0,
            "i2s_prepare_failed",
            esp_err_to_name(prep_err)
        );
        ESP_LOGE(TAG, "capture rejected: I2S prepare failed: %s", esp_err_to_name(prep_err));
        goto done;
    }

    audio_input_set_raw_diagnostics_enabled(false);
    i2s_preflight_result_t preflight_result = {0};
    bool preflight_ok = capture_run_i2s_preflight(
        state,
        read_buf,
        1,
        &preflight_result
    );

    if (!preflight_ok && SDACS_I2S_PREFLIGHT_RECOVERY_RETRIES > 0) {
        ESP_LOGW(TAG,
                 "I2S preflight attempt 1 failed; attempting RX recovery before retry");
        esp_err_t rec_err = audio_input_recover_rx("preflight_attempt_1_failed");
        if (rec_err == ESP_OK) {
            preflight_ok = capture_run_i2s_preflight(
                state,
                read_buf,
                2,
                &preflight_result
            );
            if (preflight_ok) {
                audio_input_counters_t counters = {0};
                audio_input_get_counters(&counters);
                ESP_LOGW(TAG,
                         "I2S preflight recovered: preflight_recovered=true preflight_attempts=2 i2s_rx_restarts=%u",
                         (unsigned)counters.rx_restarts);
            }
        } else {
            ESP_LOGE(TAG, "I2S recovery failed: %s", esp_err_to_name(rec_err));
        }
    }

    fft_metrics_reset();
    audio_input_reset_counters();
    audio_input_set_raw_diagnostics_enabled(true);
    audio_input_reset_raw_diagnostics();
    if (!preflight_ok) {
        device_state_set(SDACS_MODE_ERROR);
        capture_publish_preflight_failed(
            state,
            &preflight_result,
            preflight_result.attempt ? preflight_result.attempt : 1U,
            "i2s_preflight_failed",
            "capture rejected: I2S preflight failed after recovery"
        );
        ESP_LOGE(TAG, "capture rejected: I2S preflight failed after recovery");
        goto done;
    }
#else
    audio_input_set_raw_diagnostics_enabled(true);
    audio_input_reset_raw_diagnostics();
#endif

    capture_set_state(state, SDACS_MODE_CAPTURING,
                      state->ctx.sd_writes_enabled ? "capture started" : "capture started: MQTT/features-only mode");
    (void)temp_humidity_publish_latest_once("capture_start");
#if SDACS_FUEL_GAUGE_ENABLED
    (void)fuel_gauge_publish_latest_once("capture_start");
#endif

    start_us = esp_timer_get_time();
    end_us = start_us + ((int64_t)state->ctx.record_seconds * 1000000LL);
    window_start_us = start_us;
    next_publish_us = start_us + feature_interval_us;

    while (esp_timer_get_time() < end_us) {
        err = audio_input_read_s24(
            read_buf,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );
        io_stats.total_i2s_reads++;
        if (err == ESP_ERR_TIMEOUT) {
            io_stats.audio_read_timeouts++;
            io_stats.consecutive_timeouts++;
            window_audio_read_timeouts++;
            samples_read = 0;
        } else if (err != ESP_OK) {
            io_stats.audio_read_errors++;
            io_stats.consecutive_timeouts = 0;
            window_audio_read_errors++;
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            fatal_error = true;
            break;
        } else {
            io_stats.successful_i2s_reads++;
            io_stats.consecutive_timeouts = 0;
        }

        if (samples_read > 0) {
            fft_metrics_push_samples(read_buf, samples_read);
            fft_metrics_accumulate_block(read_buf, samples_read);
            total_samples += (uint32_t)samples_read;

            for (size_t i = 0; i < samples_read; ++i) {
                chunk[chunk_fill++] = read_buf[i];
                if (chunk_fill >= SDACS_AUDIO_CHUNK_SAMPLES) {
                    if (state->ctx.sd_writes_enabled) {
                        if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
                            ESP_LOGW(TAG, "Failed SD append for raw chunk");
                            fatal_error = true;
                            break;
                        }
                        samples_written += (uint32_t)chunk_fill;
                    }
                    chunk_fill = 0;
                }
            }
        }
        if (fatal_error) {
            break;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_publish_us) {
            int64_t window_elapsed_us = now_us - window_start_us;
            uint32_t window_elapsed_ms = (uint32_t)(window_elapsed_us / 1000LL);
            uint32_t expected_samples = (uint32_t)(((int64_t)SDACS_SAMPLE_RATE_HZ * window_elapsed_us) / 1000000LL);
            float window_effective_sr = 0.0f;
            audio_metrics_t metrics = {0};
            bool have_metrics = fft_metrics_compute_and_reset(&metrics, state->ctx.cal_offset_db);
            if (!have_metrics) {
                metrics.dbfs = -120.0f;
                metrics.laeq_db = metrics.dbfs + state->ctx.cal_offset_db;
                metrics.peak_db = metrics.laeq_db;
                metrics.sample_count = 0;
            }
            {
                window_effective_sr = window_elapsed_us > 0
                    ? ((float)metrics.sample_count * 1000000.0f) / (float)window_elapsed_us
                    : 0.0f;
                bool sample_rate_ok = window_effective_sr >=
                    ((float)SDACS_SAMPLE_RATE_HZ * SDACS_CAPTURE_MIN_EFFECTIVE_SR_RATIO);
                char audio_error[32] = "";
                if (window_audio_read_timeouts > SDACS_I2S_MAX_TIMEOUTS_PER_WINDOW) {
                    snprintf(audio_error, sizeof(audio_error), "%s", "i2s_timeouts");
                } else if (!sample_rate_ok) {
                    snprintf(audio_error, sizeof(audio_error), "%s", "low_effective_sample_rate");
                } else if (window_audio_read_errors > 0) {
                    snprintf(audio_error, sizeof(audio_error), "%s", "i2s_read_failed");
                }
                audio_input_debug_t i2s_dbg = {0};
                audio_input_raw_diagnostics_t raw_diag = {0};
                temp_humidity_reading_t th = {0};
                float temp_c = NAN;
                float humidity = NAN;
                uint32_t seq = ++feature_seq;
                const char *capture_state = device_state_to_str(device_state_get());
                (void)audio_input_get_last_debug(&i2s_dbg);
                (void)audio_input_get_raw_diagnostics(&raw_diag);
                if (temp_humidity_get_latest(&th)) {
                    temp_c = th.temp_c;
                    humidity = th.rh_percent;
                }
                fuel_gauge_reading_t batt = {0};
                bool batt_valid = false;
#if SDACS_FUEL_GAUGE_ENABLED
                batt_valid = fuel_gauge_get_latest(&batt);
#endif

                metrics_record_t record = {0};
                time_sync_get_iso8601(record.timestamp, sizeof(record.timestamp));
                record.timestamp_us = (uint64_t)now_us;
                strncpy(record.node_id, state->ctx.node_id, sizeof(record.node_id) - 1);
                strncpy(record.fw_version, SDACS_FW_VERSION, sizeof(record.fw_version) - 1);
                strncpy(record.capture_state, capture_state, sizeof(record.capture_state) - 1);
                record.record_seconds = state->ctx.record_seconds;
                record.seq = seq;
                record.n = metrics.sample_count;
                record.p2p_raw = metrics.p2p_raw;
                record.zeros = metrics.zeros;
                record.dbfs = metrics.dbfs;
                // SPL estimates are only as accurate as the active calibration offset.
                record.db_spl = metrics.laeq_db;
                record.peak_db_spl = metrics.peak_db;
                record.cal_offset_db = state->ctx.cal_offset_db;
                record.rms = metrics.rms_norm;
                record.temp_c = temp_c;
                record.rh_percent = humidity;
                record.fft_peak_hz = metrics.fft_peak_hz;
                record.fft_low_ratio = metrics.fft_low_ratio;
                record.fft_mid_ratio = metrics.fft_mid_ratio;
                record.fft_high_ratio = metrics.fft_high_ratio;
                record.fft_total_energy = metrics.fft_total_energy;
                if (state->ctx.sd_writes_enabled) {
                    (void)run_storage_append_metrics(state->ctx.storage, &record);
                }

                sdacs_features_t features = {
                    .seq = seq,
                    .t_us = (uint64_t)now_us,
                    .timestamp_us = (uint64_t)now_us,
                    .record_seconds = state->ctx.record_seconds,
                    .rms = metrics.rms_norm,
                    .dbfs = metrics.dbfs,
                    // SPL estimates are only as accurate as the active calibration offset.
                    .db_spl = metrics.laeq_db,
                    .peak_db_spl = metrics.peak_db,
                    .cal_offset_db = state->ctx.cal_offset_db,
                    .f_peak_hz = metrics.fft_peak_hz,
                    .fft_low_ratio = metrics.fft_low_ratio,
                    .fft_mid_ratio = metrics.fft_mid_ratio,
                    .fft_high_ratio = metrics.fft_high_ratio,
                    .fft_total_energy = metrics.fft_total_energy,
                    .p2p_raw = metrics.p2p_raw,
                    .zeros = (int)metrics.zeros,
                    .n = metrics.sample_count,
                    .window_elapsed_ms = window_elapsed_ms,
                    .expected_samples = expected_samples,
                    .effective_sample_rate_hz = window_effective_sr,
                    .sample_rate_ok = sample_rate_ok,
                    .temp_c = temp_c,
                    .rh_percent = humidity,
                    .batt_soc_percent = batt_valid ? batt.soc_percent : NAN,
                    .batt_voltage_v = batt_valid ? batt.voltage_v : NAN,
                    .batt_charge_rate_pct_per_hr = batt_valid ? batt.charge_rate_percent_per_hr : NAN,
                    .batt_valid = batt_valid,
                    .sd_enabled = state->ctx.sd_enabled,
                    .sd_writes_enabled = state->ctx.sd_writes_enabled,
                    .storage_mounted = state->ctx.sd_writes_enabled ? run_storage_is_mounted(state->ctx.storage) : false,
                    .i2s_sample_rate_hz = SDACS_SAMPLE_RATE_HZ,
                    .i2s_data_bits = 32,
                    .i2s_valid_bits = SDACS_MIC_VALID_BITS,
                    .i2s_sample_conversion_mode = SDACS_I2S_SAMPLE_CONVERSION_MODE,
                    .raw_diag = raw_diag,
                    .audio_read_errors = window_audio_read_errors,
                    .audio_read_timeouts = window_audio_read_timeouts,
                    .consecutive_timeouts = io_stats.consecutive_timeouts,
                    .total_i2s_reads = io_stats.total_i2s_reads,
                    .successful_i2s_reads = io_stats.successful_i2s_reads,
                    .err = audio_error[0] ? 1U : 0U,
                };
                strncpy(features.node_id, state->ctx.node_id, sizeof(features.node_id) - 1);
                features.node_id[sizeof(features.node_id) - 1] = '\0';
                strncpy(features.capture_state, capture_state, sizeof(features.capture_state) - 1);
                features.capture_state[sizeof(features.capture_state) - 1] = '\0';
                strncpy(features.storage_mode, state->ctx.storage_mode, sizeof(features.storage_mode) - 1);
                features.storage_mode[sizeof(features.storage_mode) - 1] = '\0';
                strncpy(features.i2s_frame_mode, "stereo", sizeof(features.i2s_frame_mode) - 1);
                features.i2s_frame_mode[sizeof(features.i2s_frame_mode) - 1] = '\0';
                strncpy(features.i2s_rx_mode, SDACS_I2S_RX_MODE_LABEL, sizeof(features.i2s_rx_mode) - 1);
                features.i2s_rx_mode[sizeof(features.i2s_rx_mode) - 1] = '\0';
                strncpy(features.i2s_selected_slot, SDACS_I2S_SELECTED_SLOT_LABEL, sizeof(features.i2s_selected_slot) - 1);
                features.i2s_selected_slot[sizeof(features.i2s_selected_slot) - 1] = '\0';
                strncpy(features.i2s_slot_mask, SDACS_I2S_SLOT_MASK_LABEL, sizeof(features.i2s_slot_mask) - 1);
                features.i2s_slot_mask[sizeof(features.i2s_slot_mask) - 1] = '\0';
                strncpy(features.i2s_sample_conversion,
                        SDACS_I2S_SAMPLE_CONVERSION_LABEL,
                        sizeof(features.i2s_sample_conversion) - 1);
                features.i2s_sample_conversion[sizeof(features.i2s_sample_conversion) - 1] = '\0';
                strncpy(features.storage_error,
                        state->ctx.sd_writes_enabled ? run_storage_last_error_name(state->ctx.storage) : "",
                        sizeof(features.storage_error) - 1);
                features.storage_error[sizeof(features.storage_error) - 1] = '\0';
                strncpy(features.storage_error_detail,
                        state->ctx.sd_writes_enabled ? run_storage_last_error_detail(state->ctx.storage) : "",
                        sizeof(features.storage_error_detail) - 1);
                features.storage_error_detail[sizeof(features.storage_error_detail) - 1] = '\0';
                strncpy(features.audio_error, audio_error, sizeof(features.audio_error) - 1);
                features.audio_error[sizeof(features.audio_error) - 1] = '\0';
                if (!wifi_mqtt_try_send(&features)) {
                    io_stats.mqtt_feature_drops++;
                    ESP_LOGW(TAG, "features queue full; dropped seq=%u", (unsigned)features.seq);
                }

                ESP_LOGI(TAG,
                         "Feature row seq=%u conversion=%s n=%u expected=%u dbfs=%.2f p2p_raw=%" PRId32 " f_peak=%.1f sample_rate_ok=%s window_ms=%u eff_sr=%.2f timeouts=%u audio_error=%s",
                         (unsigned)seq,
                         SDACS_I2S_SAMPLE_CONVERSION_LABEL,
                         (unsigned)metrics.sample_count,
                         (unsigned)expected_samples,
                         (double)metrics.dbfs,
                         metrics.p2p_raw,
                         (double)metrics.fft_peak_hz,
                         sample_rate_ok ? "true" : "false",
                         (unsigned)window_elapsed_ms,
                         (double)window_effective_sr,
                         (unsigned)window_audio_read_timeouts,
                         audio_error[0] ? audio_error : "");

                ESP_LOGI(TAG,
                         "Feature row request=%s n=%u expected=%u dbfs=%.2f p2p_raw=%" PRId32 " zeros=%u f_peak=%.1f storage_mode=%s err=%u",
                         state->request_id,
                         (unsigned)metrics.sample_count,
                         (unsigned)expected_samples,
                         (double)metrics.dbfs,
                         metrics.p2p_raw,
                         (unsigned)metrics.zeros,
                         (double)metrics.fft_peak_hz,
                         state->ctx.storage_mode,
                         (unsigned)features.err);

                ESP_LOGI(TAG,
                         "Audio feature debug: timestamp_us=%" PRIu64 " capture_state=%s record_seconds=%u cal_offset_db=%.2f raw0=0x%08" PRIX32 " s0=%" PRId32 " min=%" PRId32 " max=%" PRId32 " p2p_raw=%" PRId32 " zeros=%u rms=%.6f dbfs=%.2f db_spl=%.2f peak_db_spl=%.2f f_peak_hz=%.1f low_ratio=%.4f mid_ratio=%.4f high_ratio=%.4f fft_total_energy=%.6e sample_count=%u",
                         (uint64_t)now_us,
                         capture_state,
                         (unsigned)state->ctx.record_seconds,
                         (double)state->ctx.cal_offset_db,
                         i2s_dbg.raw0,
                         i2s_dbg.sample0,
                         i2s_dbg.min_sample,
                         i2s_dbg.max_sample,
                         metrics.p2p_raw,
                         (unsigned)metrics.zeros,
                         (double)metrics.rms_norm,
                         (double)metrics.dbfs,
                         (double)metrics.laeq_db,
                         (double)metrics.peak_db,
                         (double)metrics.fft_peak_hz,
                         (double)metrics.fft_low_ratio,
                         (double)metrics.fft_mid_ratio,
                         (double)metrics.fft_high_ratio,
                         (double)metrics.fft_total_energy,
                         (unsigned)metrics.sample_count);

#if SDACS_ENABLE_RAW_SAMPLE_DIAGNOSTICS
                ESP_LOGI(TAG,
                         "RAW DIAG seq=%u conversion=%s n=%u f_peak=%.1f current_p2p=%" PRId32 " current_dbfs=%.2f shift8_p2p=%" PRId32 " shift8_dbfs=%.2f low24_p2p=%" PRId32 " low24_dbfs=%.2f raw0=0x%08" PRIX32,
                         (unsigned)seq,
                         SDACS_I2S_SAMPLE_CONVERSION_LABEL,
                         (unsigned)metrics.sample_count,
                         (double)metrics.fft_peak_hz,
                         raw_diag.current.p2p,
                         (double)raw_diag.current.dbfs,
                         raw_diag.shift8.p2p,
                         (double)raw_diag.shift8.dbfs,
                         raw_diag.low24.p2p,
                         (double)raw_diag.low24.dbfs,
                         raw_diag.raw_word0);
#endif

                ESP_LOGI(TAG, "LAeq=%.2f dB peak=%.2f dB written=%u",
                         metrics.laeq_db, metrics.peak_db, (unsigned)samples_written);
            }

            window_audio_read_timeouts = 0;
            window_audio_read_errors = 0;
            audio_input_reset_raw_diagnostics();
            window_start_us = now_us;
            next_publish_us = now_us + feature_interval_us;
        }
    }

    if (chunk_fill > 0 && state->ctx.sd_writes_enabled) {
        if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
            ESP_LOGW(TAG, "Failed final SD append");
            fatal_error = true;
        } else {
            samples_written += (uint32_t)chunk_fill;
        }
    }

    temp_humidity_reading_t final_th = {0};
    (void)temp_humidity_read_once(
        SDACS_TEMP_HUMIDITY_I2C_PORT,
        SDACS_TEMP_HUMIDITY_ADDR,
        &final_th
    );
#if SDACS_FUEL_GAUGE_ENABLED
    fuel_gauge_reading_t final_batt = {0};
    (void)fuel_gauge_read_once(
        SDACS_FUEL_GAUGE_I2C_PORT,
        SDACS_FUEL_GAUGE_ADDR,
        &final_batt
    );
#endif

    capture_set_state(state, SDACS_MODE_FINALIZING, "capture finalizing");

    if (state->ctx.sd_writes_enabled) {
        err = run_storage_convert_raw_to_wav(state->ctx.storage, SDACS_SAMPLE_RATE_HZ);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WAV conversion failed: %s", esp_err_to_name(err));
            fatal_error = true;
        } else {
            ESP_LOGI(TAG, "WAV conversion complete");
        }

        run_storage_refresh_timestamps(state->ctx.storage);
        ESP_LOGI(TAG, "SD finalization complete");
    } else {
        raw_bytes = 0;
        wav_bytes = 0;
        csv_bytes = 0;
        ESP_LOGI(TAG, "Skipping SD finalization in MQTT/features-only mode");
    }

    ESP_LOGI(TAG, "Recording complete: %.2f s, chunk_msgs=%u",
             (float)(esp_timer_get_time() - start_us) / 1000000.0f,
             (unsigned)samples_written);

    if (state->ctx.sd_writes_enabled) {
        run_storage_verify(state->ctx.storage);
        err = run_storage_verify_capture(
            state->ctx.storage,
            verify_reason,
            sizeof(verify_reason),
            &raw_bytes,
            &wav_bytes,
            &csv_bytes
        );
        if (err != ESP_OK) {
            fatal_error = true;
        }
    }

    int64_t elapsed_us = esp_timer_get_time() - start_us;
    int64_t elapsed_ms = elapsed_us / 1000LL;
    float effective_sample_rate_hz = elapsed_us > 0
        ? ((float)total_samples * 1000000.0f) / (float)elapsed_us
        : 0.0f;
    uint32_t expected_total_samples = state->ctx.record_seconds * SDACS_SAMPLE_RATE_HZ;
    capture_sample_rate_ok = effective_sample_rate_hz >=
        ((float)SDACS_SAMPLE_RATE_HZ * SDACS_CAPTURE_MIN_EFFECTIVE_SR_RATIO);
    if (!capture_sample_rate_ok) {
        capture_valid = false;
        snprintf(capture_audio_error, sizeof(capture_audio_error), "%s", "low_effective_sample_rate");
    } else if (io_stats.audio_read_errors > 0) {
        capture_valid = false;
        snprintf(capture_audio_error, sizeof(capture_audio_error), "%s", "i2s_read_failed");
    } else if (io_stats.audio_read_timeouts > 0 &&
               io_stats.successful_i2s_reads == 0) {
        capture_valid = false;
        snprintf(capture_audio_error, sizeof(capture_audio_error), "%s", "i2s_timeouts");
    }

    ESP_LOGI(TAG,
             "Capture complete request=%s storage_mode=%s total_samples=%u expected=%u elapsed_ms=%" PRIi64 " effective_sr=%.2f sample_rate_ok=%s audio_error=%s",
             state->request_id,
             state->ctx.storage_mode,
             (unsigned)total_samples,
             (unsigned)expected_total_samples,
             (int64_t)elapsed_ms,
             (double)effective_sample_rate_hz,
             capture_sample_rate_ok ? "true" : "false",
             capture_audio_error);

    if (fatal_error) {
        capture_set_state(state, SDACS_MODE_ERROR,
                          verify_reason[0] ? verify_reason : "capture failed");
    } else if (!capture_valid) {
        capture_set_state(state, SDACS_MODE_ERROR,
                          "capture failed: effective sample rate too low");
        capture_publish_complete(
            state,
            raw_bytes,
            wav_bytes,
            csv_bytes,
            total_samples,
            expected_total_samples,
            elapsed_ms,
            effective_sample_rate_hz,
            capture_sample_rate_ok,
            false,
            &io_stats,
            capture_audio_error,
            "error",
            "capture failed: effective sample rate too low");
    } else {
        capture_set_state(state, SDACS_MODE_COMPLETE, "capture complete");
        (void)temp_humidity_publish_latest_once("capture_complete");
#if SDACS_FUEL_GAUGE_ENABLED
        (void)fuel_gauge_publish_latest_once("capture_complete");
#endif
        capture_publish_complete(
            state,
            raw_bytes,
            wav_bytes,
            csv_bytes,
            total_samples,
            expected_total_samples,
            elapsed_ms,
            effective_sample_rate_hz,
            capture_sample_rate_ok,
            true,
            &io_stats,
            "",
            "complete",
            state->ctx.sd_writes_enabled ? "capture complete" : "capture complete: MQTT/features-only mode");
    }

done:
    vTaskDelay(pdMS_TO_TICKS(250));
    device_state_set(SDACS_MODE_IDLE);
    ESP_LOGI(TAG, "Capture state: idle (ready)");
    free(state);
    vTaskDelete(NULL);
}

esp_err_t capture_task_start(const capture_context_t *ctx)
{
    capture_task_state_t *state = NULL;
    BaseType_t ok;
    int topic_len = 0;

    if (!ctx || !ctx->storage || !ctx->node_id || !ctx->base_topic) {
        return ESP_ERR_INVALID_ARG;
    }

    state = calloc(1, sizeof(*state));
    if (!state) {
        return ESP_ERR_NO_MEM;
    }

    state->ctx = *ctx;
    strncpy(state->node_id, ctx->node_id, sizeof(state->node_id) - 1);
    strncpy(state->base_topic, ctx->base_topic, sizeof(state->base_topic) - 1);
    strncpy(state->request_id,
            (ctx->request_id && ctx->request_id[0] != '\0') ? ctx->request_id : "manual",
            sizeof(state->request_id) - 1);
    state->ctx.node_id = state->node_id;
    state->ctx.base_topic = state->base_topic;
    state->ctx.request_id = state->request_id;

    topic_len = snprintf(state->status_topic, sizeof(state->status_topic), "%s/status", ctx->base_topic);
    if (topic_len <= 0 || topic_len >= (int)sizeof(state->status_topic)) {
        free(state);
        return ESP_ERR_INVALID_SIZE;
    }
    topic_len = snprintf(state->complete_topic, sizeof(state->complete_topic), "%s/capture_complete", ctx->base_topic);
    if (topic_len <= 0 || topic_len >= (int)sizeof(state->complete_topic)) {
        free(state);
        return ESP_ERR_INVALID_SIZE;
    }

    ok = xTaskCreatePinnedToCore(
        capture_task_run,
        "capture_task",
        SDACS_CAPTURE_TASK_STACK_SIZE,
        state,
        SDACS_CAPTURE_TASK_PRIORITY,
        NULL,
        SDACS_CAPTURE_TASK_CORE_ID
    );
    if (ok != pdPASS) {
        free(state);
        return ESP_FAIL;
    }

    return ESP_OK;
}
