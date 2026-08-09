/*
 * SDACS module: Fourth-order Butterworth high-pass filter
 *
 * Purpose:
 *   Implements two cascaded second-order biquads configured for the scene-classifier high-pass cutoff.
 *
 * Design note:
 *   A fixed filter is used instead of AGC/adaptive preprocessing so model training and deployment see the same transfer function.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

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
                                       float q)
{
    if (!section || !isfinite(sample_rate_hz) || !isfinite(cutoff_hz) ||
        !isfinite(q) || sample_rate_hz <= 0.0f || cutoff_hz <= 0.0f ||
        cutoff_hz >= (sample_rate_hz * 0.5f) || q <= 0.0f) {
        return false;
    }

    const float omega = 2.0f * (float)M_PI * cutoff_hz / sample_rate_hz;
    const float cosine = cosf(omega);
    const float sine = sinf(omega);
    const float alpha = sine / (2.0f * q);
    const float a0 = 1.0f + alpha;

    section->b0 = ((1.0f + cosine) * 0.5f) / a0;
    section->b1 = (-(1.0f + cosine)) / a0;
    section->b2 = section->b0;
    section->a1 = (-2.0f * cosine) / a0;
    section->a2 = (1.0f - alpha) / a0;
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
    static const float q_values[2] = {
        0.5411961001461970f,
        1.3065629648763766f,
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
