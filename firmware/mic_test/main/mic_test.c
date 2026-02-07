/*
    mic_test - ESP-IDF bring-up test for ICS-43434 I2S microphone
    ------------------------------------------------------------
    Target: ESP32-S3 (Freenove ESP32-S3 WROOM)
    Framework: ESP-IDF v5.x
    Language: C (not Arduino, not C++)

    PURPOSE:
      - Verify I2S clocking and data wiring
      - Confirm microphone is producing real audio samples
      - Print simple statistics to serial (USB)

    WIRING (ICS-43434 → ESP32-S3):
      3V     -> 3.3V
      GND    -> GND
      SEL    -> GND        (LEFT channel)
      BCLK   -> GPIO6
      LRCLK  -> GPIO5
      DOUT   -> GPIO4
*/

#include <stdio.h>
#include <math.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
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

// How often to print stats
#define PRINT_PERIOD_MS  250

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;

/*
    Convert a 32-bit I2S slot word into a signed 24-bit sample.

    The ICS-43434 outputs 24-bit two's-complement audio.
    In standard MSB-aligned I2S mode, the sample occupies the
    upper 24 bits of a 32-bit slot.
*/
static inline int32_t i2s_word_to_s24(int32_t w)
{
    return (w >> 8);   // arithmetic shift preserves sign
}

/* ------------------------------------------------------------
   I2S initialization
------------------------------------------------------------ */
static void i2s_mic_init(void)
{
    // Create RX channel (mic → ESP32 only)
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = I2S_DIN_GPIO,
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    ESP_LOGI(TAG,
        "I2S initialized: BCLK=%d WS=%d DIN=%d SR=%d",
        I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, SAMPLE_RATE_HZ);
}

/* ------------------------------------------------------------
   Microphone test task
------------------------------------------------------------ */
static void mic_test_task(void *arg)
{
    int32_t raw[I2S_FRAMES_PER_READ];
    size_t bytes_read = 0;

    TickType_t last_print = xTaskGetTickCount();

    while (1) {
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

        int n = bytes_read / sizeof(int32_t);
        if (n <= 0) continue;

        int32_t minv = INT32_MAX;
        int32_t maxv = INT32_MIN;
        int64_t sum  = 0;
        double  acc2 = 0.0;
        int zeros = 0;

        for (int i = 0; i < n; i++) {
            int32_t s24 = i2s_word_to_s24(raw[i]);

            if (s24 == 0) zeros++;
            if (s24 < minv) minv = s24;
            if (s24 > maxv) maxv = s24;

            sum += s24;

            float x = (float)s24 / 8388608.0f; // 2^23
            acc2 += (double)x * (double)x;
        }

        float mean = (float)((double)sum / n);
        float rms  = (float)sqrt(acc2 / n);
        int32_t p2p = maxv - minv;

        TickType_t now = xTaskGetTickCount();
        if (pdTICKS_TO_MS(now - last_print) >= PRINT_PERIOD_MS) {
            last_print = now;

            ESP_LOGI(TAG,
                "n=%d mean=%.1f p2p=%" PRId32 " rms=%.6f zeros=%d",
                n, mean, p2p, rms, zeros
            );

            if (zeros == n) {
                ESP_LOGE(TAG,
                    "ALL ZERO SAMPLES! Check DIN, clocks, and SEL=GND.");
            }
        }
    }
}

/* ------------------------------------------------------------
   app_main
------------------------------------------------------------ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    i2s_mic_init();

    xTaskCreatePinnedToCore(
        mic_test_task,
        "mic_test",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    ESP_LOGI(TAG, "ICS-43434 mic test running. Clap or speak near mic.");
}