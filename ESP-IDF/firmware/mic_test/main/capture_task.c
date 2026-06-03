#include "capture_task.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "config_store.h"
#include "audioAnalysis.h"
#include "recordAudio.h"
#include "sdCard.h"
#include "sdacs_config.h"
#include "tempHumidity.h"
#include "network.h"
#include "mqtt_publish.h"

typedef struct {
    capture_context_t ctx;
    char audio_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
} capture_task_state_t;

static const char *TAG = "capture_task";

#define CAPTURE_TASK_DONE_BIT BIT0
static EventGroupHandle_t s_capture_events = NULL;

static const temp_humidity_reading_t *select_temp_humidity_for_window(
        const temp_humidity_reading_t *history,
        size_t history_count,
        int64_t window_end_us,
        size_t *history_index)
{
    const temp_humidity_reading_t *selected = NULL;
    if (!history || !history_index) {
        return NULL;
    }

    while (*history_index < history_count &&
           history[*history_index].last_sample_time_us <= window_end_us) {
        selected = &history[*history_index];
        (*history_index)++;
    }
    return selected;
}

static bool append_metrics_record(run_storage_t *storage,
                                  const char *node_id,
                                  const temp_humidity_reading_t *th,
                                  const audio_metrics_t *metrics)
{
    if (!storage || !node_id || !metrics) {
        return false;
    }

    metrics_record_t record = {0};
    time_sync_get_iso8601(record.timestamp, sizeof(record.timestamp));
    strncpy(record.node_id, node_id, sizeof(record.node_id) - 1);
    record.laeq_db = metrics->laeq_db;
    record.peak_db = metrics->peak_db;
    record.dbfs = metrics->dbfs;
    record.rms = metrics->rms_norm;
    record.fft_peak_hz = metrics->fft_peak_hz;

    if (th && th->valid) {
        record.temp_c = th->temp_c;
        record.humidity = th->rh_percent;
    } else {
        record.temp_c = NAN;
        record.humidity = NAN;
    }

    return run_storage_append_metrics(storage, &record);
}

static bool analyze_raw_file(const char *path,
                             const char *node_id,
                             const temp_humidity_reading_t *th_history,
                             size_t th_history_count,
                             run_storage_t *storage,
                             float cal_offset_db,
                             int64_t recording_start_us)
{
    static int32_t sample_buf[SDACS_RAW_CHUNK_SIZE];
    static int32_t segment_buf[(SDACS_SAMPLE_RATE_HZ * 500) / 1000];
    int64_t segment_duration_us = 500000LL;
    size_t segment_samples = sizeof(segment_buf) / sizeof(segment_buf[0]);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open RAW file for analysis: %s", path);
        return false;
    }

    size_t segment_fill = 0;
    size_t history_index = 0;
    int64_t next_window_end_us = recording_start_us + segment_duration_us;
    size_t rows_written = 0;
    bool ok = true;

    while (1) {
        size_t samples_read = fread(sample_buf, sizeof(sample_buf[0]),
                                    sizeof(sample_buf) / sizeof(sample_buf[0]), f);
        if (samples_read == 0) {
            break;
        }

        fft_metrics_push_samples(sample_buf, samples_read);
        fft_metrics_accumulate_block(sample_buf, samples_read);

        for (size_t i = 0; i < samples_read; ++i) {
            segment_buf[segment_fill++] = sample_buf[i];
            if (segment_fill >= segment_samples) {
                audio_metrics_t window_metrics = {0};
                if (!fft_metrics_compute_metrics_block(segment_buf, segment_samples, &window_metrics, cal_offset_db)) {
                    ESP_LOGW(TAG, "Failed to compute 500ms window audio metrics");
                    ok = false;
                    break;
                }

                const temp_humidity_reading_t *th = select_temp_humidity_for_window(
                    th_history, th_history_count, next_window_end_us, &history_index);

                if (!append_metrics_record(storage, node_id, th, &window_metrics)) {
                    ESP_LOGW(TAG, "Failed to append 500ms metrics row to CSV");
                    ok = false;
                    break;
                }

                rows_written++;
                segment_fill = 0;
                next_window_end_us += segment_duration_us;
            }
        }

        if (!ok || samples_read < sizeof(sample_buf) / sizeof(sample_buf[0])) {
            break;
        }
    }

    if (ok && segment_fill > 0) {
        audio_metrics_t window_metrics = {0};
        if (fft_metrics_compute_metrics_block(segment_buf, segment_fill, &window_metrics, cal_offset_db)) {
            const temp_humidity_reading_t *th = select_temp_humidity_for_window(
                th_history, th_history_count, next_window_end_us, &history_index);
            if (!append_metrics_record(storage, node_id, th, &window_metrics)) {
                ESP_LOGW(TAG, "Failed to append final partial metrics row to CSV");
                ok = false;
            } else {
                rows_written++;
            }
        } else {
            ESP_LOGW(TAG, "Failed to compute final partial window audio metrics");
            ok = false;
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Wrote %u post-recording metrics rows", (unsigned)rows_written);
    if (ok && rows_written == 0) {
        ESP_LOGW(TAG, "RAW file contained no samples to analyze");
        return false;
    }
    return ok;
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
    if (!run_storage_begin_raw(state->ctx.storage)) {
        ESP_LOGE(TAG, "Failed to open raw audio file for recording");
        audio_input_deinit();
        temp_humidity_stop();
        if (s_capture_events) {
            xEventGroupSetBits(s_capture_events, CAPTURE_TASK_DONE_BIT);
        }
        free(state);
        vTaskDelete(NULL);
        return;
    }

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

    if (!run_storage_end_raw(state->ctx.storage)) {
        ESP_LOGW(TAG, "Failed to finalize raw audio file cleanly");
    }

    audio_input_deinit();

    static temp_humidity_reading_t th_history[128];
    size_t th_history_count = 0;
    (void)temp_humidity_get_history(th_history, sizeof(th_history) / sizeof(th_history[0]), &th_history_count);
    if (temp_humidity_get_latest(&th)) {
        temp_c = th.temp_c;
        humidity = th.rh_percent;
    }
    temp_humidity_stop();
    ESP_LOGI(TAG, "Recording complete.");

    ESP_LOGI(TAG, "PHASE 1 COMPLETE: Recording done. %.2f s recorded to SD",
             (float)(recording_end_us - start_us) / 1000000.0f);

    // === PHASE 2: Analysis (read SD, compute metrics) ===
    ESP_LOGI(TAG, "PHASE 2: Reading RAW and computing post-recording metrics...");
    
    audio_metrics_t final_metrics = {0};
    
    if (analyze_raw_file(state->ctx.storage->raw_path,
                         state->ctx.node_id,
                         th_history,
                         th_history_count,
                         state->ctx.storage,
                         state->ctx.cal_offset_db,
                         start_us)) {
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
        if (s_capture_events) {
            xEventGroupSetBits(s_capture_events, CAPTURE_TASK_DONE_BIT);
        }
        free(state);
        vTaskDelete(NULL);
        return;
    }

    time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);

    esp_err_t mqtt_err = mqtt_publish_start();
    if (mqtt_err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT start failed; skipping MQTT stream: %s", esp_err_to_name(mqtt_err));
        if (s_capture_events) {
            xEventGroupSetBits(s_capture_events, CAPTURE_TASK_DONE_BIT);
        }
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


        ESP_LOGI(TAG, "PHASE 3 COMPLETE: Post-file MQTT streaming done");
    } else {
        ESP_LOGI(TAG, "PHASE 3 SKIPPED: MQTT unavailable (%s)", esp_err_to_name(werr));
    }

    if (s_capture_events) {
        xEventGroupSetBits(s_capture_events, CAPTURE_TASK_DONE_BIT);
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

    if (!s_capture_events) {
        s_capture_events = xEventGroupCreate();
        if (!s_capture_events) {
            return ESP_ERR_NO_MEM;
        }
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

bool capture_task_wait_complete(uint32_t timeout_ms)
{
    if (!s_capture_events) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_capture_events,
        CAPTURE_TASK_DONE_BIT,
        pdTRUE,
        pdFALSE,
        timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)
    );

    return (bits & CAPTURE_TASK_DONE_BIT) != 0;
}
