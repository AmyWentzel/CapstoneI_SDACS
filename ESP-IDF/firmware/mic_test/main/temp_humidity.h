#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float temp_c;
    float rh_percent;
    uint32_t sample_count;
    uint32_t error_count;
    int64_t last_sample_time_us;   // esp_timer_get_time()
    bool valid;
} temp_humidity_reading_t;

/**
 * Start periodic sampling of an HDC302x temperature/humidity sensor over I2C.
 *
 * This module:
 *  - initializes I2C master (safe if already installed)
 *  - runs a FreeRTOS task that triggers a measurement and reads results
 *  - validates CRC for each 16-bit word
 *  - converts raw values to degC and %RH
 *  - logs readings to serial (ESP_LOGI)
 *
 * @param i2c_port      I2C port number (e.g., I2C_NUM_0)
 * @param sda_gpio      SDA GPIO
 * @param scl_gpio      SCL GPIO
 * @param i2c_freq_hz   I2C clock (100k or 400k)
 * @param sensor_addr   7-bit I2C addr (HDC302x typically 0x44..0x47 depending on straps)
 * @param period_ms     Sample period (2000–5000ms recommended)
 * @return true if started successfully
 */
bool temp_humidity_start(int i2c_port,
                         int sda_gpio,
                         int scl_gpio,
                         uint32_t i2c_freq_hz,
                         uint8_t sensor_addr,
                         uint32_t period_ms);

/**
 * Get the most recent reading (thread-safe).
 * @param out Output struct
 * @return true if at least one valid reading has been captured
 */
bool temp_humidity_get_latest(temp_humidity_reading_t *out);

/**
 * Stop the module task (optional).
 */
void temp_humidity_stop(void);

#ifdef __cplusplus
}
#endif