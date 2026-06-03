#include "node_identity.h"

#include <stdio.h>
#include <string.h>

#include "sdacs_config.h"

const char *sdacs_node_id(void)
{
    return SDACS_NODE_ID;
}

bool sdacs_node_id_is_valid(const char *id)
{
    return id &&
           (strcmp(id, "node01") == 0 ||
            strcmp(id, "node02") == 0 ||
            strcmp(id, "node03") == 0 ||
            strcmp(id, "node04") == 0);
}

esp_err_t sdacs_get_mqtt_base(char *out, size_t out_sz)
{
    int n = 0;

    if (!out || out_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    if (!sdacs_node_id_is_valid(sdacs_node_id())) {
        return ESP_ERR_INVALID_STATE;
    }

    n = snprintf(out, out_sz, "sdacs/node/%s", sdacs_node_id());
    if (n <= 0 || n >= (int)out_sz) {
        out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t sdacs_build_topic(char *out, size_t out_sz, const char *suffix)
{
    char base[32];
    int n = 0;
    esp_err_t err = ESP_OK;

    if (!out || out_sz == 0 || !suffix) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    err = sdacs_get_mqtt_base(base, sizeof(base));
    if (err != ESP_OK) {
        return err;
    }

    n = snprintf(out, out_sz, "%s%s", base, suffix);
    if (n <= 0 || n >= (int)out_sz) {
        out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t sdacs_get_ble_name(char *out, size_t out_sz)
{
    int n = 0;

    if (!out || out_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    if (!sdacs_node_id_is_valid(sdacs_node_id())) {
        return ESP_ERR_INVALID_STATE;
    }

    n = snprintf(out, out_sz, "SDACS-%s", sdacs_node_id());
    if (n <= 0 || n >= (int)out_sz) {
        out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t sdacs_get_ble_mfg_payload(char *out, size_t out_sz)
{
    int n = 0;

    if (!out || out_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out[0] = '\0';
    if (!sdacs_node_id_is_valid(sdacs_node_id())) {
        return ESP_ERR_INVALID_STATE;
    }

    n = snprintf(out, out_sz, "SDACS:%s", sdacs_node_id());
    if (n <= 0 || n >= (int)out_sz) {
        out[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}
