#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

bool temp_humidity_start(int i2c_port,
                         int sda_gpio,
                         int scl_gpio,
                         uint32_t i2c_freq_hz,
                         uint8_t sensor_addr,
                         uint32_t period_ms);

bool temp_humidity_get_latest(temp_humidity_reading_t *out);
bool temp_humidity_get_history(temp_humidity_reading_t *out, size_t max_count, size_t *out_count);

// Publish a single snapshot of the latest reading to MQTT with phase marker.
bool temp_humidity_publish_latest_once(const char *phase);

void temp_humidity_stop(void);

#ifdef __cplusplus
}
#endif
