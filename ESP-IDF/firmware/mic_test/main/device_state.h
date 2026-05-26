#pragma once

#include <stdbool.h>

typedef enum {
    SDACS_MODE_BOOT = 0,
    SDACS_MODE_IDLE,
    SDACS_MODE_ARMED,
    SDACS_MODE_CAPTURING,
    SDACS_MODE_FINALIZING,
    SDACS_MODE_COMPLETE,
    SDACS_MODE_OTA,
    SDACS_MODE_ERROR
} sdacs_mode_t;

void device_state_init(void);
sdacs_mode_t device_state_get(void);
void device_state_set(sdacs_mode_t mode);
bool device_state_is_idle(void);
bool device_state_can_start_capture(void);
bool device_state_can_start_ota(void);
const char *device_state_to_str(sdacs_mode_t mode);
