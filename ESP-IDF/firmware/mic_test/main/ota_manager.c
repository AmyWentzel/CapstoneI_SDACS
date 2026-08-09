/*
 * SDACS module: Asynchronous firmware OTA update manager
 *
 * Purpose:
 *   Runs ESP HTTPS OTA in a dedicated task, publishes progress/status, and coordinates device state around update execution.
 *
 * Design note:
 *   OTA is isolated from capture operation and reports the running firmware version for traceability.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#include "ota_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "device_state.h"
#include "node_identity.h"
#include "sdacs_config.h"
#include "wifi_mqtt.h"

typedef struct {
    char url[256];
    char target_version[64];
} ota_request_t;

static const char *TAG = "ota_manager";
static volatile bool s_running = false;

static void publish_ota_status(const char *state, const char *message, const char *target_version)
{
    char topic[160];
    char payload[320];

    if (sdacs_build_topic(topic, sizeof(topic), "/ota/status") != ESP_OK) {
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{\"node_id\":\"%s\",\"record_type\":\"ota_status\",\"timestamp\":%" PRIi64 ","
                 "\"mode\":\"%s\",\"fw_version\":\"%s\","
                 "\"target_version\":\"%s\",\"state\":\"%s\",\"message\":\"%s\"}",
                 sdacs_node_id(),
                 (int64_t)esp_timer_get_time(),
                 device_state_to_str(device_state_get()),
                 SDACS_FW_VERSION,
                 target_version ? target_version : "",
                 state ? state : "",
                 message ? message : "") >= (int)sizeof(payload)) {
        return;
    }

    (void)wifi_mqtt_publish_status_json(topic, payload);
}

static void ota_task(void *arg)
{
    ota_request_t *req = (ota_request_t *)arg;
    esp_err_t err = ESP_FAIL;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    esp_http_client_config_t http_config = {
        .url = req->url,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    device_state_set(SDACS_MODE_OTA);
    (void)wifi_mqtt_set_ota_state(false, true);
    (void)wifi_mqtt_publish_heartbeat("updating");
    publish_ota_status("starting", "OTA starting", req->target_version);

    if (running && boot) {
        ESP_LOGI(TAG, "Running partition=%s boot partition=%s",
                 running->label, boot->label);
    }
    if (app_desc) {
        ESP_LOGI(TAG, "Current firmware version=%s", app_desc->version);
    }

    publish_ota_status("downloading", "Downloading image", req->target_version);
    err = esp_https_ota(&ota_config);
    if (err == ESP_OK) {
        (void)wifi_mqtt_publish_heartbeat("restarting");
        publish_ota_status("verifying", "Verifying and rebooting", req->target_version);
        s_running = false;
        free(req);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    device_state_set(SDACS_MODE_IDLE);
    (void)wifi_mqtt_set_ota_state(true, false);
    (void)wifi_mqtt_publish_heartbeat("ota_failed");
    publish_ota_status("failed", esp_err_to_name(err), req->target_version);
    s_running = false;
    free(req);
    vTaskDelete(NULL);
}

esp_err_t ota_manager_start(const char *url, const char *target_version)
{
    ota_request_t *req = NULL;
    BaseType_t ok;

    if (!url || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    req = calloc(1, sizeof(*req));
    if (!req) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(req->url, url, sizeof(req->url) - 1);
    if (target_version) {
        strncpy(req->target_version, target_version, sizeof(req->target_version) - 1);
    }

    s_running = true;
    ok = xTaskCreate(ota_task, "ota_task", 8192, req, 5, NULL);
    if (ok != pdPASS) {
        s_running = false;
        free(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ota_manager_is_running(void)
{
    return s_running;
}
