#include "fft_metrics.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_dsp.h"

#include "sdacs_config.h"

typedef struct {
    float fft_in[SDACS_FFT_SIZE * 2];
    float hann_window[SDACS_FFT_SIZE];
    int32_t fft_buffer[SDACS_FFT_SIZE];
    int fft_index;
    double sum_sq;
    int32_t peak_abs;
    int32_t min_sample;
    int32_t max_sample;
    uint32_t zeros;
    uint32_t count;
    bool initialized;
} fft_metrics_ctx_t;

static fft_metrics_ctx_t s_fft = {0};

typedef struct {
    float peak_hz;
    float low_energy;
    float mid_energy;
    float high_energy;
    float total_energy;
    float low_ratio;
    float mid_ratio;
    float high_ratio;
} fft_band_metrics_t;

static fft_band_metrics_t compute_fft_band_metrics(void)
{
    const float ratio_epsilon = 1e-12f;
    fft_band_metrics_t metrics = {0};
    int start = s_fft.fft_index;
    float peak_energy = 0.0f;
    int peak_bin = 0;

    for (int i = 0; i < SDACS_FFT_SIZE; ++i) {
        int buf_index = (start + i) % SDACS_FFT_SIZE;
        float sample = (float)s_fft.fft_buffer[buf_index] / 8388608.0f;
        s_fft.fft_in[2 * i] = sample * s_fft.hann_window[i];
        s_fft.fft_in[(2 * i) + 1] = 0.0f;
    }

    dsps_fft2r_fc32(s_fft.fft_in, SDACS_FFT_SIZE);
    dsps_bit_rev_fc32(s_fft.fft_in, SDACS_FFT_SIZE);
    dsps_cplx2reC_fc32(s_fft.fft_in, SDACS_FFT_SIZE);

    for (int i = 1; i < SDACS_FFT_SIZE / 2; ++i) {
        float real = s_fft.fft_in[2 * i];
        float imag = s_fft.fft_in[(2 * i) + 1];
        float energy = (real * real) + (imag * imag);
        float freq_hz = ((float)i * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;

        if (freq_hz < 20.0f) {
            continue;
        }

        if (energy > peak_energy) {
            peak_energy = energy;
            peak_bin = i;
        }

        if (freq_hz < 250.0f) {
            metrics.low_energy += energy;
        } else if (freq_hz < 2000.0f) {
            metrics.mid_energy += energy;
        } else if (freq_hz <= 8000.0f) {
            metrics.high_energy += energy;
        }
    }

    metrics.peak_hz = ((float)peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
    metrics.total_energy = metrics.low_energy + metrics.mid_energy + metrics.high_energy;
    if (metrics.total_energy > ratio_epsilon) {
        metrics.low_ratio = metrics.low_energy / metrics.total_energy;
        metrics.mid_ratio = metrics.mid_energy / metrics.total_energy;
        metrics.high_ratio = metrics.high_energy / metrics.total_energy;
    }

    return metrics;
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
    s_fft.min_sample = INT32_MAX;
    s_fft.max_sample = INT32_MIN;
    return ESP_OK;
}

void fft_metrics_reset(void)
{
    s_fft.fft_index = 0;
    memset(s_fft.fft_buffer, 0, sizeof(s_fft.fft_buffer));
    memset(s_fft.fft_in, 0, sizeof(s_fft.fft_in));
    s_fft.sum_sq = 0.0;
    s_fft.peak_abs = 0;
    s_fft.min_sample = INT32_MAX;
    s_fft.max_sample = INT32_MIN;
    s_fft.zeros = 0;
    s_fft.count = 0;
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
        if (sample < s_fft.min_sample) {
            s_fft.min_sample = sample;
        }
        if (sample > s_fft.max_sample) {
            s_fft.max_sample = sample;
        }
        if (sample == 0) {
            s_fft.zeros++;
        }
        s_fft.sum_sq += (double)sample * (double)sample;
        s_fft.count++;
    }
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
    fft_band_metrics_t fft_metrics = compute_fft_band_metrics();

    *out = (audio_metrics_t){
        .rms_norm = rms_norm,
        .dbfs = dbfs,
        .laeq_db = dbfs + cal_offset_db,
        .peak_db = 20.0f * log10f(peak_norm + 1e-12f) + cal_offset_db,
        .fft_peak_hz = fft_metrics.peak_hz,
        .fft_low_energy = fft_metrics.low_energy,
        .fft_mid_energy = fft_metrics.mid_energy,
        .fft_high_energy = fft_metrics.high_energy,
        .fft_total_energy = fft_metrics.total_energy,
        .fft_low_ratio = fft_metrics.low_ratio,
        .fft_mid_ratio = fft_metrics.mid_ratio,
        .fft_high_ratio = fft_metrics.high_ratio,
        .peak_abs = s_fft.peak_abs,
        .p2p_raw = s_fft.max_sample - s_fft.min_sample,
        .zeros = s_fft.zeros,
        .sample_count = s_fft.count,
    };

    s_fft.sum_sq = 0.0;
    s_fft.peak_abs = 0;
    s_fft.min_sample = INT32_MAX;
    s_fft.max_sample = INT32_MIN;
    s_fft.zeros = 0;
    s_fft.count = 0;
    return true;
}
