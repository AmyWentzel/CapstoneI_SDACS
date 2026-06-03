#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "sdCard.h"

typedef struct {
    run_storage_t *storage;
    const char *node_id;
    const char *base_topic;
    float cal_offset_db;
    uint32_t record_seconds;
} capture_context_t;

esp_err_t capture_task_start(const capture_context_t *ctx);
bool capture_task_wait_complete(uint32_t timeout_ms);
