#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "hpf_filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double measure_gain_db(double frequency_hz)
{
    const double sample_rate_hz = 48000.0;
    const int total_samples = 48000;
    const int settle_samples = 24000;
    hpf_filter_t filter;
    double input_sum_sq = 0.0;
    double output_sum_sq = 0.0;

    if (!hpf_filter_init(&filter, (float)sample_rate_hz, 150.0f)) {
        fprintf(stderr, "HPF initialization failed\n");
        exit(2);
    }

    for (int n = 0; n < total_samples; ++n) {
        const double input = sin(2.0 * M_PI * frequency_hz * n / sample_rate_hz);
        const double output = hpf_filter_process(&filter, (float)input);
        if (n >= settle_samples) {
            input_sum_sq += input * input;
            output_sum_sq += output * output;
        }
    }

    return 10.0 * log10(output_sum_sq / input_sum_sq);
}

static void verify_reset(void)
{
    hpf_filter_t filter;
    if (!hpf_filter_init(&filter, 48000.0f, 150.0f)) {
        fprintf(stderr, "HPF initialization failed\n");
        exit(2);
    }
    (void)hpf_filter_process(&filter, 100000.0f);
    hpf_filter_reset(&filter);
    for (size_t i = 0; i < 2U; ++i) {
        if (filter.sections[i].s1 != 0.0f || filter.sections[i].s2 != 0.0f) {
            fprintf(stderr, "HPF reset left non-zero state\n");
            exit(1);
        }
    }
}

int main(void)
{
    const double frequencies[] = {46.875, 93.75, 140.625, 150.0, 250.0, 1000.0};
    const double expected_db[] = {-40.4, -16.4, -4.5, -3.0, -0.1, 0.0};
    const double tolerance_db[] = {2.0, 1.0, 0.5, 0.35, 0.35, 0.2};
    const size_t count = sizeof(frequencies) / sizeof(frequencies[0]);

    verify_reset();
    for (size_t i = 0; i < count; ++i) {
        const double gain_db = measure_gain_db(frequencies[i]);
        printf("%8.3f Hz  %8.3f dB\n", frequencies[i], gain_db);
        if (fabs(gain_db - expected_db[i]) > tolerance_db[i]) {
            fprintf(stderr, "HPF response outside tolerance at %.3f Hz\n", frequencies[i]);
            return 1;
        }
    }
    puts("HPF response and reset tests passed");
    return 0;
}
