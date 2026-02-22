#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_STORE_MAX_WIFI_SSID_LEN 32
#define CONFIG_STORE_MAX_WIFI_PASS_LEN 64
#define CONFIG_STORE_MAX_MQTT_URI_LEN 128
#define CONFIG_STORE_MAX_MQTT_TOPIC_LEN 128
#define CONFIG_STORE_MAX_NODE_ID_LEN 16

esp_err_t config_store_init(void);

esp_err_t config_store_get_wifi(const char **ssid, const char **pass);
esp_err_t config_store_set_wifi(const char *ssid, const char *pass);

esp_err_t config_store_get_mqtt(const char **broker_uri, const char **topic);
esp_err_t config_store_set_mqtt(const char *broker_uri, const char *topic);

esp_err_t config_store_get_node_id(const char **node_id);
esp_err_t config_store_set_node_id(const char *node_id);

esp_err_t config_store_get_sample_rate_hz(uint32_t *sample_rate_hz);
esp_err_t config_store_set_sample_rate_hz(uint32_t sample_rate_hz);

esp_err_t config_store_get_cal_offset_db(float *cal_offset_db);
esp_err_t config_store_set_cal_offset_db(float cal_offset_db);

#ifdef __cplusplus
}
#endif
