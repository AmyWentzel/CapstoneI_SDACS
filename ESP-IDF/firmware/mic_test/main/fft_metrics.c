#include "fft_metrics.h"

#include <math.h>

#include "esp_check.h"
#include "esp_dsp.h"

#include "sdacs_config.h"

typedef struct {
    float fft_in[SDACS_FFT_SIZE * 2];
    float fft_mag[SDACS_FFT_SIZE];
    float hann_window[SDACS_FFT_SIZE];
    int32_t fft_buffer[SDACS_FFT_SIZE];
    int fft_index;
    double sum_sq;
    int32_t peak_abs;
    uint32_t count;
    bool initialized;
} fft_metrics_ctx_t;

static fft_metrics_ctx_t s_fft = {0};

static float compute_fft_peak_hz(void)
{
    int start = s_fft.fft_index;

    for (int i = 0; i < SDACS_FFT_SIZE; ++i) {
        int buf_index = (start + i) % SDACS_FFT_SIZE;
        float sample = (float)s_fft.fft_buffer[buf_index] / 8388608.0f;
        s_fft.fft_in[2 * i] = sample * s_fft.hann_window[i];
        s_fft.fft_in[(2 * i) + 1] = 0.0f;
    }

    dsps_fft2r_fc32(s_fft.fft_in, SDACS_FFT_SIZE);
    dsps_bit_rev_fc32(s_fft.fft_in, SDACS_FFT_SIZE);
    dsps_cplx2reC_fc32(s_fft.fft_in, SDACS_FFT_SIZE);

    for (int i = 5; i < SDACS_FFT_SIZE / 2; ++i) {
        float real = s_fft.fft_in[2 * i];
        float imag = s_fft.fft_in[(2 * i) + 1];
        s_fft.fft_mag[i] = sqrtf((real * real) + (imag * imag));
    }

    int peak_bin = 5;
    float peak_val = s_fft.fft_mag[5];
    for (int i = 6; i < SDACS_FFT_SIZE / 2; ++i) {
        if (s_fft.fft_mag[i] > peak_val) {
            peak_val = s_fft.fft_mag[i];
            peak_bin = i;
        }
    }

    return ((float)peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
}

esp_err_t fft_metrics_init(void)
{
    if (s_fft.initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(dsps_fft2r_init_fc32(NULL, SDACS_FFT_SIZE), "fft_metrics", "FFT init failed");
    for (int i = 0; i < SDACS_FFT_SIZE; ++i) {
        s_fft.hann_window[i] =
            0.5f * (1.0f - cosf((2.0f * (float)M_PI * i) / (SDACS_FFT_SIZE - 1)));
    }

    s_fft.initialized = true;
    return ESP_OK;
}

void fft_metrics_push_samples(const int32_t *samples, size_t n)
{
    if (!samples) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        s_fft.fft_buffer[s_fft.fft_index++] = samples[i];
        if (s_fft.fft_index >= SDACS_FFT_SIZE) {
            s_fft.fft_index = 0;
        }
    }
}

void fft_metrics_accumulate_block(const int32_t *samples, size_t n)
{
    if (!samples) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        int32_t sample = samples[i];
        int32_t abs_sample = (sample < 0) ? -sample : sample;

        if (abs_sample > s_fft.peak_abs) {
            s_fft.peak_abs = abs_sample;
        }
        s_fft.sum_sq += (double)sample * (double)sample;
        s_fft.count++;
    }
}

static float compute_fft_peak_hz_from_samples(const int32_t *samples, size_t count)
{
    if (!samples || count < SDACS_FFT_SIZE) {
        return NAN;
    }

    float fft_in[SDACS_FFT_SIZE * 2];
    float fft_mag[SDACS_FFT_SIZE];

    for (int i = 0; i < SDACS_FFT_SIZE; ++i) {
        float sample = (float)samples[i] / 8388608.0f;
        fft_in[2 * i] = sample * s_fft.hann_window[i];
        fft_in[(2 * i) + 1] = 0.0f;
    }

    dsps_fft2r_fc32(fft_in, SDACS_FFT_SIZE);
    dsps_bit_rev_fc32(fft_in, SDACS_FFT_SIZE);
    dsps_cplx2reC_fc32(fft_in, SDACS_FFT_SIZE);

    for (int i = 5; i < SDACS_FFT_SIZE / 2; ++i) {
        float real = fft_in[2 * i];
        float imag = fft_in[(2 * i) + 1];
        fft_mag[i] = sqrtf((real * real) + (imag * imag));
    }

    int peak_bin = 5;
    float peak_val = fft_mag[5];
    for (int i = 6; i < SDACS_FFT_SIZE / 2; ++i) {
        if (fft_mag[i] > peak_val) {
            peak_val = fft_mag[i];
            peak_bin = i;
        }
    }

    return ((float)peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
}

bool fft_metrics_compute_metrics_block(const int32_t *samples, size_t n, audio_metrics_t *out, float cal_offset_db)
{
    if (!samples || n == 0 || !out) {
        return false;
    }

    double sum_sq = 0.0;
    int32_t peak_abs = 0;
    for (size_t i = 0; i < n; ++i) {
        int32_t sample = samples[i];
        int32_t abs_sample = sample < 0 ? -sample : sample;
        if (abs_sample > peak_abs) {
            peak_abs = abs_sample;
        }
        sum_sq += (double)sample * (double)sample;
    }

    float rms = sqrtf((float)(sum_sq / (double)n));
    float rms_norm = rms / 8388608.0f;
    float dbfs = 20.0f * log10f(rms_norm + 1e-12f);
    float peak_norm = (float)peak_abs / 8388608.0f;
    const int32_t *fft_samples = samples;
    if (n > SDACS_FFT_SIZE) {
        fft_samples = samples + ((n - SDACS_FFT_SIZE) / 2);
    }

    *out = (audio_metrics_t){
        .rms_norm = rms_norm,
        .dbfs = dbfs,
        .laeq_db = dbfs + cal_offset_db,
        .peak_db = 20.0f * log10f(peak_norm + 1e-12f) + cal_offset_db,
        .fft_peak_hz = compute_fft_peak_hz_from_samples(fft_samples, n),
        .peak_abs = peak_abs,
        .sample_count = (uint32_t)n,
    };
    return true;
}

bool fft_metrics_compute_and_reset(audio_metrics_t *out, float cal_offset_db)
{
    if (!out || s_fft.count == 0) {
        return false;
    }

    float rms = sqrtf((float)(s_fft.sum_sq / (double)s_fft.count));
    float rms_norm = rms / 8388608.0f;
    float dbfs = 20.0f * log10f(rms_norm + 1e-12f);
    float peak_norm = (float)s_fft.peak_abs / 8388608.0f;

    *out = (audio_metrics_t){
        .rms_norm = rms_norm,
        .dbfs = dbfs,
        .laeq_db = dbfs + cal_offset_db,
        .peak_db = 20.0f * log10f(peak_norm + 1e-12f) + cal_offset_db,
        .fft_peak_hz = compute_fft_peak_hz(),
        .peak_abs = s_fft.peak_abs,
        .sample_count = s_fft.count,
    };

    s_fft.sum_sq = 0.0;
    s_fft.peak_abs = 0;
    s_fft.count = 0;
    return true;
}
