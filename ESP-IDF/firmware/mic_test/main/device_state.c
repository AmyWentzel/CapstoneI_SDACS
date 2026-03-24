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
        case SDACS_MODE_CAPTURING: return "capturing";
        case SDACS_MODE_OTA: return "ota";
        case SDACS_MODE_ERROR: return "error";
        default: return "unknown";
    }
}
