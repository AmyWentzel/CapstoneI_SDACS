#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t raw0;
    uint32_t raw1;
    uint32_t raw_min;
    uint32_t raw_max;
    int32_t sample0;
    int32_t min_sample;
    int32_t max_sample;
    int32_t peak_abs;
    uint32_t zero_count;
    uint32_t nonzero_count;
    uint32_t repeated_count;
    size_t bytes_read;
    size_t raw_words_read;
    size_t samples_read;
} audio_input_debug_t;

typedef struct {
    int32_t min;
    int32_t max;
    int32_t peak_abs;
    int32_t p2p;
    float rms;
    float dbfs;
} audio_input_conversion_diag_t;

typedef struct {
    uint32_t raw_word0;
    uint32_t raw_word1;
    uint32_t raw_word_min;
    uint32_t raw_word_max;
    uint32_t raw_word_nonzero_count;
    uint32_t raw_word_repeated_count;
    uint32_t converted_zeros;
    uint32_t sample_count;
    int32_t converted_sample0;
    int32_t converted_sample_min;
    int32_t converted_sample_max;
    int32_t converted_peak_abs;
    int32_t converted_p2p_raw;
    float software_gain;
    int32_t pre_gain_peak_abs;
    int32_t post_gain_peak_abs;
    uint32_t clipped_sample_count;
    audio_input_conversion_diag_t pre_gain;
    audio_input_conversion_diag_t current;
    audio_input_conversion_diag_t shift8;
    audio_input_conversion_diag_t low24;
    audio_input_conversion_diag_t shift16;
    float dbfs_norm_24bit;
    float dbfs_norm_32bit;
    uint32_t dbfs_normalization_bits;
} audio_input_raw_diagnostics_t;

typedef struct {
    uint32_t total_read_calls;
    uint32_t successful_reads;
    uint32_t timeouts;
    uint32_t errors;
    uint64_t total_bytes_read;
    uint64_t total_raw_words;
    uint64_t total_selected_samples;
    uint64_t total_read_elapsed_us;
    uint64_t total_timeout_elapsed_us;
    uint32_t min_read_elapsed_us;
    uint32_t max_read_elapsed_us;
    uint32_t min_timeout_elapsed_us;
    uint32_t max_timeout_elapsed_us;
    uint32_t timeout_immediate_count;
    uint32_t rx_restarts;
    uint32_t rx_recreates;
    esp_err_t last_error;
    char last_error_name[32];
} audio_input_counters_t;

typedef struct {
    uint32_t duration_ms;
    uint32_t elapsed_ms;
    uint32_t read_calls;
    uint32_t successful_reads;
    uint32_t timeout_reads;
    uint32_t error_reads;
    uint32_t bytes_per_successful_read;
    uint32_t selected_samples_per_successful_read;
    uint64_t total_bytes_read;
    uint64_t total_raw_words;
    uint64_t total_selected_samples;
    float effective_selected_sample_rate_hz;
    float effective_raw_word_rate_hz;
    uint32_t first_success_ms;
    uint32_t max_gap_between_successful_reads_ms;
    uint32_t min_gap_between_successful_reads_ms;
    float avg_gap_between_successful_reads_ms;
    uint32_t min_read_elapsed_us;
    uint32_t max_read_elapsed_us;
    float avg_read_elapsed_us;
    uint32_t min_timeout_elapsed_us;
    uint32_t max_timeout_elapsed_us;
    float avg_timeout_elapsed_us;
    uint32_t timeout_immediate_count;
    char last_error_name[32];
    audio_input_debug_t debug;
} audio_input_i2s_diag_result_t;

esp_err_t audio_input_init(void);
esp_err_t audio_input_prepare_for_capture(void);
esp_err_t audio_input_recover_rx(const char *reason);
esp_err_t audio_input_read_s24(int32_t *dst,
                               size_t max_samples,
                               size_t *samples_read,
                               uint32_t timeout_ms);
bool audio_input_get_last_debug(audio_input_debug_t *out);
bool audio_input_get_raw_diagnostics(audio_input_raw_diagnostics_t *out);
void audio_input_set_raw_diagnostics_enabled(bool enabled);
void audio_input_get_counters(audio_input_counters_t *out);
void audio_input_reset_counters(void);
void audio_input_reset_raw_diagnostics(void);
esp_err_t audio_input_run_i2s_diag(uint32_t duration_ms, audio_input_i2s_diag_result_t *out);
void audio_input_deinit(void);
