#include "batteryLife.h"

#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

static const char *TAG = "batteryLife";
static bool s_initialized = false;
static esp_adc_cal_characteristics_t s_adc_chars;

bool batteryLife_init(void)
{
    if (s_initialized) {
        return true;
    }

    adc1_config_width(ADC_WIDTH_BIT_12);
    esp_err_t err = adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel configuration failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_adc_chars);
    s_initialized = true;
    return true;
}

int batteryLife_get_percent(void)
{
    if (!s_initialized && !batteryLife_init()) {
        return 0;
    }

    int raw = adc1_get_raw(ADC1_CHANNEL_0);
    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, &s_adc_chars);
    int percent = (int)((voltage * 100ULL) / 3300ULL);
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    return percent;
}

const char *batteryLife_get_level_pin(int battery_percent)
{
    if (battery_percent >= 90) {
        return "A0";
    }
    if (battery_percent >= 75) {
        return "A4";
    }
    if (battery_percent >= 65) {
        return "A3";
    }
    if (battery_percent >= 50) {
        return "A2";
    }
    if (battery_percent >= 26) {
        return "A1";
    }
    return "A5";
}
