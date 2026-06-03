#pragma once

#include <stdbool.h>
#include <stdint.h>

bool batteryLife_init(void);
int batteryLife_get_percent(void);
const char *batteryLife_get_level_pin(int battery_percent);
