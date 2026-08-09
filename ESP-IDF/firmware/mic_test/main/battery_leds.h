/*
 * SDACS module: Public interface for battery indicator LEDs
 *
 * Purpose:
 *   Exposes LED initialization, percentage display, shutdown, and initialization-state queries.
 *
 * Design note:
 *   Hardware details remain encapsulated in the implementation.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t battery_leds_init(void);
void battery_leds_show_percent(float soc_percent);
void battery_leds_off(void);
bool battery_leds_is_initialized(void);
