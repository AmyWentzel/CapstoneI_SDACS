/*
 * SDACS module: Pure signed-24-bit sample math helpers
 *
 * Purpose:
 *   Provides rounding, gain-by-power-of-two, and saturation helpers that can be host-tested without ESP-IDF hardware dependencies.
 *
 * Design note:
 *   Separating arithmetic from the driver makes clipping behavior reproducible and unit-testable.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_S24_MIN (-8388608)
#define AUDIO_S24_MAX 8388607

static inline int32_t audio_saturate_s24_i64(int64_t sample, bool *clipped)
{
    if (sample > (int64_t)AUDIO_S24_MAX) {
        if (clipped) {
            *clipped = true;
        }
        return AUDIO_S24_MAX;
    }
    if (sample < (int64_t)AUDIO_S24_MIN) {
        if (clipped) {
            *clipped = true;
        }
        return AUDIO_S24_MIN;
    }
    if (clipped) {
        *clipped = false;
    }
    return (int32_t)sample;
}

static inline int32_t audio_apply_gain_shift_s24(int32_t sample,
                                                 unsigned shift,
                                                 bool *clipped)
{
    return audio_saturate_s24_i64((int64_t)sample * ((int64_t)1 << shift), clipped);
}

static inline int32_t audio_round_float_to_s24(float sample, bool *clipped)
{
    if (!(sample == sample)) {
        if (clipped) {
            *clipped = true;
        }
        return 0;
    }
    if (sample >= (float)AUDIO_S24_MAX) {
        if (clipped) {
            *clipped = sample > (float)AUDIO_S24_MAX;
        }
        return AUDIO_S24_MAX;
    }
    if (sample <= (float)AUDIO_S24_MIN) {
        if (clipped) {
            *clipped = sample < (float)AUDIO_S24_MIN;
        }
        return AUDIO_S24_MIN;
    }

    if (clipped) {
        *clipped = false;
    }
    return (int32_t)(sample + ((sample >= 0.0f) ? 0.5f : -0.5f));
}
