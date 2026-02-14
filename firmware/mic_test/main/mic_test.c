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

    WIRING (ICS-43434 → ESP32-S3):
      3V     -> 3.3V
      GND    -> GND
      SEL    -> GND        (LEFT channel)
      BCLK   -> GPIO6
      LRCLK  -> GPIO5
      DOUT   -> GPIO4
*/

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

/* ================= USER CONFIG ================= */

// I2S pin mapping (safe pins for ESP32-S3)
#define I2S_BCLK_GPIO   GPIO_NUM_6
#define I2S_WS_GPIO     GPIO_NUM_5
#define I2S_DIN_GPIO    GPIO_NUM_4

// Audio sample rate
#define SAMPLE_RATE_HZ  48000

// I2S read size
#define I2S_FRAMES_PER_READ  512

// One-shot recording duration in seconds.
#define RECORD_SECONDS 20

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;
static int32_t raw[I2S_FRAMES_PER_READ];

// Recorded audio for later FFT/MQTT pipeline.
static int32_t *g_audio_samples = NULL;
static size_t g_audio_samples_count = 0;
static const size_t g_audio_samples_capacity = (size_t)SAMPLE_RATE_HZ * RECORD_SECONDS;

/*
    Convert a 32-bit I2S slot word into a signed 24-bit sample.

    The ICS-43434 outputs 24-bit two's-complement audio.
    Depending on I2S packing, the valid sample may appear in the
    lower 24 bits of the 32-bit slot word.
*/
static inline int32_t i2s_word_to_s24(int32_t w)
{
    // Keep only 24 payload bits.
    int32_t s = w & 0x00FFFFFF;
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
    size_t bytes_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)RECORD_SECONDS * 1000000LL);
    int64_t next_progress_us = start_us + 1000000LL;

    while (esp_timer_get_time() < end_us && g_audio_samples_count < g_audio_samples_capacity) {
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

        // Consume only LEFT-phase words from interleaved I2S stream.
        // With current packing this lands on even indices, so only every
        // second raw word is stored.
        for (int i = 0; i < n_raw; i += 2) {
            int32_t s24 = i2s_word_to_s24(raw[i]);
            if (g_audio_samples_count >= g_audio_samples_capacity) {
                break;
            }
            g_audio_samples[g_audio_samples_count++] = s24;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_progress_us) {
            next_progress_us += 1000000LL;
            ESP_LOGI(TAG, "Recording... %u/%u samples",
                (unsigned)g_audio_samples_count,
                (unsigned)g_audio_samples_capacity);
        }
    }

    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));

    ESP_LOGI(TAG, "Recording complete: %.2f s, samples=%u",
        (float)(esp_timer_get_time() - start_us) / 1000000.0f,
        (unsigned)g_audio_samples_count);
    ESP_LOGI(TAG, "Stored stream currently uses left-phase-only decimation (i += 2).");
    ESP_LOGI(TAG, "Samples stored in g_audio_samples for later FFT/MQTT.");

    vTaskDelete(NULL);
}

/* ------------------------------------------------------------
   app_main
------------------------------------------------------------ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    // Allocate enough space for up to 20 seconds of captured samples.
    // Try PSRAM first (if present), then fall back to internal RAM.
    g_audio_samples = (int32_t *)heap_caps_malloc(
        g_audio_samples_capacity * sizeof(int32_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (g_audio_samples == NULL) {
        g_audio_samples = (int32_t *)heap_caps_malloc(
            g_audio_samples_capacity * sizeof(int32_t),
            MALLOC_CAP_8BIT
        );
    }
    if (g_audio_samples == NULL) {
        ESP_LOGE(TAG, "Audio buffer allocation failed (%u samples).",
            (unsigned)g_audio_samples_capacity);
        return;
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

