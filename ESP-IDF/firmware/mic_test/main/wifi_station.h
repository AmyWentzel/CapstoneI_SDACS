#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t sdacs_wifi_station_start(void);
esp_err_t sdacs_wifi_station_wait_connected(uint32_t timeout_ms);
