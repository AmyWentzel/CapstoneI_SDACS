#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef struct {
    char timestamp[32];
    char node_id[32];
    float laeq_db;
    float peak_db;
    float dbfs;
    float rms;
    float temp_c;
    float humidity;
    float fft_peak_hz;
    float fft_low_ratio;
    float fft_mid_ratio;
    float fft_high_ratio;
    float fft_total_energy;
} metrics_record_t;

typedef struct {
    sdmmc_card_t *card;
    char run_dir[160];
    char raw_path[256];
    char wav_path[256];
    char csv_path[256];
    char cal_csv_path[256];
    char cal_offset_path[256];
} run_storage_t;

esp_err_t run_storage_init(run_storage_t *rs);
esp_err_t run_storage_create_session(run_storage_t *rs, const char *node_id);
bool run_storage_append_raw(run_storage_t *rs, const int32_t *samples, size_t count);
bool run_storage_append_metrics(run_storage_t *rs, const metrics_record_t *rec);
esp_err_t run_storage_convert_raw_to_wav(run_storage_t *rs, uint32_t sample_rate_hz);
void run_storage_refresh_timestamps(run_storage_t *rs);
void run_storage_verify(run_storage_t *rs);
esp_err_t run_storage_get_file_size(const char *path, size_t *out_size);
esp_err_t run_storage_verify_capture(run_storage_t *rs, char *reason, size_t reason_sz,
                                     size_t *raw_bytes, size_t *wav_bytes, size_t *csv_bytes);
