#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include <string.h>
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

// Forward declarations
static void turn_off_all_leds(void);

// Initialize all LED pins as outputs
static void init_led_pins(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << SDACS_LED_A0_GPIO) |
                        (1ULL << SDACS_LED_A1_GPIO) |
                        (1ULL << SDACS_LED_A2_GPIO) |
                        (1ULL << SDACS_LED_A3_GPIO) |
                        (1ULL << SDACS_LED_A4_GPIO) |
                        (1ULL << SDACS_LED_A5_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Turn off all LEDs initially
    gpio_set_level(SDACS_LED_A0_GPIO, 0);
    gpio_set_level(SDACS_LED_A1_GPIO, 0);
    gpio_set_level(SDACS_LED_A2_GPIO, 0);
    gpio_set_level(SDACS_LED_A3_GPIO, 0);
    gpio_set_level(SDACS_LED_A4_GPIO, 0);
    gpio_set_level(SDACS_LED_A5_GPIO, 0);
    
    ESP_LOGI(TAG, "LED pins initialized");
}

// Turn on LED and all lower LEDs (cascading effect)
static void turn_on_led_cascade(const char *pin_label)
{
    if (pin_label == NULL) {
        turn_off_all_leds();
        return;
    }
    
    turn_off_all_leds();
    
    // Turn on the specified LED and all lower-numbered ones
    if (strcmp(pin_label, "A0") == 0) {
        gpio_set_level(SDACS_LED_A0_GPIO, 1);
        gpio_set_level(SDACS_LED_A1_GPIO, 1);
        gpio_set_level(SDACS_LED_A2_GPIO, 1);
        gpio_set_level(SDACS_LED_A3_GPIO, 1);
        gpio_set_level(SDACS_LED_A4_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    } else if (strcmp(pin_label, "A1") == 0) {
        gpio_set_level(SDACS_LED_A1_GPIO, 1);
        gpio_set_level(SDACS_LED_A2_GPIO, 1);
        gpio_set_level(SDACS_LED_A3_GPIO, 1);
        gpio_set_level(SDACS_LED_A4_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    } else if (strcmp(pin_label, "A2") == 0) {
        gpio_set_level(SDACS_LED_A2_GPIO, 1);
        gpio_set_level(SDACS_LED_A3_GPIO, 1);
        gpio_set_level(SDACS_LED_A4_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    } else if (strcmp(pin_label, "A3") == 0) {
        gpio_set_level(SDACS_LED_A3_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    } else if (strcmp(pin_label, "A4") == 0) {
        gpio_set_level(SDACS_LED_A4_GPIO, 1);
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    } else if (strcmp(pin_label, "A5") == 0) {
        gpio_set_level(SDACS_LED_A5_GPIO, 1);
    }
}

// Turn off all LEDs
static void turn_off_all_leds(void)
{
    gpio_set_level(SDACS_LED_A0_GPIO, 0);
    gpio_set_level(SDACS_LED_A1_GPIO, 0);
    gpio_set_level(SDACS_LED_A2_GPIO, 0);
    gpio_set_level(SDACS_LED_A3_GPIO, 0);
    gpio_set_level(SDACS_LED_A4_GPIO, 0);
    gpio_set_level(SDACS_LED_A5_GPIO, 0);
}

void app_main(void)
{
    // Initialize and run battery monitoring for 3 seconds before anything else
    init_led_pins();
    
    ESP_LOGI(TAG, "Starting battery life check");
    if (batteryLife_init()) {
        int battery_percent = batteryLife_get_percent();
        const char *led_pin = batteryLife_get_level_pin(battery_percent);
        
        turn_on_led_cascade(led_pin);
        ESP_LOGI(TAG, "Battery level: %d%% - LED cascade starting at: %s", battery_percent, led_pin);
        
        vTaskDelay(pdMS_TO_TICKS(3000));  // Wait 3 seconds
        turn_off_all_leds();
        ESP_LOGI(TAG, "Battery check complete");
    } else {
        ESP_LOGW(TAG, "Battery initialization failed");
    }

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
