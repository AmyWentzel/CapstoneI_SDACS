#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "fuel_gauge.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_ERROR_CHECK(fuel_gauge_init());

    while (1) {
        fuel_gauge_reading_t batt;
        esp_err_t err = fuel_gauge_read(&batt);

        if (err == ESP_OK && batt.valid) {
            ESP_LOGI(TAG, "Battery: %.3f V | %.1f %% | ver=0x%04X",
                     batt.voltage_v, batt.soc_percent, batt.version_raw);
        } else {
            ESP_LOGW(TAG, "Fuel gauge read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
