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
#include "wifi_mqtt.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t ver;
    uint16_t flags;
    uint32_t seq;
    uint64_t t_us;
    uint32_t sample_rate;
    uint32_t n;
} sdacs_audio_hdr_t;

#define SDACS_MAGIC 0x43414453u

typedef struct {
    capture_context_t ctx;
    char audio_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
} capture_task_state_t;

static const char *TAG = "capture_task";

static void capture_task_run(void *arg)
{
    capture_task_state_t *state = (capture_task_state_t *)arg;
    size_t samples_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)state->ctx.record_seconds * 1000000LL);

    sdacs_audio_hdr_t hdr = {
        .magic = SDACS_MAGIC,
        .ver = 1,
        .flags = 0,
        .seq = 0,
        .sample_rate = SDACS_SAMPLE_RATE_HZ,
    };

    static int32_t read_buf[SDACS_I2S_FRAMES_PER_READ];
    static int32_t chunk[SDACS_AUDIO_CHUNK_SAMPLES];
    static uint8_t payload[sizeof(sdacs_audio_hdr_t) + (SDACS_AUDIO_CHUNK_SAMPLES * sizeof(int32_t))];
    size_t chunk_fill = 0;

    // Try WiFi/MQTT with a short timeout; if unavailable, record locally only
    ESP_LOGI(TAG, "Checking WiFi+MQTT availability (timeout=%dms)...", SDACS_WIFI_TIME_SYNC_WAIT_MS);
    esp_err_t werr = wifi_mqtt_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    bool mqtt_available = (werr == ESP_OK);
    
    if (mqtt_available) {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Streaming + SD logging.");
        (void)temp_humidity_publish_latest_once("start");
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT unavailable (%s). Recording to SD only.", esp_err_to_name(werr));
    }

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

                hdr.seq++;
                chunk_fill = 0;
            }
        }

        // Removed real-time metrics calculation - will be done after recording
    }

    audio_input_deinit();
    temp_humidity_stop();
    (void)run_storage_convert_raw_to_wav(state->ctx.storage, SDACS_SAMPLE_RATE_HZ);
    run_storage_refresh_timestamps(state->ctx.storage);

    ESP_LOGI(TAG, "PHASE 1 COMPLETE: Recording done. %.2f s recorded to SD",
             (float)(esp_timer_get_time() - start_us) / 1000000.0f);

    // === PHASE 2: Analysis (read SD, compute metrics) ===
    ESP_LOGI(TAG, "PHASE 2: Starting post-recording analysis...");
    
    audio_metrics_t final_metrics = {0};
    
    // Read the recorded audio file and compute metrics
    FILE *analysis_file = fopen(state->ctx.storage->raw_path, "rb");
    if (analysis_file) {
        int32_t sample_buf[512];
        size_t n;
        while ((n = fread(sample_buf, sizeof(int32_t), 512, analysis_file)) > 0) {
            fft_metrics_push_samples(sample_buf, n);
            fft_metrics_accumulate_block(sample_buf, n);
        }
        fclose(analysis_file);
        ESP_LOGI(TAG, "Audio analysis complete");
    } else {
        ESP_LOGE(TAG, "Failed to open raw file for analysis");
    }

    if (fft_metrics_compute_and_reset(&final_metrics, state->ctx.cal_offset_db)) {
        temp_humidity_reading_t th = {0};
        float temp_c = NAN;
        float humidity = NAN;
        if (temp_humidity_get_latest(&th)) {
            temp_c = th.temp_c;
            humidity = th.rh_percent;
        }

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

        // Send metrics summary via MQTT if available
        if (mqtt_available) {
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

            if (wifi_mqtt_try_send(&feat)) {
                ESP_LOGI(TAG, "Metrics summary sent via MQTT");
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to compute metrics");
    }

    // === PHASE 3: Streaming (send audio to MQTT if available) ===
    if (mqtt_available) {
        ESP_LOGI(TAG, "PHASE 3: Starting post-recording audio streaming...");
        
        FILE *stream_file = fopen(state->ctx.storage->raw_path, "rb");
        if (!stream_file) {
            ESP_LOGE(TAG, "Failed to open raw file for streaming");
        } else {
            fseek(stream_file, 0, SEEK_END);
            long raw_size_l = ftell(stream_file);
            fseek(stream_file, 0, SEEK_SET);
            
            if (raw_size_l > 0) {
                size_t total_samples = (size_t)raw_size_l / sizeof(int32_t);
                size_t samples_streamed = 0;
                uint32_t chunk_seq = 0;
                
                ESP_LOGI(TAG, "Streaming %zu samples in %d-sample chunks", total_samples, SDACS_AUDIO_CHUNK_SAMPLES);
                
                while (samples_streamed < total_samples) {
                    size_t samples_to_read = SDACS_AUDIO_CHUNK_SAMPLES;
                    if (samples_streamed + samples_to_read > total_samples) {
                        samples_to_read = total_samples - samples_streamed;
                    }
                    
                    size_t rd = fread(chunk, sizeof(int32_t), samples_to_read, stream_file);
                    if (rd == 0) {
                        break;
                    }
                    
                    // Send chunk over MQTT
                    hdr.n = (uint32_t)rd;
                    hdr.t_us = (uint64_t)esp_timer_get_time();
                    hdr.seq = chunk_seq;
                    memcpy(payload, &hdr, sizeof(hdr));
                    memcpy(payload + sizeof(hdr), chunk, rd * sizeof(int32_t));
                    
                    esp_err_t err = wifi_mqtt_publish_raw(
                        state->audio_topic,
                        payload,
                        sizeof(hdr) + (rd * sizeof(int32_t)),
                        0,
                        0
                    );
                    if (err != ESP_OK && (chunk_seq % 20u == 0u)) {
                        ESP_LOGW(TAG, "Audio chunk publish dropped: %s", esp_err_to_name(err));
                    }
                    
                    samples_streamed += rd;
                    chunk_seq++;
                    
                    // Small delay to avoid overwhelming MQTT
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                
                ESP_LOGI(TAG, "PHASE 3 COMPLETE: Audio streaming done - %" PRIu32 " chunks sent", chunk_seq);
            }
            
            fclose(stream_file);
        }
    } else {
        ESP_LOGI(TAG, "PHASE 3 SKIPPED: MQTT unavailable");
    }

    run_storage_verify(state->ctx.storage);
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
