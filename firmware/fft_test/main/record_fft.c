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

/*
    AW - 2026/02/19
    record_fft.c - added two functions to record 20 seconds of audio and record FFT 
    audio is stored in audio_buffer, and FFT results are stored in FFT_results
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"

#include "esp_dsp.h"

/* ================= USER CONFIG ================= */

// I2S pin mapping (safe pins for ESP32-S3)
#define I2S_BCLK_GPIO   GPIO_NUM_6
#define I2S_WS_GPIO     GPIO_NUM_5
#define I2S_DIN_GPIO    GPIO_NUM_4

// Audio sample rate
#define SAMPLE_RATE_HZ  48000

// I2S read size
#define I2S_FRAMES_PER_READ  512

// Recording duration
#define RECORD_DURATION_SEC  20

// How often to print stats during recording
#define PRINT_PERIOD_MS  250

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;

// Recording buffer: 20 seconds at 48kHz = 960,000 samples
// WARNING: This uses ~3.75 MB of RAM. If ESP32-S3 runs out of memory,
// reduce RECORD_DURATION_SEC or use external storage (SPIFFS/SD card).
static int32_t audio_buffer[SAMPLE_RATE_HZ * RECORD_DURATION_SEC];
static size_t buffer_index = 0;
static SemaphoreHandle_t recording_done_sem = NULL;

// FFT parameters
#define FFT_SIZE 1024
static float fft_input[FFT_SIZE];
static float fft_output[FFT_SIZE];
static float magnitude[FFT_SIZE / 2];

// FFT results storage
typedef struct {
    float magnitude;
    int bin;
    float frequency;
} FFT_Result;

#define MAX_FFT_CHUNKS 1000
static FFT_Result FFT_results[MAX_FFT_CHUNKS];
static size_t num_fft_chunks = 0;

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
   Recording task - fills audio_buffer for 20 seconds
------------------------------------------------------------ */
static void recording_task(void *arg)
{
    int32_t raw[I2S_FRAMES_PER_READ];
    size_t bytes_read = 0;

    size_t max_samples = SAMPLE_RATE_HZ * RECORD_DURATION_SEC;
    TickType_t last_print = xTaskGetTickCount();

    ESP_LOGI(TAG, "Starting %d second recording...", RECORD_DURATION_SEC);

    while (buffer_index < max_samples) {
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

        // Add samples to buffer, being careful not to exceed max
        for (int i = 0; i < n && buffer_index < max_samples; i++) {
            audio_buffer[buffer_index++] = i2s_word_to_s24(raw[i]);
        }

        // Print progress periodically
        TickType_t now = xTaskGetTickCount();
        if (pdTICKS_TO_MS(now - last_print) >= PRINT_PERIOD_MS) {
            last_print = now;
            float percent = (float)buffer_index / max_samples * 100.0f;
            ESP_LOGI(TAG, "Recording: %.1f%% (%zu / %zu samples)",
                percent, buffer_index, max_samples);
        }
    }

    ESP_LOGI(TAG, "Recording complete! %zu samples collected.", buffer_index);
    xSemaphoreGive(recording_done_sem);
    vTaskDelete(NULL);  // Task ends after recording is done
}

/* ------------------------------------------------------------
   Process audio buffer (called from main after recording)
------------------------------------------------------------ */
static void calculateFFTAudio(void)
{
    if (buffer_index == 0) {
        ESP_LOGE(TAG, "No audio samples recorded!");
        return;
    }

    ESP_LOGI(TAG, "Processing %zu samples with FFT (size=%d)...", buffer_index, FFT_SIZE);
    
    // Initialize FFT
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FFT initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Process audio in chunks of FFT_SIZE
    size_t num_chunks = buffer_index / FFT_SIZE;
    ESP_LOGI(TAG, "Computing %zu FFTs...", num_chunks);
    num_fft_chunks = 0;  // Reset chunk counter

    for (size_t chunk_idx = 0; chunk_idx < num_chunks; chunk_idx++) {
        // 1. Convert int32 samples to float and normalize
        for (int i = 0; i < FFT_SIZE; i++) {
            // Normalize to [-1, 1] range
            fft_input[i] = (float)audio_buffer[chunk_idx * FFT_SIZE + i] / (float)INT32_MAX;
        }

        // 2. Apply Hann window to reduce spectral leakage
        dsps_wind_hann_f32(fft_input, FFT_SIZE);

        // 3. Prepare input for FFT (interleave real and imaginary parts)
        for (int i = 0; i < FFT_SIZE; i++) {
            fft_output[i * 2 + 0] = fft_input[i];      // Real part
            fft_output[i * 2 + 1] = 0.0f;              // Imaginary part (initially zero)
        }

        // 4. Compute FFT
        dsps_fft2r_fc32(fft_output, FFT_SIZE);

        // 5. Bit reversal (required by esp-dsp)
        dsps_bit_rev_fc32(fft_output, FFT_SIZE);

        // 6. Compute magnitude spectrum
        float max_magnitude = 0.0f;
        int max_bin = 0;
        for (int i = 0; i < FFT_SIZE / 2; i++) {
            float real = fft_output[i * 2 + 0];
            float imag = fft_output[i * 2 + 1];
            magnitude[i] = sqrt(real * real + imag * imag) / FFT_SIZE;

            // Track peak frequency
            if (magnitude[i] > max_magnitude) {
                max_magnitude = magnitude[i];
                max_bin = i;
            }
        }

        // Calculate corresponding frequency
        float peak_freq = (float)max_bin * SAMPLE_RATE_HZ / FFT_SIZE;

        // Store result in FFT_results array
        if (chunk_idx < MAX_FFT_CHUNKS) {
            FFT_results[chunk_idx].magnitude = max_magnitude;
            FFT_results[chunk_idx].bin = max_bin;
            FFT_results[chunk_idx].frequency = peak_freq;
        }

        // Print results for this chunk
        ESP_LOGI(TAG, "FFT Chunk %zu Results:", chunk_idx);
        ESP_LOGI(TAG, "  Peak magnitude: %.4f at bin %d", max_magnitude, max_bin);
        ESP_LOGI(TAG, "  Peak frequency: %.1f Hz", peak_freq);
    }

    num_fft_chunks = (num_chunks < MAX_FFT_CHUNKS) ? num_chunks : MAX_FFT_CHUNKS;

    ESP_LOGI(TAG, "FFT processing complete!");
}

/* ------------------------------------------------------------
   app_main
------------------------------------------------------------ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    // Create semaphore for recording completion
    recording_done_sem = xSemaphoreCreateBinary();
    if (recording_done_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    i2s_mic_init();

    // Start recording task
    xTaskCreatePinnedToCore(
        recording_task,
        "recording_task",
        4096,
        NULL,
        5,
        NULL,
        1
    );

    // Wait for recording to complete
    ESP_LOGI(TAG, "Waiting for recording to complete...");
    if (xSemaphoreTake(recording_done_sem, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Recording finished, processing audio...");
        calculateFFTAudio();
        ESP_LOGI(TAG, "Audio processing complete!");
    }

    // Keep main alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
