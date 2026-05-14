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
#include "fft_metrics.h"
#include "run_storage.h"
#include "sdacs_config.h"
#include "temp_humidity.h"
#include "time_sync.h"
#include "mqtt_publish.h"
#include "wifi_station.h"

typedef struct {
    capture_context_t ctx;
    char audio_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
} capture_task_state_t;

static const char *TAG = "capture_task";

static int32_t wav_s24_to_i32(const uint8_t bytes[3])
{
    int32_t sample = (int32_t)bytes[0] |
                     ((int32_t)bytes[1] << 8) |
                     ((int32_t)bytes[2] << 16);
    if (sample & 0x00800000) {
        sample |= ~0x00FFFFFF;
    }
    return sample;
}

static bool analyze_wav_file(const char *path)
{
    static uint8_t wav_bytes[SDACS_WAV_CHUNK_SIZE * 3];
    static int32_t sample_buf[SDACS_WAV_CHUNK_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open WAV file for analysis: %s", path);
        return false;
    }

    if (fseek(f, 44, SEEK_SET) != 0) {
        fclose(f);
        ESP_LOGE(TAG, "Failed to seek past WAV header: %s", path);
        return false;
    }

    while (1) {
        size_t bytes_read = fread(wav_bytes, 1, sizeof(wav_bytes), f);
        if (bytes_read == 0) {
            break;
        }

        size_t samples_read = bytes_read / 3;
        for (size_t i = 0; i < samples_read; ++i) {
            sample_buf[i] = wav_s24_to_i32(&wav_bytes[i * 3]);
        }
        fft_metrics_push_samples(sample_buf, samples_read);
        fft_metrics_accumulate_block(sample_buf, samples_read);

        if (bytes_read < sizeof(wav_bytes)) {
            break;
        }
    }

    fclose(f);
    return true;
}

static void stream_file_to_mqtt(const char *topic, const char *path)
{
    static uint8_t payload[2048];
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for MQTT streaming: %s", path);
        return;
    }

    uint32_t chunk_seq = 0;
    size_t total_bytes = 0;
    while (1) {
        size_t rd = fread(payload, 1, sizeof(payload), f);
        if (rd == 0) {
            break;
        }

        esp_err_t err = mqtt_publish_raw(topic, payload, rd, 0, 0);
        if (err != ESP_OK && (chunk_seq % 20u == 0u)) {
            ESP_LOGW(TAG, "File stream chunk dropped: %s", esp_err_to_name(err));
        }

        total_bytes += rd;
        chunk_seq++;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    fclose(f);
    ESP_LOGI(TAG, "Streamed %u bytes from %s to %s",
             (unsigned)total_bytes, path, topic);
}

static void capture_task_run(void *arg)
{
    capture_task_state_t *state = (capture_task_state_t *)arg;
    size_t samples_read = 0;
    int64_t start_us = 0;
    int64_t end_us = 0;

    static int32_t read_buf[SDACS_I2S_FRAMES_PER_READ];
    static int32_t chunk[SDACS_AUDIO_CHUNK_SAMPLES];
    size_t chunk_fill = 0;
    temp_humidity_reading_t th = {0};
    float temp_c = NAN;
    float humidity = NAN;

    ESP_LOGI(TAG, "PHASE 1: Recording audio + temp/humidity locally for %" PRIu32 " s",
             state->ctx.record_seconds);
    start_us = esp_timer_get_time();
    end_us = start_us + ((int64_t)state->ctx.record_seconds * 1000000LL);

    while (esp_timer_get_time() < end_us) {
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
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }
        if (samples_read == 0) {
            continue;
        }

        // Record audio to SD only - no processing during recording
        for (size_t i = 0; i < samples_read; ++i) {
            chunk[chunk_fill++] = read_buf[i];
            if (chunk_fill >= SDACS_AUDIO_CHUNK_SAMPLES) {
                if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
                    ESP_LOGW(TAG, "Failed SD append for raw chunk");
                }

                chunk_fill = 0;
            }
        }

        // Removed real-time metrics calculation - will be done after recording
    }

    int64_t recording_end_us = esp_timer_get_time();

    if (chunk_fill > 0) {
        if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
            ESP_LOGW(TAG, "Failed SD append for final raw chunk");
        }
        chunk_fill = 0;
    }

    audio_input_deinit();
    if (temp_humidity_get_latest(&th)) {
        temp_c = th.temp_c;
        humidity = th.rh_percent;
    }
    temp_humidity_stop();
    ESP_LOGI(TAG, "Recording complete; converting raw audio to WAV...");
    int64_t wav_start_us = esp_timer_get_time();
    esp_err_t wav_err = run_storage_convert_raw_to_wav(state->ctx.storage, SDACS_SAMPLE_RATE_HZ);
    int64_t wav_end_us = esp_timer_get_time();
    if (wav_err != ESP_OK) {
        ESP_LOGE(TAG, "WAV conversion failed: %s", esp_err_to_name(wav_err));
    } else {
        ESP_LOGI(TAG, "WAV conversion complete in %.2f s",
                 (float)(wav_end_us - wav_start_us) / 1000000.0f);
    }
    run_storage_refresh_timestamps(state->ctx.storage);

    ESP_LOGI(TAG, "PHASE 1 COMPLETE: Recording done. %.2f s recorded to SD",
             (float)(recording_end_us - start_us) / 1000000.0f);

    // === PHASE 2: Analysis (read SD, compute metrics) ===
    ESP_LOGI(TAG, "PHASE 2: Reading WAV and computing post-recording metrics...");
    
    audio_metrics_t final_metrics = {0};
    
    if (analyze_wav_file(state->ctx.storage->wav_path)) {
        ESP_LOGI(TAG, "Audio analysis complete");
    } else {
        ESP_LOGE(TAG, "Audio analysis failed");
    }

    if (fft_metrics_compute_and_reset(&final_metrics, state->ctx.cal_offset_db)) {
        // Store final metrics record to CSV
        metrics_record_t record = {0};
        time_sync_get_iso8601(record.timestamp, sizeof(record.timestamp));
        strncpy(record.node_id, state->ctx.node_id, sizeof(record.node_id) - 1);
        record.laeq_db = final_metrics.laeq_db;
        record.peak_db = final_metrics.peak_db;
        record.dbfs = final_metrics.dbfs;
        record.rms = final_metrics.rms_norm;
        record.temp_c = temp_c;
        record.humidity = humidity;
        record.fft_peak_hz = final_metrics.fft_peak_hz;
        (void)run_storage_append_metrics(state->ctx.storage, &record);

        ESP_LOGI(TAG, "PHASE 2 COMPLETE: Analysis Results - LAeq=%.2f dB, Peak=%.2f dB, FFT Peak=%.1f Hz",
                 final_metrics.laeq_db, final_metrics.peak_db, final_metrics.fft_peak_hz);
    } else {
        ESP_LOGW(TAG, "Failed to compute metrics");
    }

    run_storage_refresh_timestamps(state->ctx.storage);
    run_storage_verify(state->ctx.storage);

    // === PHASE 3: Streaming (only after files are complete) ===
    ESP_LOGI(TAG, "PHASE 3: Files complete; starting WiFi/MQTT before streaming...");
    esp_err_t net_err = sdacs_wifi_station_start();
    if (net_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi start failed; skipping MQTT stream: %s", esp_err_to_name(net_err));
        free(state);
        vTaskDelete(NULL);
        return;
    }

    time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);

    esp_err_t mqtt_err = mqtt_publish_start();
    if (mqtt_err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT start failed; skipping MQTT stream: %s", esp_err_to_name(mqtt_err));
        free(state);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t werr = mqtt_publish_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    bool mqtt_available = (werr == ESP_OK);
    if (mqtt_available) {
        if (final_metrics.sample_count > 0) {
            sdacs_features_t feat = {0};
            strncpy(feat.node_id, state->ctx.node_id, sizeof(feat.node_id) - 1);
            feat.seq = 0;
            feat.t_us = (uint64_t)esp_timer_get_time();
            feat.n = final_metrics.sample_count;
            feat.rms = final_metrics.rms_norm;
            feat.dbfs = final_metrics.dbfs;
            feat.db_spl = final_metrics.laeq_db;
            feat.f_peak_hz = final_metrics.fft_peak_hz;
            feat.p2p_raw = final_metrics.peak_abs * 2;
            feat.zeros = 0;

            if (mqtt_publish_try_send_features(&feat)) {
                ESP_LOGI(TAG, "Metrics summary queued for MQTT");
            }
        }

        stream_file_to_mqtt(state->audio_topic, state->ctx.storage->wav_path);
        ESP_LOGI(TAG, "PHASE 3 COMPLETE: Post-file MQTT streaming done");
    } else {
        ESP_LOGI(TAG, "PHASE 3 SKIPPED: MQTT unavailable (%s)", esp_err_to_name(werr));
    }

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
    topic_len = snprintf(state->audio_topic, sizeof(state->audio_topic), "%s/audio", ctx->base_topic);
    if (topic_len <= 0 || topic_len >= (int)sizeof(state->audio_topic)) {
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
