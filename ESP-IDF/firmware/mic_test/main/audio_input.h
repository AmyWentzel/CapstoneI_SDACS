#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t raw0;
    int32_t sample0;
    int32_t min_sample;
    int32_t max_sample;
    uint32_t zero_count;
    size_t bytes_read;
    size_t samples_read;
} audio_input_debug_t;

esp_err_t audio_input_init(void);
esp_err_t audio_input_read_s24(int32_t *dst,
                               size_t max_samples,
                               size_t *samples_read,
                               uint32_t timeout_ms);
bool audio_input_get_last_debug(audio_input_debug_t *out);
void audio_input_deinit(void);
