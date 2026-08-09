/*
 * SDACS module: Public time synchronization interface
 *
 * Purpose:
 *   Exposes validity checks, UTC formatting, logging, and bounded SNTP synchronization.
 *
 * Design note:
 *   Consumers can distinguish synchronized timestamps from uptime-only timing.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool time_sync_is_valid(void);
void time_sync_get_iso8601(char *out, size_t out_len);
void time_sync_log_current(const char *prefix);
void time_sync_try_sntp(uint32_t wait_ms);
