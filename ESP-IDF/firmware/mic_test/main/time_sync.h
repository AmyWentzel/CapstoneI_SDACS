#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool time_sync_is_valid(void);
void time_sync_get_iso8601(char *out, size_t out_len);
void time_sync_log_current(const char *prefix);
void time_sync_try_sntp(uint32_t wait_ms);
