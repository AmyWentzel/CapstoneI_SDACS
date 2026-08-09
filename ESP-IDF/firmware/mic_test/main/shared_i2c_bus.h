/*
 * SDACS module: Public shared-I2C interface
 *
 * Purpose:
 *   Defines bus configuration and basic write/read/write-read transactions.
 *
 * Design note:
 *   Peripheral drivers depend on this interface rather than creating independent I2C buses.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int port;
    int sda_gpio;
    int scl_gpio;
    uint32_t freq_hz;
} shared_i2c_bus_config_t;

esp_err_t shared_i2c_bus_init(const shared_i2c_bus_config_t *cfg);
bool shared_i2c_bus_is_ready(void);
int shared_i2c_bus_port(void);
esp_err_t shared_i2c_write(int port, uint8_t addr, const uint8_t *data, size_t len, TickType_t timeout_ticks);
esp_err_t shared_i2c_read(int port, uint8_t addr, uint8_t *data, size_t len, TickType_t timeout_ticks);
esp_err_t shared_i2c_write_read(int port, uint8_t addr,
                                const uint8_t *tx_data, size_t tx_len,
                                uint8_t *rx_data, size_t rx_len,
                                TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
