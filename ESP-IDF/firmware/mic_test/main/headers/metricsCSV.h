#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tempHumidity.h"

typedef struct {
    uint32_t measurement;
    char timestamp[32];
    char node_id[32];
    float laeq_db;
    float peak_db;
    float dbfs;
    float rms;
    float temp_c;
    float humidity;
    float fft_peak_hz;
} metrics_record_t;

bool metricsCSV_init(void);
bool metricsCSV_append(const metrics_record_t *rec);
const char *metricsCSV_get_path(void);
