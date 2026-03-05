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

// Include Project-Specific Headers
#include "audioProcessing.h"



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
