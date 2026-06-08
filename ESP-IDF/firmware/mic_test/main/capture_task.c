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

static void capture_publish_status(capture_task_state_t *state,
                                   sdacs_mode_t mode,
                                   const char *message)
{
    char payload[384];
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
        "\"message\":\"%s\""
        "}",
        state->node_id,
        (int64_t)esp_timer_get_time(),
        SDACS_FW_VERSION,
        state->request_id,
        device_state_to_str(mode),
        (unsigned)state->ctx.delay_ms,
        (unsigned)state->ctx.record_seconds,
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

static void capture_publish_complete(capture_task_state_t *state,
                                     size_t raw_bytes,
                                     size_t wav_bytes,
                                     size_t csv_bytes)
{
    char payload[1024];
    char timestamp[32];
    int len = 0;

    if (!state || state->complete_topic[0] == '\0') {
        return;
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
        "\"state\":\"complete\","
        "\"record_seconds\":%u,"
        "\"raw_path\":\"%s\","
        "\"wav_path\":\"%s\","
        "\"metrics_path\":\"%s\","
        "\"raw_bytes\":%u,"
        "\"wav_bytes\":%u,"
        "\"metrics_bytes\":%u,"
        "\"timestamp\":\"%s\""
        "}",
        state->node_id,
        SDACS_FW_VERSION,
        state->request_id,
        (unsigned)state->ctx.record_seconds,
        state->ctx.storage->raw_path,
        state->ctx.storage->wav_path,
        state->ctx.storage->csv_path,
        (unsigned)raw_bytes,
        (unsigned)wav_bytes,
        (unsigned)csv_bytes,
        timestamp
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

static void capture_task_run(void *arg)
{
    capture_task_state_t *state = (capture_task_state_t *)arg;
    size_t samples_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = 0;
    int64_t next_metrics_us = 0;
    uint32_t samples_written = 0;
    uint32_t feature_seq = 0;

    static int32_t read_buf[SDACS_I2S_FRAMES_PER_READ];
    static int32_t chunk[SDACS_AUDIO_CHUNK_SAMPLES];
    size_t chunk_fill = 0;
    bool fatal_error = false;
    char verify_reason[96];
    size_t raw_bytes = 0;
    size_t wav_bytes = 0;
    size_t csv_bytes = 0;
    esp_err_t err = ESP_OK;

    capture_set_state(state, SDACS_MODE_ARMED, "capture command accepted");

    ESP_LOGI(TAG, "Waiting for WiFi+MQTT before delayed capture...");
    esp_err_t werr = wifi_mqtt_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not ready (%s). Continuing with local SD capture.", esp_err_to_name(werr));
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Starting delayed SD-first capture.");
    }

    ESP_LOGI(TAG, "Delay countdown: %u ms", (unsigned)state->ctx.delay_ms);
    if (state->ctx.delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(state->ctx.delay_ms));
    }

    err = run_storage_create_session(state->ctx.storage, state->node_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create SD capture session: %s", esp_err_to_name(err));
        capture_set_state(state, SDACS_MODE_ERROR, "failed to create SD capture session");
        goto done;
    }

    ESP_LOGI(TAG, "SD write started: %s", state->ctx.storage->run_dir);
    capture_set_state(state, SDACS_MODE_CAPTURING, "capture started");
    (void)temp_humidity_publish_latest_once("capture_start");
#if SDACS_FUEL_GAUGE_ENABLED
    (void)fuel_gauge_publish_latest_once("capture_start");
#endif

    start_us = esp_timer_get_time();
    end_us = start_us + ((int64_t)state->ctx.record_seconds * 1000000LL);
    next_metrics_us = start_us + 1000000LL;

    while (esp_timer_get_time() < end_us) {
        err = audio_input_read_s24(
            read_buf,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            fatal_error = true;
            break;
        }
        if (samples_read == 0) {
            continue;
        }

        fft_metrics_push_samples(read_buf, samples_read);
        fft_metrics_accumulate_block(read_buf, samples_read);

        for (size_t i = 0; i < samples_read; ++i) {
            chunk[chunk_fill++] = read_buf[i];
            if (chunk_fill >= SDACS_AUDIO_CHUNK_SAMPLES) {
                if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
                    ESP_LOGW(TAG, "Failed SD append for raw chunk");
                    fatal_error = true;
                    break;
                }

                samples_written += (uint32_t)chunk_fill;
                chunk_fill = 0;
            }
        }
        if (fatal_error) {
            break;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_metrics_us) {
            audio_metrics_t metrics = {0};
            if (fft_metrics_compute_and_reset(&metrics, state->ctx.cal_offset_db)) {
                audio_input_debug_t i2s_dbg = {0};
                temp_humidity_reading_t th = {0};
                float temp_c = NAN;
                float humidity = NAN;
                (void)audio_input_get_last_debug(&i2s_dbg);
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
                strncpy(record.node_id, state->ctx.node_id, sizeof(record.node_id) - 1);
                record.laeq_db = metrics.laeq_db;
                record.peak_db = metrics.peak_db;
                record.dbfs = metrics.dbfs;
                record.rms = metrics.rms_norm;
                record.temp_c = temp_c;
                record.humidity = humidity;
                record.fft_peak_hz = metrics.fft_peak_hz;
                (void)run_storage_append_metrics(state->ctx.storage, &record);

                sdacs_features_t features = {
                    .seq = ++feature_seq,
                    .t_us = (uint64_t)now_us,
                    .rms = metrics.rms_norm,
                    .dbfs = metrics.dbfs,
                    .db_spl = metrics.laeq_db,
                    .f_peak_hz = metrics.fft_peak_hz,
                    .p2p_raw = metrics.p2p_raw,
                    .zeros = (int)metrics.zeros,
                    .n = metrics.sample_count,
                    .temp_c = temp_c,
                    .rh_percent = humidity,
                    .batt_soc_percent = batt_valid ? batt.soc_percent : NAN,
                    .batt_voltage_v = batt_valid ? batt.voltage_v : NAN,
                    .batt_charge_rate_pct_per_hr = batt_valid ? batt.charge_rate_percent_per_hr : NAN,
                    .batt_valid = batt_valid,
                    .err = (th.valid ? th.error_count : 0U) + (batt_valid ? batt.error_count : 0U),
                };
                strncpy(features.node_id, state->ctx.node_id, sizeof(features.node_id) - 1);
                features.node_id[sizeof(features.node_id) - 1] = '\0';
                if (!wifi_mqtt_try_send(&features)) {
                    ESP_LOGW(TAG, "features queue full; dropped seq=%u", (unsigned)features.seq);
                }

                ESP_LOGI(TAG,
                         "Audio feature debug: raw0=0x%08" PRIX32 " s0=%" PRId32 " min=%" PRId32 " max=%" PRId32 " p2p_raw=%" PRId32 " zeros=%u rms=%.6f dbfs=%.2f db_spl=%.2f f_peak_hz=%.1f sample_count=%u",
                         i2s_dbg.raw0,
                         i2s_dbg.sample0,
                         i2s_dbg.min_sample,
                         i2s_dbg.max_sample,
                         metrics.p2p_raw,
                         (unsigned)metrics.zeros,
                         (double)metrics.rms_norm,
                         (double)metrics.dbfs,
                         (double)metrics.laeq_db,
                         (double)metrics.fft_peak_hz,
                         (unsigned)metrics.sample_count);

                ESP_LOGI(TAG, "LAeq=%.2f dB peak=%.2f dB written=%u",
                         metrics.laeq_db, metrics.peak_db, (unsigned)samples_written);
            }

            next_metrics_us += 1000000LL;
        }
    }

    if (chunk_fill > 0) {
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

    err = run_storage_convert_raw_to_wav(state->ctx.storage, SDACS_SAMPLE_RATE_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WAV conversion failed: %s", esp_err_to_name(err));
        fatal_error = true;
    } else {
        ESP_LOGI(TAG, "WAV conversion complete");
    }

    run_storage_refresh_timestamps(state->ctx.storage);
    ESP_LOGI(TAG, "SD finalization complete");

    ESP_LOGI(TAG, "Recording complete: %.2f s, chunk_msgs=%u",
             (float)(esp_timer_get_time() - start_us) / 1000000.0f,
             (unsigned)samples_written);

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

    if (fatal_error) {
        capture_set_state(state, SDACS_MODE_ERROR,
                          verify_reason[0] ? verify_reason : "capture failed");
    } else {
        capture_set_state(state, SDACS_MODE_COMPLETE, "capture complete");
        (void)temp_humidity_publish_latest_once("capture_complete");
#if SDACS_FUEL_GAUGE_ENABLED
        (void)fuel_gauge_publish_latest_once("capture_complete");
#endif
        capture_publish_complete(state, raw_bytes, wav_bytes, csv_bytes);
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
