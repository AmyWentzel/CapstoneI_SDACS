#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float voltage_v;
    float soc_percent;
    uint16_t version_raw;
    bool valid;
} fuel_gauge_reading_t;

esp_err_t fuel_gauge_init(void);
esp_err_t fuel_gauge_read(fuel_gauge_reading_t *out);
esp_err_t fuel_gauge_get_voltage(float *voltage_v);
esp_err_t fuel_gauge_get_soc(float *soc_percent);
esp_err_t fuel_gauge_get_version(uint16_t *version_raw);

#ifdef __cplusplus
}
#endif