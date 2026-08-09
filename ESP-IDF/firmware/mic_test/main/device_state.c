/*
 * SDACS module: Thread-safe logical device state
 *
 * Purpose:
 *   Tracks whether the node is idle, capturing, performing OTA, or in another mutually exclusive operation.
 *
 * Design note:
 *   Central state checks prevent conflicting operations such as OTA during capture.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#include "device_state.h"

static volatile sdacs_mode_t s_mode = SDACS_MODE_BOOT;

void device_state_init(void)
{
    s_mode = SDACS_MODE_IDLE;
}

sdacs_mode_t device_state_get(void)
{
    return s_mode;
}

void device_state_set(sdacs_mode_t mode)
{
    s_mode = mode;
}

bool device_state_is_idle(void)
{
    return s_mode == SDACS_MODE_IDLE;
}

bool device_state_can_start_capture(void)
{
    return s_mode == SDACS_MODE_IDLE;
}

bool device_state_can_start_ota(void)
{
    return s_mode == SDACS_MODE_IDLE;
}

const char *device_state_to_str(sdacs_mode_t mode)
{
    switch (mode) {
        case SDACS_MODE_BOOT: return "boot";
        case SDACS_MODE_IDLE: return "idle";
        case SDACS_MODE_ARMED: return "armed";
        case SDACS_MODE_CAPTURING: return "capturing";
        case SDACS_MODE_FINALIZING: return "finalizing";
        case SDACS_MODE_COMPLETE: return "complete";
        case SDACS_MODE_OTA: return "ota";
        case SDACS_MODE_ERROR: return "error";
        default: return "unknown";
    }
}
