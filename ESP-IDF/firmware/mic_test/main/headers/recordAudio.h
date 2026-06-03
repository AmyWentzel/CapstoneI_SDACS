#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

bool recordAudio_init(void);
bool recordAudio_capture(uint32_t seconds);
bool recordAudio_shutdown(void);

esp_err_t audio_input_init(void);
esp_err_t audio_input_read_s24(int32_t *dst,
                               size_t max_samples,
                               size_t *samples_read,
                               uint32_t timeout_ms);
void audio_input_deinit(void);
