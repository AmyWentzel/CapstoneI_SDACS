#include "capture_task.h"

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
    int64_t next_metrics_us = start_us + 1000000LL;
    uint32_t samples_streamed = 0;
    uint32_t metrics_seq = 0;

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
    bool fatal_error = false;

    device_state_set(SDACS_MODE_CAPTURING);

    ESP_LOGI(TAG, "Waiting for WiFi+MQTT before streaming...");
    esp_err_t werr = wifi_mqtt_wait_connected(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not ready (%s). Continuing with local SD logging.", esp_err_to_name(werr));
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Starting stream + SD logging.");
        (void)temp_humidity_publish_latest_once("start");
        (void)fuel_gauge_publish_latest_once("start");
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
                }

                hdr.n = (uint32_t)chunk_fill;
                hdr.t_us = (uint64_t)esp_timer_get_time();
                memcpy(payload, &hdr, sizeof(hdr));
                memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));
                err = wifi_mqtt_publish_raw(
                    state->audio_topic,
                    payload,
                    sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
                    0,
                    0
                );
                if (err != ESP_OK && (hdr.seq % 20u == 0u)) {
                    ESP_LOGW(TAG, "Audio chunk publish dropped: %s", esp_err_to_name(err));
                }

                samples_streamed += (uint32_t)chunk_fill;
                hdr.seq++;
                chunk_fill = 0;
            }
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_metrics_us) {
            audio_metrics_t metrics = {0};
            if (fft_metrics_compute_and_reset(&metrics, state->ctx.cal_offset_db)) {
                temp_humidity_reading_t th = {0};
                fuel_gauge_reading_t batt = {0};
                float temp_c = NAN;
                float humidity = NAN;
                float batt_soc_percent = NAN;
                float batt_voltage_v = NAN;
                float batt_charge_rate_pct_per_hr = NAN;
                bool batt_valid = false;
                if (temp_humidity_get_latest(&th)) {
                    temp_c = th.temp_c;
                    humidity = th.rh_percent;
                }
                if (fuel_gauge_get_latest(&batt)) {
                    batt_soc_percent = batt.soc_percent;
                    batt_voltage_v = batt.voltage_v;
                    batt_charge_rate_pct_per_hr = batt.charge_rate_percent_per_hr;
                    batt_valid = true;
                }

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

                sdacs_features_t feat = {0};
                strncpy(feat.node_id, state->ctx.node_id, sizeof(feat.node_id) - 1);
                feat.seq = metrics_seq++;
                feat.t_us = (uint64_t)now_us;
                feat.n = metrics.sample_count;
                feat.rms = metrics.rms_norm;
                feat.dbfs = metrics.dbfs;
                feat.db_spl = metrics.laeq_db;
                feat.f_peak_hz = metrics.fft_peak_hz;
                feat.p2p_raw = metrics.peak_abs * 2;
                feat.zeros = 0;
                feat.temp_c = temp_c;
                feat.rh_percent = humidity;
                // Node-RED should parse the new battery fields alongside temp/humidity and audio metrics.
                feat.batt_soc_percent = batt_soc_percent;
                feat.batt_voltage_v = batt_voltage_v;
                feat.batt_charge_rate_pct_per_hr = batt_charge_rate_pct_per_hr;
                feat.batt_valid = batt_valid;

                if (!wifi_mqtt_try_send(&feat)) {
                    ESP_LOGW(TAG, "Failed to enqueue 1 Hz features");
                }

                ESP_LOGI(TAG, "LAeq=%.2f dB peak=%.2f dB streamed=%u",
                         metrics.laeq_db, metrics.peak_db, (unsigned)samples_streamed);
            }

            next_metrics_us += 1000000LL;
        }
    }

    if (chunk_fill > 0) {
        if (!run_storage_append_raw(state->ctx.storage, chunk, chunk_fill)) {
            ESP_LOGW(TAG, "Failed final SD append");
        }

        hdr.n = (uint32_t)chunk_fill;
        hdr.t_us = (uint64_t)esp_timer_get_time();
        memcpy(payload, &hdr, sizeof(hdr));
        memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));
        (void)wifi_mqtt_publish_raw(
            state->audio_topic,
            payload,
            sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
            0,
            0
        );
        hdr.seq++;
    }

    (void)run_storage_convert_raw_to_wav(state->ctx.storage, SDACS_SAMPLE_RATE_HZ);
    run_storage_refresh_timestamps(state->ctx.storage);

    ESP_LOGI(TAG, "Recording complete: %.2f s, chunk_msgs=%u",
             (float)(esp_timer_get_time() - start_us) / 1000000.0f,
             (unsigned)hdr.seq);

    if (wifi_mqtt_is_connected()) {
        (void)temp_humidity_publish_latest_once("end");
        (void)fuel_gauge_publish_latest_once("end");
    }

    run_storage_verify(state->ctx.storage);
    device_state_set(fatal_error ? SDACS_MODE_ERROR : SDACS_MODE_IDLE);
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
