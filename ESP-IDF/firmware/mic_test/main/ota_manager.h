/*
 * SDACS module: Public OTA manager interface
 *
 * Purpose:
 *   Starts an OTA request and reports whether an update is already active.
 *
 * Design note:
 *   Command parsing remains outside the OTA subsystem.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ota_manager_start(const char *url, const char *target_version);
bool ota_manager_is_running(void);
