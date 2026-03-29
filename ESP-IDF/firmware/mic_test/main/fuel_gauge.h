#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float soc_percent;
    float voltage_v;
    float charge_rate_percent_per_hr;
    bool valid;
    uint32_t sample_count;
    uint32_t error_count;
    int64_t last_sample_time_us;
} fuel_gauge_reading_t;

bool fuel_gauge_start(int i2c_port,
                      uint8_t sensor_addr,
                      uint32_t period_ms);

bool fuel_gauge_get_latest(fuel_gauge_reading_t *out);
bool fuel_gauge_publish_latest_once(const char *phase);
void fuel_gauge_stop(void);

#ifdef __cplusplus
}
#endif
