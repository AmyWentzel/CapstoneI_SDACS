#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t battery_leds_init(void);
void battery_leds_show_percent(float soc_percent);
void battery_leds_off(void);
bool battery_leds_is_initialized(void);
