#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "run_storage.h"

typedef struct {
    run_storage_t *storage;
    const char *node_id;
    const char *base_topic;
    const char *request_id;
    uint32_t delay_ms;
    float cal_offset_db;
    uint32_t record_seconds;
    bool sd_enabled;
    bool sd_writes_enabled;
    char storage_mode[24];
} capture_context_t;

esp_err_t capture_task_start(const capture_context_t *ctx);
