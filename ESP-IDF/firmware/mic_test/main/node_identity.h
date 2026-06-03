#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDACS_NODE_ID_MAX_LEN 16

const char *sdacs_node_id(void);
bool sdacs_node_id_is_valid(const char *id);
esp_err_t sdacs_build_topic(char *out, size_t out_sz, const char *suffix);
esp_err_t sdacs_get_mqtt_base(char *out, size_t out_sz);
esp_err_t sdacs_get_ble_name(char *out, size_t out_sz);
esp_err_t sdacs_get_ble_mfg_payload(char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
