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
#include <limits.h>

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

// Full-scale magnitude for signed 24-bit PCM: [-2^23, 2^23-1]
// Used to normalize samples into [-1.0, +1.0).
#define S24_FULL_SCALE  8388608.0f

// Small floor for dB conversion to avoid log10(0).
// 1 LSB of 24-bit full-scale is a practical minimum.
#define RMS_DB_FLOOR_LINEAR (1.0f / S24_FULL_SCALE)

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;
static int32_t raw[I2S_FRAMES_PER_READ];

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

        // Raw 32-bit words delivered by I2S DMA.
        int n_raw = bytes_read / sizeof(int32_t);
        if (n_raw <= 0) continue;

        // Stats over the decoded PCM sample set:
        // - min/max -> p2p (peak-to-peak span in raw counts)
        // - sum     -> mean (DC offset estimate in raw counts)
        // - acc2    -> rms (signal energy)
        // - zeros   -> simple integrity signal (excessive zeros can indicate link issues)
        int32_t minv = INT32_MAX;
        int32_t maxv = INT32_MIN;
        int64_t sum  = 0;
        double  acc2 = 0.0;
        int zeros = 0;

        int n = 0;
        // Consume only LEFT-phase words from interleaved I2S stream.
        // With current packing this lands on even indices.
        for (int i = 0; i < n_raw; i += 2) {
            int32_t s24 = i2s_word_to_s24(raw[i]);

            if (s24 == 0) zeros++;
            if (s24 < minv) minv = s24;
            if (s24 > maxv) maxv = s24;

            sum += s24;

            // Normalize signed 24-bit sample to approximately [-1.0, +1.0).
            float x = (float)s24 / S24_FULL_SCALE;
            acc2 += (double)x * (double)x;
            n++;
        }

        if (n <= 0) continue;

        // Mean in raw sample counts (not normalized). Near zero is expected for AC-coupled audio.
        float mean = (float)((double)sum / n);
        // RMS in linear full-scale units (0..1+). Useful for level tracking.
        float rms  = (float)sqrt(acc2 / n);
        // RMS converted to dBFS using linear full-scale reference (1.0).
        // With this convention, a full-scale sine is about -3.01 dBFS.
        float rms_dbfs = 20.0f * log10f(fmaxf(rms, RMS_DB_FLOOR_LINEAR));
        // Peak-to-peak span in raw sample counts.
        int32_t p2p = maxv - minv;

        TickType_t now = xTaskGetTickCount();
        if (pdTICKS_TO_MS(now - last_print) >= PRINT_PERIOD_MS) {
            last_print = now;

            ESP_LOGI(TAG,
                // Field guide:
                // n         : number of analyzed samples this print window
                // mean      : average raw count (DC bias indicator)
                // p2p       : max-min raw count span
                // rms       : linear RMS, normalized to full-scale
                // rms_dbfs  : RMS in dBFS (more negative = quieter)
                // zeros     : exact-zero sample count in this block
                "n=%d mean=%.1f p2p=%" PRId32 " rms=%.6f rms_dbfs=%.2f zeros=%d",
                n, mean, p2p, rms, rms_dbfs, zeros
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
        8192,
        NULL,
        5,
        NULL,
        1
    );

    ESP_LOGI(TAG, "ICS-43434 mic test running. Clap or speak near mic.");
}

