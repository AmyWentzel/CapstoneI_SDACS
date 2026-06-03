#include "audioAnalysis.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_dsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "metricsCSV.h"
#include "sdCard.h"
#include "network.h"
#include "sdacs_config.h"
#include "tempHumidity.h"

static const char *TAG = "audioAnalysis";
static metrics_record_t s_summary_record;

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

    ESP_RETURN_ON_ERROR(dsps_fft2r_init_fc32(NULL, SDACS_FFT_SIZE), TAG, "FFT init failed");
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

    for (int i = 0; i < SDACS_FFT_SIZE; ++i) {
        float sample = (float)samples[i] / 8388608.0f;
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

static bool read_wav_header(FILE *f, uint32_t *sample_rate, uint16_t *bits_per_sample, uint16_t *channels, uint32_t *data_size)
{
    if (f == NULL || sample_rate == NULL || bits_per_sample == NULL || channels == NULL || data_size == NULL) {
        return false;
    }

    char riff[4];
    fread(riff, 1, 4, f);
    if (strncmp(riff, "RIFF", 4) != 0) {
        return false;
    }

    fseek(f, 20, SEEK_SET);
    fread(channels, sizeof(*channels), 1, f);
    fread(sample_rate, sizeof(*sample_rate), 1, f);
    fseek(f, 34, SEEK_SET);
    fread(bits_per_sample, sizeof(*bits_per_sample), 1, f);
    fseek(f, 40, SEEK_SET);
    fread(data_size, sizeof(*data_size), 1, f);
    fseek(f, 44, SEEK_SET);
    return true;
}

static float db_from_amplitude(float normalized)
{
    const float min_value = 1e-12f;
    return 20.0f * log10f(fmaxf(normalized, min_value));
}

static float normalize_sample(int32_t sample)
{
    return sample / 8388608.0f;
}

bool audioAnalysis_analyze_wav(float calibration_offset_db)
{
    const char *path = sdCard_get_wav_path();
    if (path == NULL) {
        ESP_LOGE(TAG, "WAV path is not available");
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Unable to open WAV file: %s", path);
        return false;
    }

    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint16_t channels = 0;
    uint32_t data_size = 0;
    if (!read_wav_header(f, &sample_rate, &bits_per_sample, &channels, &data_size)) {
        ESP_LOGE(TAG, "WAV header invalid");
        fclose(f);
        return false;
    }

    if (bits_per_sample != 24 || channels != 1) {
        ESP_LOGW(TAG, "Unexpected WAV format: %u-bit, %u channels", bits_per_sample, channels);
    }

    const size_t frame_size = (bits_per_sample / 8) * channels;
    const uint32_t window_ms = 500;
    const size_t window_bytes = (size_t)((uint64_t)sample_rate * frame_size * window_ms / 1000ULL);
    if (window_bytes == 0) {
        ESP_LOGE(TAG, "Invalid audio window size");
        fclose(f);
        return false;
    }

    uint8_t *window_buffer = malloc(window_bytes);
    if (window_buffer == NULL) {
        ESP_LOGE(TAG, "Unable to allocate audio analysis buffer");
        fclose(f);
        return false;
    }

    size_t max_samples = window_bytes / frame_size;
    int32_t *window_samples = malloc(max_samples * sizeof(int32_t));
    if (window_samples == NULL) {
        ESP_LOGE(TAG, "Unable to allocate audio analysis sample buffer");
        free(window_buffer);
        fclose(f);
        return false;
    }

    double total_sq = 0.0;
    double total_samples = 0.0;
    float max_peak = 0.0f;
    metrics_record_t last_record = {0};
    size_t rows_written = 0;

    while (true) {
        size_t read_bytes = fread(window_buffer, 1, window_bytes, f);
        if (read_bytes == 0) {
            break;
        }

        size_t sample_count = read_bytes / frame_size;
        if (sample_count == 0) {
            continue;
        }

        double window_sq = 0.0;
        for (size_t i = 0; i < sample_count; ++i) {
            uint32_t raw_sample = (window_buffer[i * frame_size + 0] << 0) |
                                  (window_buffer[i * frame_size + 1] << 8) |
                                  (window_buffer[i * frame_size + 2] << 16);
            int32_t signed_sample = (raw_sample & 0x800000) ? (raw_sample | 0xFF000000) : raw_sample;
            window_samples[i] = signed_sample;
            float normalized = normalize_sample(signed_sample);
            float abs_value = fabsf(normalized);
            window_sq += (double)normalized * (double)normalized;
            if (abs_value > max_peak) {
                max_peak = abs_value;
            }
        }

        audio_metrics_t metrics = {0};
        if (!fft_metrics_compute_metrics_block(window_samples, sample_count, &metrics, calibration_offset_db)) {
            ESP_LOGW(TAG, "Unable to compute audio metrics for window %u", (unsigned)(rows_written + 1));
            continue;
        }

        temp_humidity_reading_t latest = {0};
        tempHumidity_get_latest(&latest);

        metrics_record_t rec = {0};
        rec.measurement = rows_written + 1;
        rec.laeq_db = metrics.laeq_db;
        rec.peak_db = metrics.peak_db;
        rec.dbfs = metrics.dbfs;
        rec.rms = metrics.rms_norm;
        rec.temp_c = latest.valid ? latest.temp_c : NAN;
        rec.humidity = latest.valid ? latest.humidity : NAN;
        rec.fft_peak_hz = metrics.fft_peak_hz;

        if (metricsCSV_append(&rec)) {
            rows_written++;
        }
        last_record = rec;

        total_sq += window_sq;
        total_samples += sample_count;
    }

    free(window_samples);

    free(window_buffer);
    fclose(f);

    if (rows_written == 0) {
        ESP_LOGW(TAG, "No CSV rows written during WAV analysis");
        return false;
    }

    ESP_LOGI(TAG, "Audio analysis wrote %u CSV rows", (unsigned)rows_written);

    if (total_samples > 0.0) {
        float overall_rms = sqrtf(total_sq / total_samples);
        float overall_dbfs = db_from_amplitude(overall_rms);
        s_summary_record = last_record;
        s_summary_record.laeq_db = overall_dbfs + calibration_offset_db;
        s_summary_record.dbfs = overall_dbfs;
        s_summary_record.rms = overall_rms;
        s_summary_record.peak_db = db_from_amplitude(max_peak);
    }

    return true;
}

bool audioAnalysis_init(void)
{
    memset(&s_summary_record, 0, sizeof(s_summary_record));
    return true;
}

bool audioAnalysis_get_latest_summary(metrics_record_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = s_summary_record;
    return true;
}
