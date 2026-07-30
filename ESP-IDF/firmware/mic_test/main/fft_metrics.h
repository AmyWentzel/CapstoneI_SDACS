#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float rms_norm;
    float dbfs;
    float laeq_db;
    float peak_db;
    float fft_peak_hz;
    float f_peak_full_hz;
    float f_peak_acoustic_hz;
    float f_peak_acoustic_energy;
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
    float fft_low_energy;
    float fft_mid_energy;
    float fft_high_energy;
    float fft_total_energy;
    float fft_low_ratio;
    float fft_mid_ratio;
    float fft_high_ratio;
    int32_t peak_abs;
    int32_t p2p_raw;
    uint32_t zeros;
    uint32_t sample_count;
} audio_metrics_t;

typedef enum {
    FFT_METRICS_PATH_SPECTRAL = 0,
    FFT_METRICS_PATH_SCENE = 1,
    FFT_METRICS_PATH_COUNT = 2,
} fft_metrics_path_t;

esp_err_t fft_metrics_init(void);
void fft_metrics_push_samples(const int32_t *samples, size_t n);
void fft_metrics_accumulate_block(const int32_t *samples, size_t n);
bool fft_metrics_compute_and_reset(audio_metrics_t *out, float cal_offset_db);
void fft_metrics_reset(void);

void fft_metrics_push_samples_for_path(fft_metrics_path_t path,
                                       const int32_t *samples,
                                       size_t n);
void fft_metrics_accumulate_block_for_path(fft_metrics_path_t path,
                                           const int32_t *samples,
                                           size_t n);
void fft_metrics_ingest_block_for_path(fft_metrics_path_t path,
                                       const int32_t *samples,
                                       size_t n);
bool fft_metrics_compute_and_reset_for_path(fft_metrics_path_t path,
                                            audio_metrics_t *out,
                                            float cal_offset_db);
void fft_metrics_reset_path(fft_metrics_path_t path);
