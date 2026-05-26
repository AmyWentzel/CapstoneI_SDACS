#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t sdacs_ble_locator_advertise_for(const char *node_id, uint32_t duration_ms);
