/*
 * SDACS module: Public BLE locator interface
 *
 * Purpose:
 *   Exposes synchronous boot advertisement and asynchronous re-advertise requests.
 *
 * Design note:
 *   The API prevents overlapping BLE locator operations.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t sdacs_ble_locator_advertise_for(const char *node_id, uint32_t duration_ms);
esp_err_t sdacs_ble_locator_request_advertise(uint32_t duration_ms);
bool sdacs_ble_locator_is_busy(void);
