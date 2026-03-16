#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float rms_norm;
    float dbfs;
    float laeq_db;
    float peak_db;
    float fft_peak_hz;
    int32_t peak_abs;
    uint32_t sample_count;
} audio_metrics_t;

esp_err_t fft_metrics_init(void);
void fft_metrics_push_samples(const int32_t *samples, size_t n);
void fft_metrics_accumulate_block(const int32_t *samples, size_t n);
bool fft_metrics_compute_and_reset(audio_metrics_t *out, float cal_offset_db);
