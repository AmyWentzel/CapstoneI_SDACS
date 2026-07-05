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
    bool tone_1khz_ratio_hit;
    bool tone_1khz_peak_hit;
    bool tone_1khz_detected;
    float acoustic_band_energy;
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

esp_err_t fft_metrics_init(void);
void fft_metrics_push_samples(const int32_t *samples, size_t n);
void fft_metrics_accumulate_block(const int32_t *samples, size_t n);
bool fft_metrics_compute_and_reset(audio_metrics_t *out, float cal_offset_db);
void fft_metrics_reset(void);
