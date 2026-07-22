#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

esp_err_t sdacs_ble_locator_advertise_for(uint32_t duration_ms);
esp_err_t sdacs_ble_locator_start_async(uint32_t duration_ms);
bool sdacs_ble_locator_is_busy(void);
