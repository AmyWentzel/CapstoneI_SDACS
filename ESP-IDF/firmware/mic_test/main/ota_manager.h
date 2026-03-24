#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ota_manager_start(const char *url, const char *target_version);
bool ota_manager_is_running(void);
