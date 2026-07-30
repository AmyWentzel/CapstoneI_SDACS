#include "hpf_filter.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool configure_high_pass_biquad(hpf_biquad_t *section,
                                       float sample_rate_hz,
                                       float cutoff_hz,
                                       double q)
{
    if (!section || !isfinite(sample_rate_hz) || !isfinite(cutoff_hz) ||
        !isfinite(q) || sample_rate_hz <= 0.0f || cutoff_hz <= 0.0f ||
        cutoff_hz >= (sample_rate_hz * 0.5f) || q <= 0.0) {
        return false;
    }

    /* Calculate coefficients in double precision once, then process samples
     * in float using the ESP32-S3 single-precision FPU.
     */
    const double omega = 2.0 * M_PI * (double)cutoff_hz / (double)sample_rate_hz;
    const double cosine = cos(omega);
    const double sine = sin(omega);
    const double alpha = sine / (2.0 * q);
    const double a0 = 1.0 + alpha;

    section->b0 = (float)(((1.0 + cosine) * 0.5) / a0);
    section->b1 = (float)((-(1.0 + cosine)) / a0);
    section->b2 = section->b0;
    section->a1 = (float)((-2.0 * cosine) / a0);
    section->a2 = (float)((1.0 - alpha) / a0);
    section->s1 = 0.0f;
    section->s2 = 0.0f;
    return true;
}

bool hpf_filter_init(hpf_filter_t *filter, float sample_rate_hz, float cutoff_hz)
{
    if (!filter) {
        return false;
    }

    memset(filter, 0, sizeof(*filter));

    /* Fourth-order Butterworth pole-pair Q values. */
    static const double q_values[2] = {
        0.5411961001461970,
        1.3065629648763766,
    };

    for (size_t i = 0; i < 2U; ++i) {
        if (!configure_high_pass_biquad(
                &filter->sections[i], sample_rate_hz, cutoff_hz, q_values[i])) {
            memset(filter, 0, sizeof(*filter));
            return false;
        }
    }

    filter->sample_rate_hz = sample_rate_hz;
    filter->cutoff_hz = cutoff_hz;
    filter->initialized = true;
    return true;
}

void hpf_filter_reset(hpf_filter_t *filter)
{
    if (!filter) {
        return;
    }

    for (size_t i = 0; i < 2U; ++i) {
        filter->sections[i].s1 = 0.0f;
        filter->sections[i].s2 = 0.0f;
    }
}

static float process_biquad(hpf_biquad_t *section, float sample)
{
    /* Transposed direct-form II is compact and numerically well behaved for
     * the low 150 Hz cutoff at 48 kHz.
     */
    const float output = (section->b0 * sample) + section->s1;
    section->s1 = (section->b1 * sample) - (section->a1 * output) + section->s2;
    section->s2 = (section->b2 * sample) - (section->a2 * output);
    return output;
}

float hpf_filter_process(hpf_filter_t *filter, float sample)
{
    if (!filter || !filter->initialized || !isfinite(sample)) {
        return sample;
    }

    float output = sample;
    for (size_t i = 0; i < 2U; ++i) {
        output = process_biquad(&filter->sections[i], output);
    }
    return output;
}
