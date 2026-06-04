#include "batteryLife.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "batteryLife";
static bool s_initialized = false;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;

bool batteryLife_init(void)
{
    if (s_initialized) {
        return true;
    }

    // Initialize ADC oneshot unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ret = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return false;
    }

    // Calibrate ADC
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values: %s", esp_err_to_name(ret));
        // Continue without calibration - it's not fatal
    }

    s_initialized = true;
    return true;
}

int batteryLife_get_percent(void)
{
    if (!s_initialized && !batteryLife_init()) {
        return 0;
    }

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return 0;
    }

    uint32_t voltage = 0;
    if (s_adc_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(s_adc_cali_handle, raw, (int *)&voltage);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC calibration conversion failed: %s", esp_err_to_name(ret));
            // Fallback to raw conversion
            voltage = raw * 3300 / 4096;
        }
    } else {
        // Fallback conversion without calibration
        voltage = raw * 3300 / 4096;
    }

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
