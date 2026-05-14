#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char node_id[17];
    uint32_t seq;
    uint64_t t_us;
    float rms;
    float dbfs;
    float db_spl;
    float f_peak_hz;
    int32_t p2p_raw;
    int zeros;
    uint32_t n;
} sdacs_features_t;

esp_err_t mqtt_publish_start(void);
esp_err_t mqtt_publish_wait_connected(uint32_t timeout_ms);
bool mqtt_publish_try_send_features(const sdacs_features_t *f);
esp_err_t mqtt_publish_raw(const char *topic, const void *payload, size_t len, int qos, int retain);
bool mqtt_publish_is_connected(void);

