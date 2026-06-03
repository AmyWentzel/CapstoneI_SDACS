#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "metricsCSV.h"
#include "sdmmc_cmd.h"

typedef struct {
    sdmmc_card_t *card;
    FILE *raw_file;
    char run_dir[160];
    char raw_path[256];
    char wav_path[256];
    char csv_path[256];
    char cal_csv_path[256];
    char cal_offset_path[256];
} run_storage_t;

bool sdCard_init(void);
bool sdCard_create_session(void);
bool sdCard_open_raw(void);
bool sdCard_append_raw(const void *data, size_t len);
bool sdCard_close_raw(void);
bool sdCard_convert_raw_to_wav(uint32_t sample_rate_hz);
bool sdCard_verify_run(void);
const char *sdCard_get_run_dir(void);
const char *sdCard_get_wav_path(void);
const char *sdCard_get_metrics_path(void);

esp_err_t run_storage_init(run_storage_t *rs);
esp_err_t run_storage_create_session(run_storage_t *rs, const char *node_id);
bool run_storage_begin_raw(run_storage_t *rs);
bool run_storage_append_raw(run_storage_t *rs, const int32_t *samples, size_t count);
bool run_storage_end_raw(run_storage_t *rs);
bool run_storage_append_metrics(run_storage_t *rs, const metrics_record_t *rec);
esp_err_t run_storage_convert_raw_to_wav(run_storage_t *rs, uint32_t sample_rate_hz);
void run_storage_refresh_timestamps(run_storage_t *rs);
void run_storage_verify(run_storage_t *rs);
