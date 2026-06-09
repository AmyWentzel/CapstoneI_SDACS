#include "battery_leds.h"

#include <math.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"

#include "sdacs_config.h"

static const char *TAG = "battery_leds";

/*
 * Physical P3 battery LED order is top-to-bottom:
 * P3.1 GPIO1, P3.2 GPIO18, P3.3 GPIO17, P3.4 GPIO16, P3.5 GPIO15, P3.6 GPIO14.
 * Keep this explicit because the visual bar order is the reverse of the
 * SDACS_LED_BATT_1..6 logical numbering in sdacs_config.h.
 */
static const gpio_num_t s_led_gpios_top_to_bottom[] = {
    SDACS_LED_BATT_6_GPIO, // GPIO1, physical top
    SDACS_LED_BATT_5_GPIO, // GPIO18
    SDACS_LED_BATT_4_GPIO, // GPIO17
    SDACS_LED_BATT_3_GPIO, // GPIO16
    SDACS_LED_BATT_2_GPIO, // GPIO15
    SDACS_LED_BATT_1_GPIO, // GPIO14, physical bottom
};

static bool s_initialized = false;

static size_t led_count_for_soc(float soc_percent)
{
    if (!isfinite(soc_percent) || soc_percent <= 0.0f) {
        return 0;
    }
    if (soc_percent >= 90.0f) {
        return 6;
    }
    if (soc_percent >= 75.0f) {
        return 5;
    }
    if (soc_percent >= 65.0f) {
        return 4;
    }
    if (soc_percent >= 50.0f) {
        return 3;
    }
    if (soc_percent >= 26.0f) {
        return 2;
    }
    return 1;
}

esp_err_t battery_leds_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (s_initialized) {
        return ESP_OK;
    }

    const size_t led_count = sizeof(s_led_gpios_top_to_bottom) / sizeof(s_led_gpios_top_to_bottom[0]);
    for (size_t i = 0; i < led_count; ++i) {
        cfg.pin_bit_mask |= (1ULL << s_led_gpios_top_to_bottom[i]);
    }

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Battery LED GPIO init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    battery_leds_off();
    ESP_LOGI(TAG, "Battery LEDs initialized top-to-bottom: %d,%d,%d,%d,%d,%d",
             s_led_gpios_top_to_bottom[0],
             s_led_gpios_top_to_bottom[1],
             s_led_gpios_top_to_bottom[2],
             s_led_gpios_top_to_bottom[3],
             s_led_gpios_top_to_bottom[4],
             s_led_gpios_top_to_bottom[5]);
    return ESP_OK;
}

void battery_leds_show_percent(float soc_percent)
{
    if (!s_initialized) {
        return;
    }

    const size_t count = led_count_for_soc(soc_percent);
    const size_t led_count = sizeof(s_led_gpios_top_to_bottom) / sizeof(s_led_gpios_top_to_bottom[0]);
    for (size_t i = 0; i < led_count; ++i) {
        bool on = i >= (led_count - count);
        (void)gpio_set_level(s_led_gpios_top_to_bottom[i], on ? 1 : 0);
    }
}

void battery_leds_off(void)
{
    if (!s_initialized) {
        return;
    }

    const size_t led_count = sizeof(s_led_gpios_top_to_bottom) / sizeof(s_led_gpios_top_to_bottom[0]);
    for (size_t i = 0; i < led_count; ++i) {
        (void)gpio_set_level(s_led_gpios_top_to_bottom[i], 0);
    }
}

bool battery_leds_is_initialized(void)
{
    return s_initialized;
}
