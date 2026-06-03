#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t network_provisioning_apply_defaults(void);

esp_err_t sdacs_wifi_station_start(void);
esp_err_t sdacs_wifi_station_wait_connected(uint32_t timeout_ms);

bool time_sync_is_valid(void);
void time_sync_get_iso8601(char *out, size_t out_len);
void time_sync_log_current(const char *prefix);
void time_sync_try_sntp(uint32_t wait_ms);
