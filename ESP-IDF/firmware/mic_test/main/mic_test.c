/*
    mic_test - ESP-IDF bring-up test for ICS-43434 I2S microphone
    ------------------------------------------------------------
    Target: ESP32-S3 (Freenove ESP32-S3 WROOM)
    Framework: ESP-IDF v5.x
    Language: C (not Arduino, not C++)

    PURPOSE:
      - Verify I2S clocking and data wiring
      - Confirm microphone is producing real audio samples
      - Record a fixed capture window to RAM for later processing

    WIRING (ICS-43434 → ESP32-S3 Metro):
      3V     -> 3.3V
      GND    -> GND
      SEL    -> GND        (LEFT channel)
      BCLK   -> GPIO14          A0
      LRCLK  -> GPIO15 (WS)     A1
      DOUT   -> GPIO16 (DIN)    A2
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "driver/i2s_std.h"
#include "config_store.h"
#include "wifi_mqtt.h"
#include "temp_humidity.h"

/* ================= USER CONFIG ================= */

// I2S pin mapping (safe pins for ESP32-S3)
#define I2S_BCLK_GPIO   GPIO_NUM_14     //Bit Clock from ESP32
#define I2S_WS_GPIO     GPIO_NUM_15     //Word Select or LRCL on ICS-43434
#define I2S_DIN_GPIO    GPIO_NUM_16     //Data is sent to EPS from ICS-43434 over DOUT

// Audio sample rate
#define SAMPLE_RATE_HZ  48000

// I2S read size
#define I2S_FRAMES_PER_READ  512

// One-shot recording duration in seconds.
#define RECORD_SECONDS 20 
// Audio is stored in fixed-size chunks to avoid one large contiguous allocation.
#define AUDIO_CHUNK_SAMPLES 2048
#define CAL_OFFSET_DB  94.0f   // placeholder until calibrated

// Temporary first-boot provisioning values (stored into NVS if wifi is empty).
#define PROVISION_WIFI_SSID   "Cheerios & Shreddies"
#define PROVISION_WIFI_PASS   "JesusisLord"
#define PROVISION_MQTT_URI    "mqtt://172.20.10.5:1883"
#define PROVISION_MQTT_TOPIC  "sdacs/node/node01/features"
#define PROVISION_ALWAYS_SYNC_MQTT 1

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;
static int32_t raw[I2S_FRAMES_PER_READ];

typedef struct __attribute__((packed)) {
    uint32_t magic;       // 'S''D''A''C'
    uint16_t ver;         // 1
    uint16_t flags;       // 0 for now
    uint32_t seq;         // chunk counter
    uint64_t t_us;        // esp_timer_get_time() when chunk completed
    uint32_t sample_rate; // 48000
    uint32_t n;           // samples in this chunk
} sdacs_audio_hdr_t;

#define SDACS_MAGIC 0x43414453u  // 'SDAC' little-endian

static void maybe_provision_network_config(void)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    const char *broker_uri = NULL;
    const char *mqtt_topic = NULL;
    esp_err_t err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return;
    }

    if (strcmp(PROVISION_WIFI_SSID, "YOUR_WIFI_SSID") == 0 ||
        strcmp(PROVISION_WIFI_PASS, "YOUR_WIFI_PASSWORD") == 0) {
        ESP_LOGW(TAG, "Provisioning skipped: set PROVISION_WIFI_SSID/PROVISION_WIFI_PASS in mic_test.c");
        return;
    }

    if (!ssid || ssid[0] == '\0') {
        ESP_ERROR_CHECK(config_store_set_wifi(PROVISION_WIFI_SSID, PROVISION_WIFI_PASS));
        ESP_LOGI(TAG, "Provisioned WiFi defaults into NVS (one-time).");
    } else {
        ESP_LOGI(TAG, "WiFi already provisioned; keeping existing SSID.");
    }

    err = config_store_get_mqtt(&broker_uri, &mqtt_topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return;
    }

#if PROVISION_ALWAYS_SYNC_MQTT
    if (!broker_uri || !mqtt_topic ||
        strcmp(broker_uri, PROVISION_MQTT_URI) != 0 ||
        strcmp(mqtt_topic, PROVISION_MQTT_TOPIC) != 0) {
        ESP_ERROR_CHECK(config_store_set_mqtt(PROVISION_MQTT_URI, PROVISION_MQTT_TOPIC));
        broker_uri = PROVISION_MQTT_URI;
        mqtt_topic = PROVISION_MQTT_TOPIC;
        ESP_LOGW(TAG, "Synced MQTT settings in NVS to firmware defaults.");
    }
#endif

    ESP_LOGI(TAG, "Active MQTT config: broker=%s topic=%s",
             broker_uri ? broker_uri : "(null)",
             mqtt_topic ? mqtt_topic : "(null)");
}

/*
    Convert a 32-bit I2S slot word into a signed 24-bit sample.

    The ICS-43434 outputs 24-bit two's-complement audio.
    For this setup, the valid sample is treated as MSB-aligned
    in the 32-bit slot word (bits 31:8).
*/
static inline int32_t i2s_word_to_s24(int32_t w)
{
    // ICS-43434 data is typically MSB-aligned in a 32-bit slot (bits 31:8).
    // Shift down to a 24-bit payload, then sign-extend to 32-bit.
    int32_t s = (int32_t)((uint32_t)w >> 8);
    // If bit23 is set, sign-extend to 32-bit.
    if (s & 0x00800000) {
        s |= ~0x00FFFFFF;
    }
    return s;
}

/* ------------------------------------------------------------
   I2S initialization
------------------------------------------------------------ */
static void i2s_mic_init(void)
{
    // Create an I2S channel on controller 0 in MASTER mode.
    // MASTER means the ESP32 generates BCLK and WS.
    // We request only RX because this test is mic input only.
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    // Allocate channel resources and return an RX handle in rx_chan.
    // TX handle is NULL because this application does not transmit audio.
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    // Configure standard I2S (MSB/Philips style) timing and pin routing.
    i2s_std_config_t std_cfg = {
        // Derive clock configuration for the requested sampling rate.
        // The driver sets WS to SAMPLE_RATE_HZ and BCLK accordingly.
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),

        // Slot format on the wire:
        // - 32-bit slot width (container per channel slot)
        // - MONO mode (single logical channel for analysis)
        // The ICS-43434 produces 24-bit PCM values packed into this 32-bit slot.
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),

        // Map I2S signals to ESP32 pins.
        .gpio_cfg = {
            // Not used for this microphone setup.
            .mclk = I2S_GPIO_UNUSED,
            // Bit clock output from ESP32 to microphone.
            .bclk = I2S_BCLK_GPIO,
            // Word-select (LRCLK/WS) output from ESP32 to microphone.
            .ws   = I2S_WS_GPIO,
            // TX data line unused because this is RX-only.
            .dout = I2S_GPIO_UNUSED,
            // RX data line from microphone to ESP32.
            .din  = I2S_DIN_GPIO,
        },
    };

    // ICS-43434 with SEL=GND transmits on LEFT WS phase.
    // Restrict slot mask to LEFT so we don't ingest the inactive phase as data.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // Apply configuration to hardware channel.
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));

    // Enable channel + DMA engine so reads can start returning samples.
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // Startup summary line for quick wiring/config sanity check in monitor logs.
    ESP_LOGI(TAG,
        "I2S initialized: BCLK=%d WS=%d DIN=%d SR=%d",
        I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, SAMPLE_RATE_HZ);
}

/* ------------------------------------------------------------
   Microphone test task
------------------------------------------------------------ */
static void mic_test_task(void *arg)
{
    const char *base_topic = NULL;
    const char *broker = NULL;
    ESP_ERROR_CHECK(config_store_get_mqtt(&broker, &base_topic));
    (void)broker;
    if (!base_topic || base_topic[0] == '\0') {
        ESP_LOGE(TAG, "MQTT base topic is empty.");
        vTaskDelete(NULL);
        return;
    }

    char audio_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
    int topic_len = snprintf(audio_topic, sizeof(audio_topic), "%s/audio", base_topic);
    if (topic_len <= 0 || topic_len >= (int)sizeof(audio_topic)) {
        ESP_LOGE(TAG, "Audio topic too long; base topic='%s'", base_topic ? base_topic : "(null)");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)RECORD_SECONDS * 1000000LL);
    int64_t next_progress_us = start_us + 1000000LL;
    double rms_sum_sq = 0.0;
    size_t rms_count = 0;
    uint32_t samples_streamed = 0;

    sdacs_audio_hdr_t hdr = {
        .magic = SDACS_MAGIC,
        .ver = 1,
        .flags = 0,
        .seq = 0,
        .sample_rate = SAMPLE_RATE_HZ,
    };

    static int32_t chunk[AUDIO_CHUNK_SAMPLES];
    static uint8_t payload[sizeof(sdacs_audio_hdr_t) + (AUDIO_CHUNK_SAMPLES * sizeof(int32_t))];
    size_t chunk_fill = 0;

    /* ======================== ADDED FOR STREAMING STABILITY ======================== */

    ESP_LOGI(TAG, "Waiting for WiFi+MQTT before streaming...");

    esp_err_t werr = wifi_mqtt_wait_connected(15000);

    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not ready (%s). Chunks may be dropped.",
                 esp_err_to_name(werr));
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Starting audio stream.");
    }

    while (esp_timer_get_time() < end_us) {
        esp_err_t err = i2s_channel_read(
            rx_chan,
            raw,
            sizeof(raw),
            &bytes_read,
            portMAX_DELAY
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }

        // Raw 32-bit words delivered by I2S DMA.
        int n_raw = bytes_read / sizeof(int32_t);
        if (n_raw <= 0) continue;

        // In MONO + LEFT-slot mode, each returned word is a valid mic sample.
        for (int i = 0; i < n_raw; i++) {
            int32_t s24 = i2s_word_to_s24(raw[i]);
            chunk[chunk_fill++] = s24;
            samples_streamed++;

            float norm = (float)s24 / 8388608.0f;
            rms_sum_sq += (double)norm * (double)norm;
            rms_count++;

            if (chunk_fill >= AUDIO_CHUNK_SAMPLES) {
                hdr.n = (uint32_t)chunk_fill;
                hdr.t_us = (uint64_t)esp_timer_get_time();

                memcpy(payload, &hdr, sizeof(hdr));
                memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));

                esp_err_t perr = wifi_mqtt_publish_raw(
                    audio_topic,
                    payload,
                    sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
                    0,
                    0
                );
                if (perr != ESP_OK && (hdr.seq % 20u == 0u)) {
                    ESP_LOGW(TAG, "Audio chunk publish dropped: %s", esp_err_to_name(perr));
                }

                hdr.seq++;
                chunk_fill = 0;
            }
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_progress_us) {
            next_progress_us += 1000000LL;
            float rms = 0.0f;
            if (rms_count > 0) {
                rms = sqrtf((float)(rms_sum_sq / (double)rms_count));
            }

            // Convert RMS (normalized 0..1) to dBFS and then to SPL via calibration offset.
            float dbfs = 20.0f * log10f(rms + 1e-12f);
            float db_spl = dbfs + CAL_OFFSET_DB;

            ESP_LOGI(TAG, "Recording... %u/%u samples",
                (unsigned)samples_streamed,
                (unsigned)(SAMPLE_RATE_HZ * RECORD_SECONDS));
            ESP_LOGI(TAG, "  dBFS: %.2f dB", dbfs);
            ESP_LOGI(TAG, "  SPL:  %.2f dB", db_spl);

            rms_sum_sq = 0.0;
            rms_count = 0;
        }
    }

    if (chunk_fill > 0) {
        hdr.n = (uint32_t)chunk_fill;
        hdr.t_us = (uint64_t)esp_timer_get_time();
        memcpy(payload, &hdr, sizeof(hdr));
        memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));

        esp_err_t perr = wifi_mqtt_publish_raw(
            audio_topic,
            payload,
            sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
            0,
            0
        );
        if (perr != ESP_OK) {
            ESP_LOGW(TAG, "Final audio chunk publish dropped: %s", esp_err_to_name(perr));
        } else {
            hdr.seq++;
        }
    }

    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));

    ESP_LOGI(TAG, "Recording complete: %.2f s, streamed samples=%u",
        (float)(esp_timer_get_time() - start_us) / 1000000.0f,
        (unsigned)samples_streamed);
    ESP_LOGI(TAG, "Streamed %u audio chunk(s) to topic '%s'.",
        (unsigned)hdr.seq, audio_topic);

    vTaskDelete(NULL);
}

/* ------------------------------------------------------------
   app_main
------------------------------------------------------------ */
void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    maybe_provision_network_config();

    ESP_ERROR_CHECK(wifi_mqtt_start(NULL));

    bool th_ok = temp_humidity_start(0,
                    47,       // SDA
                    48,       // SCL
                    100000,   // 100kHz for reliable bring-up
                    0x44,     // default HDC302x I2C address
                    2000);    // reads every 2s
    if (!th_ok) {
        ESP_LOGW(TAG, "temp_humidity_start failed; continuing without temp/humidity telemetry");
    }

    i2s_mic_init();

    xTaskCreatePinnedToCore(
        mic_test_task,
        "mic_test",
        8192,
        NULL,
        5,
        NULL,
        1
    );

    ESP_LOGI(TAG, "ICS-43434 one-shot capture started (%d s).", RECORD_SECONDS);
}
