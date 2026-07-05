#include "fft_metrics.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
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
    float peak_energy;
    float acoustic_peak_hz;
    float acoustic_peak_energy;
    float tone_1khz_energy;
    float tone_1khz_ratio;
    float tone_1khz_peak_hz;
    float tone_1khz_peak_energy;
    float tone_1khz_local_total_energy;
    float tone_1khz_local_noise_energy;
    float tone_1khz_local_noise_avg_energy;
    float tone_1khz_local_ratio;
    float tone_1khz_contrast_db;
    bool tone_1khz_ratio_hit;
    bool tone_1khz_peak_hit;
    bool tone_1khz_contrast_hit;
    bool tone_1khz_detected;
    int tone_1khz_local_noise_bins;
    int tone_1khz_band_bins;
    float acoustic_band_energy;
    float room_band_total_energy;
    float low_rumble_energy;
    float low_rumble_ratio;
    float band_sub_energy;
    float band_bass_energy;
    float band_low_mid_energy;
    float band_mid_energy;
    float band_presence_energy;
    float band_high_energy;
    float band_sub_ratio;
    float band_bass_ratio;
    float band_low_mid_ratio;
    float band_mid_ratio;
    float band_presence_ratio;
    float band_high_ratio;
    float band_sub_peak_hz;
    float band_bass_peak_hz;
    float band_low_mid_peak_hz;
    float band_mid_peak_hz;
    float band_presence_peak_hz;
    float band_high_peak_hz;
    float band_sub_peak_energy;
    float band_bass_peak_energy;
    float band_low_mid_peak_energy;
    float band_mid_peak_energy;
    float band_presence_peak_energy;
    float band_high_peak_energy;
    float dominant_band_ratio;
    char dominant_band_name[24];
    float dominant_band_peak_hz;
    float low_energy;
    float mid_energy;
    float high_energy;
    float total_energy;
    float low_ratio;
    float mid_ratio;
    float high_ratio;
} fft_band_metrics_t;

static void update_band(float freq_hz,
                        float energy,
                        float min_hz,
                        float max_hz,
                        bool include_max,
                        float *band_energy,
                        float *band_peak_hz,
                        float *band_peak_energy)
{
    bool in_band = freq_hz >= min_hz &&
        (include_max ? (freq_hz <= max_hz) : (freq_hz < max_hz));

    if (!in_band) {
        return;
    }

    *band_energy += energy;
    if (energy > *band_peak_energy) {
        *band_peak_energy = energy;
        *band_peak_hz = freq_hz;
    }
}

static void set_dominant_band(fft_band_metrics_t *metrics)
{
    const char *name = "sub";
    float ratio = metrics->band_sub_ratio;
    float peak_hz = metrics->band_sub_peak_hz;

    if (metrics->band_bass_ratio > ratio) {
        name = "bass";
        ratio = metrics->band_bass_ratio;
        peak_hz = metrics->band_bass_peak_hz;
    }
    if (metrics->band_low_mid_ratio > ratio) {
        name = "low_mid";
        ratio = metrics->band_low_mid_ratio;
        peak_hz = metrics->band_low_mid_peak_hz;
    }
    if (metrics->band_mid_ratio > ratio) {
        name = "mid";
        ratio = metrics->band_mid_ratio;
        peak_hz = metrics->band_mid_peak_hz;
    }
    if (metrics->band_presence_ratio > ratio) {
        name = "presence";
        ratio = metrics->band_presence_ratio;
        peak_hz = metrics->band_presence_peak_hz;
    }
    if (metrics->band_high_ratio > ratio) {
        name = "high";
        ratio = metrics->band_high_ratio;
        peak_hz = metrics->band_high_peak_hz;
    }

    snprintf(metrics->dominant_band_name, sizeof(metrics->dominant_band_name), "%s", name);
    metrics->dominant_band_ratio = ratio;
    metrics->dominant_band_peak_hz = peak_hz;
}

static fft_band_metrics_t compute_fft_band_metrics(void)
{
    const float ratio_epsilon = 1e-12f;
    const float tone_low_hz = SDACS_TONE_1KHZ_CENTER_HZ - SDACS_TONE_1KHZ_BAND_HALF_WIDTH_HZ;
    const float tone_high_hz = SDACS_TONE_1KHZ_CENTER_HZ + SDACS_TONE_1KHZ_BAND_HALF_WIDTH_HZ;
    fft_band_metrics_t metrics = {0};
    int start = s_fft.fft_index;
    float peak_energy = 0.0f;
    int peak_bin = 0;
    float acoustic_peak_energy = 0.0f;
    int acoustic_peak_bin = 0;
    float tone_peak_energy = 0.0f;
    int tone_peak_bin = 0;

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

        if (freq_hz >= SDACS_FFT_ACOUSTIC_PEAK_MIN_HZ &&
            freq_hz <= SDACS_FFT_ACOUSTIC_PEAK_MAX_HZ) {
            metrics.acoustic_band_energy += energy;
            if (energy > acoustic_peak_energy) {
                acoustic_peak_energy = energy;
                acoustic_peak_bin = i;
            }
        } else if (freq_hz < SDACS_FFT_ACOUSTIC_PEAK_MIN_HZ) {
            metrics.low_rumble_energy += energy;
        }

        const bool in_tone_band = freq_hz >= tone_low_hz && freq_hz <= tone_high_hz;
        const bool in_local_region =
            freq_hz >= SDACS_TONE_1KHZ_CONTRAST_MIN_HZ &&
            freq_hz <= SDACS_TONE_1KHZ_CONTRAST_MAX_HZ;

        if (in_tone_band) {
            metrics.tone_1khz_energy += energy;
            metrics.tone_1khz_band_bins++;
            if (energy > tone_peak_energy) {
                tone_peak_energy = energy;
                tone_peak_bin = i;
            }
        }
        if (in_local_region && !in_tone_band) {
            metrics.tone_1khz_local_noise_energy += energy;
            metrics.tone_1khz_local_noise_bins++;
        }

        update_band(freq_hz, energy,
                    SDACS_BAND_SUB_MIN_HZ, SDACS_BAND_SUB_MAX_HZ, false,
                    &metrics.band_sub_energy,
                    &metrics.band_sub_peak_hz,
                    &metrics.band_sub_peak_energy);
        update_band(freq_hz, energy,
                    SDACS_BAND_BASS_MIN_HZ, SDACS_BAND_BASS_MAX_HZ, false,
                    &metrics.band_bass_energy,
                    &metrics.band_bass_peak_hz,
                    &metrics.band_bass_peak_energy);
        update_band(freq_hz, energy,
                    SDACS_BAND_LOW_MID_MIN_HZ, SDACS_BAND_LOW_MID_MAX_HZ, false,
                    &metrics.band_low_mid_energy,
                    &metrics.band_low_mid_peak_hz,
                    &metrics.band_low_mid_peak_energy);
        update_band(freq_hz, energy,
                    SDACS_BAND_MID_MIN_HZ, SDACS_BAND_MID_MAX_HZ, false,
                    &metrics.band_mid_energy,
                    &metrics.band_mid_peak_hz,
                    &metrics.band_mid_peak_energy);
        update_band(freq_hz, energy,
                    SDACS_BAND_PRESENCE_MIN_HZ, SDACS_BAND_PRESENCE_MAX_HZ, false,
                    &metrics.band_presence_energy,
                    &metrics.band_presence_peak_hz,
                    &metrics.band_presence_peak_energy);
        update_band(freq_hz, energy,
                    SDACS_BAND_HIGH_MIN_HZ, SDACS_BAND_HIGH_MAX_HZ, true,
                    &metrics.band_high_energy,
                    &metrics.band_high_peak_hz,
                    &metrics.band_high_peak_energy);

        if (freq_hz < 250.0f) {
            metrics.low_energy += energy;
        } else if (freq_hz < 2000.0f) {
            metrics.mid_energy += energy;
        } else if (freq_hz <= 8000.0f) {
            metrics.high_energy += energy;
        }
    }

    metrics.peak_hz = ((float)peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
    metrics.peak_energy = peak_energy;
    metrics.acoustic_peak_hz = ((float)acoustic_peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
    metrics.acoustic_peak_energy = acoustic_peak_energy;
    metrics.tone_1khz_peak_hz = ((float)tone_peak_bin * SDACS_SAMPLE_RATE_HZ) / SDACS_FFT_SIZE;
    metrics.tone_1khz_peak_energy = tone_peak_energy;
    metrics.tone_1khz_local_total_energy =
        metrics.tone_1khz_energy + metrics.tone_1khz_local_noise_energy;
    metrics.tone_1khz_local_noise_avg_energy =
        (metrics.tone_1khz_local_noise_bins > 0)
            ? (metrics.tone_1khz_local_noise_energy / (float)metrics.tone_1khz_local_noise_bins)
            : 0.0f;
    metrics.tone_1khz_local_ratio =
        metrics.tone_1khz_energy / fmaxf(metrics.tone_1khz_local_noise_energy, 1e-20f);
    metrics.tone_1khz_contrast_db =
        10.0f * log10f(fmaxf(metrics.tone_1khz_local_ratio, 1e-20f));
    metrics.total_energy = metrics.low_energy + metrics.mid_energy + metrics.high_energy;
    metrics.room_band_total_energy =
        metrics.band_sub_energy +
        metrics.band_bass_energy +
        metrics.band_low_mid_energy +
        metrics.band_mid_energy +
        metrics.band_presence_energy +
        metrics.band_high_energy;
    if (metrics.total_energy > ratio_epsilon) {
        metrics.low_ratio = metrics.low_energy / metrics.total_energy;
        metrics.mid_ratio = metrics.mid_energy / metrics.total_energy;
        metrics.high_ratio = metrics.high_energy / metrics.total_energy;
        metrics.low_rumble_ratio = metrics.low_rumble_energy / metrics.total_energy;
    }
    if (metrics.acoustic_band_energy > ratio_epsilon) {
        metrics.tone_1khz_ratio = metrics.tone_1khz_energy / metrics.acoustic_band_energy;
    }
    if (metrics.room_band_total_energy > ratio_epsilon) {
        metrics.band_sub_ratio = metrics.band_sub_energy / metrics.room_band_total_energy;
        metrics.band_bass_ratio = metrics.band_bass_energy / metrics.room_band_total_energy;
        metrics.band_low_mid_ratio = metrics.band_low_mid_energy / metrics.room_band_total_energy;
        metrics.band_mid_ratio = metrics.band_mid_energy / metrics.room_band_total_energy;
        metrics.band_presence_ratio = metrics.band_presence_energy / metrics.room_band_total_energy;
        metrics.band_high_ratio = metrics.band_high_energy / metrics.room_band_total_energy;
    }
    /*
     * Diagnostic tone detection threshold. Tune after real quiet/tone captures
     * rather than treating this as an SPL calibration decision.
     */
    bool ratio_hit = metrics.tone_1khz_ratio >= SDACS_TONE_1KHZ_RATIO_THRESHOLD;
    bool tone_peak_hit = fabsf(metrics.tone_1khz_peak_hz - SDACS_TONE_1KHZ_CENTER_HZ) <=
        SDACS_TONE_1KHZ_PEAK_TOLERANCE_HZ;
    bool contrast_hit =
        metrics.tone_1khz_contrast_db >= SDACS_TONE_1KHZ_CONTRAST_THRESHOLD_DB &&
        metrics.tone_1khz_local_ratio >= SDACS_TONE_1KHZ_MIN_LOCAL_RATIO;
    metrics.tone_1khz_ratio_hit = ratio_hit;
    metrics.tone_1khz_peak_hit = tone_peak_hit;
    metrics.tone_1khz_contrast_hit = contrast_hit;
    metrics.tone_1khz_detected = tone_peak_hit && (ratio_hit || contrast_hit);
    set_dominant_band(&metrics);

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

    /*
     * Production samples are normalized against signed 24-bit full scale.
     * The active conversion mode is selected in audio_input.c. For this build,
     * SHIFT8 is the production feature path, while LOW24 remains diagnostic.
     * SPL calibration must not be finalized until quiet/tone captures prove the
     * selected conversion and slot are correct.
     */
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
        .f_peak_full_hz = fft_metrics.peak_hz,
        .f_peak_acoustic_hz = fft_metrics.acoustic_peak_hz,
        .f_peak_acoustic_energy = fft_metrics.acoustic_peak_energy,
        .tone_1khz_energy = fft_metrics.tone_1khz_energy,
        .tone_1khz_ratio = fft_metrics.tone_1khz_ratio,
        .tone_1khz_peak_hz = fft_metrics.tone_1khz_peak_hz,
        .tone_1khz_peak_energy = fft_metrics.tone_1khz_peak_energy,
        .tone_1khz_local_total_energy = fft_metrics.tone_1khz_local_total_energy,
        .tone_1khz_local_noise_energy = fft_metrics.tone_1khz_local_noise_energy,
        .tone_1khz_local_noise_avg_energy = fft_metrics.tone_1khz_local_noise_avg_energy,
        .tone_1khz_local_ratio = fft_metrics.tone_1khz_local_ratio,
        .tone_1khz_contrast_db = fft_metrics.tone_1khz_contrast_db,
        .tone_1khz_ratio_hit = fft_metrics.tone_1khz_ratio_hit,
        .tone_1khz_peak_hit = fft_metrics.tone_1khz_peak_hit,
        .tone_1khz_contrast_hit = fft_metrics.tone_1khz_contrast_hit,
        .tone_1khz_detected = fft_metrics.tone_1khz_detected,
        .tone_1khz_local_noise_bins = fft_metrics.tone_1khz_local_noise_bins,
        .tone_1khz_band_bins = fft_metrics.tone_1khz_band_bins,
        .acoustic_band_energy = fft_metrics.acoustic_band_energy,
        .room_band_total_energy = fft_metrics.room_band_total_energy,
        .low_rumble_energy = fft_metrics.low_rumble_energy,
        .low_rumble_ratio = fft_metrics.low_rumble_ratio,
        .band_sub_energy = fft_metrics.band_sub_energy,
        .band_bass_energy = fft_metrics.band_bass_energy,
        .band_low_mid_energy = fft_metrics.band_low_mid_energy,
        .band_mid_energy = fft_metrics.band_mid_energy,
        .band_presence_energy = fft_metrics.band_presence_energy,
        .band_high_energy = fft_metrics.band_high_energy,
        .band_sub_ratio = fft_metrics.band_sub_ratio,
        .band_bass_ratio = fft_metrics.band_bass_ratio,
        .band_low_mid_ratio = fft_metrics.band_low_mid_ratio,
        .band_mid_ratio = fft_metrics.band_mid_ratio,
        .band_presence_ratio = fft_metrics.band_presence_ratio,
        .band_high_ratio = fft_metrics.band_high_ratio,
        .band_sub_peak_hz = fft_metrics.band_sub_peak_hz,
        .band_bass_peak_hz = fft_metrics.band_bass_peak_hz,
        .band_low_mid_peak_hz = fft_metrics.band_low_mid_peak_hz,
        .band_mid_peak_hz = fft_metrics.band_mid_peak_hz,
        .band_presence_peak_hz = fft_metrics.band_presence_peak_hz,
        .band_high_peak_hz = fft_metrics.band_high_peak_hz,
        .band_sub_peak_energy = fft_metrics.band_sub_peak_energy,
        .band_bass_peak_energy = fft_metrics.band_bass_peak_energy,
        .band_low_mid_peak_energy = fft_metrics.band_low_mid_peak_energy,
        .band_mid_peak_energy = fft_metrics.band_mid_peak_energy,
        .band_presence_peak_energy = fft_metrics.band_presence_peak_energy,
        .band_high_peak_energy = fft_metrics.band_high_peak_energy,
        .dominant_band_ratio = fft_metrics.dominant_band_ratio,
        .dominant_band_peak_hz = fft_metrics.dominant_band_peak_hz,
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
    snprintf(out->dominant_band_name, sizeof(out->dominant_band_name), "%s",
             fft_metrics.dominant_band_name);

    s_fft.sum_sq = 0.0;
    s_fft.peak_abs = 0;
    s_fft.min_sample = INT32_MAX;
    s_fft.max_sample = INT32_MIN;
    s_fft.zeros = 0;
    s_fft.count = 0;
    return true;
}
