#pragma once

#include <stdbool.h>

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
    float s1;
    float s2;
} hpf_biquad_t;

typedef struct {
    hpf_biquad_t sections[2];
    float sample_rate_hz;
    float cutoff_hz;
    bool initialized;
} hpf_filter_t;

/**
 * Configure a fourth-order Butterworth high-pass filter as two cascaded
 * second-order biquads. State is reset during initialization.
 */
bool hpf_filter_init(hpf_filter_t *filter, float sample_rate_hz, float cutoff_hz);

/** Reset only the delay state while preserving the configured coefficients. */
void hpf_filter_reset(hpf_filter_t *filter);

/** Process one sample and return the filtered value. */
float hpf_filter_process(hpf_filter_t *filter, float sample);
