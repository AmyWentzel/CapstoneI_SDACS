#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "batteryLife.h"
#include "config_store.h"
#include "network.h"
#include "recordAudio.h"
#include "audioAnalysis.h"
#include "metricsCSV.h"
#include "sdCard.h"
#include "tempHumidity.h"
#include "sdacs_config.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(network_provisioning_apply_defaults());

    if (sdacs_wifi_station_start() == ESP_OK) {
        time_sync_try_sntp(SDACS_WIFI_TIME_SYNC_WAIT_MS);
    } else {
        ESP_LOGW(TAG, "Wi-Fi startup failed; date/time may be inaccurate");
    }

    if (!sdCard_init()) {
        ESP_LOGE(TAG, "SD card initialization failed");
        return;
    }

    if (!tempHumidity_start()) {
        ESP_LOGW(TAG, "Temp/humidity sampling failed; metrics will show NAN");
    }

    ESP_LOGI(TAG, "Starting audio capture for %d seconds", SDACS_RECORD_SECONDS);
    if (!recordAudio_capture(SDACS_RECORD_SECONDS)) {
        ESP_LOGE(TAG, "Audio capture failed");
    } else {
        ESP_LOGI(TAG, "Audio capture complete");
    }

    tempHumidity_stop();

    if (!sdCard_convert_raw_to_wav(SDACS_SAMPLE_RATE_HZ)) {
        ESP_LOGE(TAG, "Raw-to-WAV conversion failed");
    }

    if (!metricsCSV_init()) {
        ESP_LOGE(TAG, "Metrics CSV initialization failed");
    }

    if (!audioAnalysis_init()) {
        ESP_LOGE(TAG, "Audio analysis initialization failed");
    }

    if (!audioAnalysis_analyze_wav(SDACS_CAL_OFFSET_DB)) {
        ESP_LOGE(TAG, "Audio analysis failed");
    } else {
        metrics_record_t summary;
        if (audioAnalysis_get_latest_summary(&summary)) {
            ESP_LOGI(TAG, "Analysis complete: LAeq=%.2f dB, peak=%.2f dB, temp=%.2f C, humidity=%.2f%%",
                     summary.laeq_db,
                     summary.peak_db,
                     summary.temp_c,
                     summary.humidity);
            ESP_LOGI(TAG, "Run directory: %s", sdCard_get_run_dir());
            ESP_LOGI(TAG, "WAV path: %s", sdCard_get_wav_path());
            ESP_LOGI(TAG, "Metrics path: %s", metricsCSV_get_path());
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
