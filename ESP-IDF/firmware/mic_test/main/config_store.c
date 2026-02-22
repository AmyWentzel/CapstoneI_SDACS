#include "config_store.h"

#include <stdbool.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define CFG_NS "sdacs_cfg"

#define KEY_WIFI_SSID  "wifi_ssid"
#define KEY_WIFI_PASS  "wifi_pass"
#define KEY_MQTT_URI   "mqtt_uri"
#define KEY_MQTT_TOPIC "mqtt_topic"
#define KEY_NODE_ID    "node_id"
#define KEY_SAMPLE_HZ  "sample_hz"
#define KEY_CAL_MDB    "cal_mdb"

#define DEFAULT_WIFI_SSID  ""
#define DEFAULT_WIFI_PASS  ""
#define DEFAULT_MQTT_URI   "mqtt://192.168.1.50"
#define DEFAULT_MQTT_TOPIC "sdacs/node/node01/features"
#define DEFAULT_NODE_ID    "node01"
#define DEFAULT_SAMPLE_HZ  48000U
#define DEFAULT_CAL_MDB    94000

static bool s_loaded = false;
static char s_wifi_ssid[CONFIG_STORE_MAX_WIFI_SSID_LEN + 1];
static char s_wifi_pass[CONFIG_STORE_MAX_WIFI_PASS_LEN + 1];
static char s_mqtt_uri[CONFIG_STORE_MAX_MQTT_URI_LEN + 1];
static char s_mqtt_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 1];
static char s_node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1];
static uint32_t s_sample_rate_hz = DEFAULT_SAMPLE_HZ;
static int32_t s_cal_mdb = DEFAULT_CAL_MDB;

static void copy_default(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || !dst_sz || !src) {
        return;
    }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static esp_err_t read_str_or_default(nvs_handle_t nvs, const char *key,
                                     char *out, size_t out_sz,
                                     const char *default_value)
{
    size_t len = out_sz;
    esp_err_t err = nvs_get_str(nvs, key, out, &len);
    if (err == ESP_OK) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        copy_default(out, out_sz, default_value);
        return ESP_OK;
    }
    copy_default(out, out_sz, default_value);
    return err;
}

static esp_err_t load_cache_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        copy_default(s_wifi_ssid, sizeof(s_wifi_ssid), DEFAULT_WIFI_SSID);
        copy_default(s_wifi_pass, sizeof(s_wifi_pass), DEFAULT_WIFI_PASS);
        copy_default(s_mqtt_uri, sizeof(s_mqtt_uri), DEFAULT_MQTT_URI);
        copy_default(s_mqtt_topic, sizeof(s_mqtt_topic), DEFAULT_MQTT_TOPIC);
        copy_default(s_node_id, sizeof(s_node_id), DEFAULT_NODE_ID);
        s_sample_rate_hz = DEFAULT_SAMPLE_HZ;
        s_cal_mdb = DEFAULT_CAL_MDB;
        s_loaded = true;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t first_err = ESP_OK;

    err = read_str_or_default(nvs, KEY_WIFI_SSID, s_wifi_ssid, sizeof(s_wifi_ssid), DEFAULT_WIFI_SSID);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = read_str_or_default(nvs, KEY_WIFI_PASS, s_wifi_pass, sizeof(s_wifi_pass), DEFAULT_WIFI_PASS);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = read_str_or_default(nvs, KEY_MQTT_URI, s_mqtt_uri, sizeof(s_mqtt_uri), DEFAULT_MQTT_URI);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = read_str_or_default(nvs, KEY_MQTT_TOPIC, s_mqtt_topic, sizeof(s_mqtt_topic), DEFAULT_MQTT_TOPIC);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = read_str_or_default(nvs, KEY_NODE_ID, s_node_id, sizeof(s_node_id), DEFAULT_NODE_ID);
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }

    err = nvs_get_u32(nvs, KEY_SAMPLE_HZ, &s_sample_rate_hz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_sample_rate_hz = DEFAULT_SAMPLE_HZ;
    } else if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }

    err = nvs_get_i32(nvs, KEY_CAL_MDB, &s_cal_mdb);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_cal_mdb = DEFAULT_CAL_MDB;
    } else if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }

    nvs_close(nvs);
    s_loaded = true;
    return first_err;
}

static esp_err_t ensure_loaded(void)
{
    if (s_loaded) {
        return ESP_OK;
    }
    return load_cache_from_nvs();
}

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    return ensure_loaded();
}

esp_err_t config_store_get_wifi(const char **ssid, const char **pass)
{
    if (!ssid || !pass) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_loaded();
    if (err != ESP_OK) {
        return err;
    }
    *ssid = s_wifi_ssid;
    *pass = s_wifi_pass;
    return ESP_OK;
}

esp_err_t config_store_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) > CONFIG_STORE_MAX_WIFI_SSID_LEN ||
        strlen(pass) > CONFIG_STORE_MAX_WIFI_PASS_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, KEY_WIFI_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    copy_default(s_wifi_ssid, sizeof(s_wifi_ssid), ssid);
    copy_default(s_wifi_pass, sizeof(s_wifi_pass), pass);
    s_loaded = true;
    return ESP_OK;
}

esp_err_t config_store_get_mqtt(const char **broker_uri, const char **topic)
{
    if (!broker_uri || !topic) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_loaded();
    if (err != ESP_OK) {
        return err;
    }
    *broker_uri = s_mqtt_uri;
    *topic = s_mqtt_topic;
    return ESP_OK;
}

esp_err_t config_store_set_mqtt(const char *broker_uri, const char *topic)
{
    if (!broker_uri || !topic) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(broker_uri) > CONFIG_STORE_MAX_MQTT_URI_LEN ||
        strlen(topic) > CONFIG_STORE_MAX_MQTT_TOPIC_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, KEY_MQTT_URI, broker_uri);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, KEY_MQTT_TOPIC, topic);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    copy_default(s_mqtt_uri, sizeof(s_mqtt_uri), broker_uri);
    copy_default(s_mqtt_topic, sizeof(s_mqtt_topic), topic);
    s_loaded = true;
    return ESP_OK;
}

esp_err_t config_store_get_node_id(const char **node_id)
{
    if (!node_id) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_loaded();
    if (err != ESP_OK) {
        return err;
    }
    *node_id = s_node_id;
    return ESP_OK;
}

esp_err_t config_store_set_node_id(const char *node_id)
{
    if (!node_id) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(node_id) > CONFIG_STORE_MAX_NODE_ID_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, KEY_NODE_ID, node_id);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    copy_default(s_node_id, sizeof(s_node_id), node_id);
    s_loaded = true;
    return ESP_OK;
}

esp_err_t config_store_get_sample_rate_hz(uint32_t *sample_rate_hz)
{
    if (!sample_rate_hz) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_loaded();
    if (err != ESP_OK) {
        return err;
    }
    *sample_rate_hz = s_sample_rate_hz;
    return ESP_OK;
}

esp_err_t config_store_set_sample_rate_hz(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(nvs, KEY_SAMPLE_HZ, sample_rate_hz);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    s_sample_rate_hz = sample_rate_hz;
    s_loaded = true;
    return ESP_OK;
}

esp_err_t config_store_get_cal_offset_db(float *cal_offset_db)
{
    if (!cal_offset_db) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_loaded();
    if (err != ESP_OK) {
        return err;
    }
    *cal_offset_db = (float)s_cal_mdb / 1000.0f;
    return ESP_OK;
}

esp_err_t config_store_set_cal_offset_db(float cal_offset_db)
{
    int32_t cal_mdb = (int32_t)(cal_offset_db * 1000.0f);

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(nvs, KEY_CAL_MDB, cal_mdb);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        return err;
    }

    s_cal_mdb = cal_mdb;
    s_loaded = true;
    return ESP_OK;
}
