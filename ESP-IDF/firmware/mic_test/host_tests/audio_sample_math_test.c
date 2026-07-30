#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio_sample_math.h"

static void expect_sample(const char *name,
                          int32_t actual,
                          int32_t expected,
                          bool clipped,
                          bool expected_clipped)
{
    if (actual != expected || clipped != expected_clipped) {
        fprintf(stderr,
                "%s: sample=%ld expected=%ld clipped=%d expected_clipped=%d\n",
                name,
                (long)actual,
                (long)expected,
                clipped,
                expected_clipped);
        exit(1);
    }
}

int main(void)
{
    bool clipped = false;
    int32_t sample = 0;
    unsigned normal_clip_count = 0;
    unsigned overload_clip_count = 0;

    sample = audio_apply_gain_shift_s24(12345, 3U, &clipped);
    expect_sample("spectral positive", sample, 98760, clipped, false);
    sample = audio_apply_gain_shift_s24(-12345, 3U, &clipped);
    expect_sample("spectral negative", sample, -98760, clipped, false);
    sample = audio_apply_gain_shift_s24(12345, 4U, &clipped);
    expect_sample("scene positive", sample, 197520, clipped, false);
    sample = audio_apply_gain_shift_s24(-12345, 4U, &clipped);
    expect_sample("scene negative", sample, -197520, clipped, false);
    sample = audio_apply_gain_shift_s24(1048576, 3U, &clipped);
    expect_sample("positive saturation", sample, AUDIO_S24_MAX, clipped, true);
    sample = audio_apply_gain_shift_s24(-1048577, 3U, &clipped);
    expect_sample("negative saturation", sample, AUDIO_S24_MIN, clipped, true);
    sample = audio_round_float_to_s24(12.6f, &clipped);
    expect_sample("positive rounding", sample, 13, clipped, false);
    sample = audio_round_float_to_s24(-12.6f, &clipped);
    expect_sample("negative rounding", sample, -13, clipped, false);

    const int32_t normal_samples[] = {-200000, -1, 0, 1, 200000};
    for (size_t i = 0; i < sizeof(normal_samples) / sizeof(normal_samples[0]); ++i) {
        (void)audio_apply_gain_shift_s24(normal_samples[i], 3U, &clipped);
        normal_clip_count += clipped ? 1U : 0U;
    }
    const int32_t overload_samples[] = {-2000000, 2000000};
    for (size_t i = 0; i < sizeof(overload_samples) / sizeof(overload_samples[0]); ++i) {
        (void)audio_apply_gain_shift_s24(overload_samples[i], 3U, &clipped);
        overload_clip_count += clipped ? 1U : 0U;
    }
    if (normal_clip_count != 0U || overload_clip_count != 2U) {
        fprintf(stderr,
                "clip counts: normal=%u expected=0 overload=%u expected=2\n",
                normal_clip_count,
                overload_clip_count);
        return 1;
    }

    puts("audio sample math tests passed");
    return 0;
}
