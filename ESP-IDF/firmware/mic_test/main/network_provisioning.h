/*
 * SDACS module: Public network provisioning interface
 *
 * Purpose:
 *   Provides the boot-time default synchronization entry point.
 *
 * Design note:
 *   Called before Wi-Fi/MQTT startup.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include "esp_err.h"

esp_err_t network_provisioning_apply_defaults(void);
