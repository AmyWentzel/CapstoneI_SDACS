#include "command_dispatcher.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "ble_locator.h"
#include "capture_task.h"
#include "config_store.h"
#include "device_state.h"
#include "fuel_gauge.h"
#include "ota_manager.h"
#include "sdacs_config.h"
#include "temp_humidity.h"
#include "wifi_mqtt.h"

static const char *TAG = "cmd_dispatch";

static run_storage_t *s_storage = NULL;
static char s_node_id[32];
static char s_base_topic[128];

static void build_status_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    if (s_base_topic[0] == '\0') {
        return;
    }

    if (snprintf(out, out_sz, "%s/status", s_base_topic) >= (int)out_sz) {
        out[0] = '\0';
    }
}

static void publish_response(const char *cmd, const char *request_id, const char *result, const char *reason)
{
    char topic[160];
    char payload[320];

    build_status_topic(topic, sizeof(topic));
    if (topic[0] == '\0') {
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{\"node_id\":\"%s\",\"record_type\":\"command_response\","
                 "\"timestamp\":%" PRIi64 ",\"fw_version\":\"%s\","
                 "\"mode\":\"%s\",\"ota_capable\":true,"
                 "\"cmd\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\",\"request_id\":\"%s\"}",
                 s_node_id[0] ? s_node_id : SDACS_NODE_ID,
                 (int64_t)esp_timer_get_time(),
                 SDACS_FW_VERSION,
                 device_state_to_str(device_state_get()),
                 cmd ? cmd : "",
                 result ? result : "",
                 reason ? reason : "",
                 request_id ? request_id : "") >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Status payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Status publish failed: %s", esp_err_to_name(err));
    }
}

static void publish_cal_offset_response(const char *request_id, float cal_offset_db)
{
    char topic[160];
    char payload[384];

    build_status_topic(topic, sizeof(topic));
    if (topic[0] == '\0') {
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{\"node_id\":\"%s\",\"record_type\":\"command_response\","
                 "\"timestamp\":%" PRIi64 ",\"fw_version\":\"%s\","
                 "\"mode\":\"%s\",\"ota_capable\":true,"
                 "\"cmd\":\"set_cal_offset\",\"result\":\"ok\",\"reason\":\"saved\","
                 "\"request_id\":\"%s\",\"cal_offset_db\":%.2f}",
                 s_node_id[0] ? s_node_id : SDACS_NODE_ID,
                 (int64_t)esp_timer_get_time(),
                 SDACS_FW_VERSION,
                 device_state_to_str(device_state_get()),
                 request_id ? request_id : "",
                 (double)cal_offset_db) >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Calibration response payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Calibration response publish failed: %s", esp_err_to_name(err));
    }
}

static void publish_capture_status(const char *request_id,
                                   sdacs_mode_t state,
                                   uint32_t delay_ms,
                                   uint32_t record_seconds,
                                   float cal_offset_db,
                                   const char *storage_mode,
                                   bool sd_enabled,
                                   bool sd_writes_enabled,
                                   const char *message)
{
    char topic[160];
    char payload[896];

    build_status_topic(topic, sizeof(topic));
    if (topic[0] == '\0') {
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{"
                 "\"node_id\":\"%s\","
                 "\"record_type\":\"capture_status\","
                 "\"timestamp\":%" PRIi64 ","
                 "\"fw_version\":\"%s\","
                 "\"request_id\":\"%s\","
                 "\"state\":\"%s\","
                 "\"delay_ms\":%u,"
                 "\"record_seconds\":%u,"
                 "\"cal_offset_db\":%.2f,"
                 "\"storage_mode\":\"%s\","
                 "\"sd_enabled\":%s,"
                 "\"sd_writes_enabled\":%s,"
                 "\"message\":\"%s\""
                 "}",
                 s_node_id[0] ? s_node_id : SDACS_NODE_ID,
                 (int64_t)esp_timer_get_time(),
                 SDACS_FW_VERSION,
                 request_id ? request_id : "",
                 device_state_to_str(state),
                 (unsigned)delay_ms,
                 (unsigned)record_seconds,
                 (double)cal_offset_db,
                 storage_mode ? storage_mode : "",
                 sd_enabled ? "true" : "false",
                 sd_writes_enabled ? "true" : "false",
                 message ? message : "") >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Capture status payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Capture status publish failed: %s", esp_err_to_name(err));
    }
}

static bool json_copy_string(const cJSON *obj, const char *key, char *out, size_t out_sz)
{
    const cJSON *item = NULL;

    if (!obj || !key || !out || out_sz == 0) {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || !item->valuestring) {
        out[0] = '\0';
        return false;
    }

    strncpy(out, item->valuestring, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
}

static bool json_copy_u32(const cJSON *obj, const char *key, uint32_t *out, bool required)
{
    const cJSON *item = NULL;

    if (!obj || !key || !out) {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) {
        return !required;
    }

    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > 4294967295.0) {
        return false;
    }

    *out = (uint32_t)item->valuedouble;
    return true;
}

#if SDACS_ENABLE_SD_STORAGE
static bool ensure_storage_ready(void)
{
    if (run_storage_is_mounted(s_storage)) {
        return true;
    }

    ESP_LOGW(TAG, "SD storage unavailable; retrying mount before capture");
    esp_err_t err = run_storage_init(s_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD storage retry failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "SD storage mounted after retry");
    return true;
}

static void build_storage_reason(char *out, size_t out_sz, const char *prefix)
{
    if (!out || out_sz == 0) {
        return;
    }

    snprintf(out,
             out_sz,
             "%s: %s %s",
             prefix ? prefix : "SD storage not mounted",
             run_storage_last_error_name(s_storage),
             run_storage_last_error_detail(s_storage));
}
#endif

static void publish_storage_status(const char *cmd,
                                   const char *request_id,
                                   const char *result,
                                   const char *reason)
{
    char topic[160];
    char payload[640];
    bool mounted = run_storage_is_mounted(s_storage);
    const char *last_error = run_storage_last_error_name(s_storage);
    const char *detail = run_storage_last_error_detail(s_storage);
    uint32_t attempts = s_storage ? s_storage->mount_attempts : 0U;

    build_status_topic(topic, sizeof(topic));
    if (topic[0] == '\0') {
        return;
    }

    if (snprintf(payload,
                 sizeof(payload),
                 "{\"node_id\":\"%s\",\"record_type\":\"command_response\","
                 "\"timestamp\":%" PRIi64 ",\"fw_version\":\"%s\","
                 "\"mode\":\"%s\",\"cmd\":\"%s\",\"result\":\"%s\","
                 "\"reason\":\"%s\",\"request_id\":\"%s\","
                 "\"storage_mounted\":%s,"
                 "\"storage_last_error\":\"%s\","
                 "\"storage_error_detail\":\"%s\","
                 "\"sd_mount_attempts\":%u}",
                 s_node_id[0] ? s_node_id : SDACS_NODE_ID,
                 (int64_t)esp_timer_get_time(),
                 SDACS_FW_VERSION,
                 device_state_to_str(device_state_get()),
                 cmd ? cmd : "storage_status",
                 result ? result : "",
                 reason ? reason : "",
                 request_id ? request_id : "",
                 mounted ? "true" : "false",
                 last_error ? last_error : "ESP_OK",
                 detail ? detail : "",
                 (unsigned)attempts) >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Storage status payload too long");
        return;
    }

    esp_err_t err = wifi_mqtt_publish_status_json(topic, payload);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Storage status publish failed: %s", esp_err_to_name(err));
    }
}

static void handle_storage_status(const char *request_id)
{
    publish_storage_status("storage_status", request_id, "ok", "storage_status");
}

static void handle_storage_remount(const char *request_id)
{
#if !SDACS_ENABLE_SD_STORAGE
    publish_storage_status("storage_remount", request_id, "rejected", "sd_disabled_by_build_config");
    return;
#endif
    if (device_state_get() != SDACS_MODE_IDLE) {
        publish_storage_status("storage_remount", request_id, "rejected", "busy");
        return;
    }

    if (run_storage_is_mounted(s_storage)) {
        publish_storage_status("storage_remount", request_id, "ok", "already_mounted");
        return;
    }

    esp_err_t err = run_storage_init(s_storage);
    if (err == ESP_OK) {
        publish_storage_status("storage_remount", request_id, "ok", "mounted");
    } else {
        publish_storage_status("storage_remount", request_id, "rejected", esp_err_to_name(err));
    }
}

static void handle_start_capture(const cJSON *root, const char *request_id)
{
    uint32_t delay_ms = 0;
    uint32_t record_seconds = SDACS_RECORD_SECONDS;
    float cal_offset_db = SDACS_CAL_OFFSET_DB;
    esp_err_t cal_err = config_store_get_cal_offset_db(&cal_offset_db);

    if (cal_err != ESP_OK) {
        cal_offset_db = SDACS_CAL_OFFSET_DB;
        ESP_LOGW(TAG, "Using default calibration offset %.2f dB: %s",
                 (double)cal_offset_db,
                 esp_err_to_name(cal_err));
    }

    capture_context_t ctx = {
        .storage = s_storage,
        .node_id = s_node_id[0] ? s_node_id : SDACS_NODE_ID,
        .base_topic = s_base_topic,
        .request_id = request_id,
        .delay_ms = 0,
        .cal_offset_db = cal_offset_db,
        .record_seconds = SDACS_RECORD_SECONDS,
#if SDACS_ENABLE_SD_STORAGE
        .sd_enabled = true,
        .sd_writes_enabled = true,
        .storage_mode = "sd_raw_wav_csv",
#else
        .sd_enabled = false,
        .sd_writes_enabled = false,
        .storage_mode = "mqtt_only",
#endif
    };

    if (!request_id || request_id[0] == '\0') {
        publish_capture_status("", SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "missing request_id");
        return;
    }

    if (!json_copy_u32(root, "delay_ms", &delay_ms, true)) {
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "missing or invalid delay_ms");
        return;
    }

    if (!json_copy_u32(root, "record_seconds", &record_seconds, true)) {
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "missing or invalid record_seconds");
        return;
    }

    ESP_LOGI(TAG, "start_capture received request_id=%s delay_ms=%u record_seconds=%u",
             request_id,
             (unsigned)delay_ms,
             (unsigned)record_seconds);

    if (record_seconds == 0 || record_seconds > 3600) {
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "invalid record_seconds");
        return;
    }

    if (delay_ms > 3600000U) {
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "invalid delay_ms");
        return;
    }

    if (!device_state_can_start_capture()) {
        ESP_LOGW(TAG, "start_capture rejected: busy");
        publish_capture_status(request_id, device_state_get(), delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "capture command rejected: busy");
        return;
    }

    if (s_base_topic[0] == '\0') {
        ESP_LOGW(TAG, "start_capture rejected: not ready");
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "capture command rejected: not ready");
        return;
    }

#if SDACS_ENABLE_SD_STORAGE
    if (!ensure_storage_ready()) {
        char reason[192];
        build_storage_reason(reason, sizeof(reason), "capture rejected: SD storage not mounted");
        ESP_LOGW(TAG, "start_capture rejected: SD storage unavailable");
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               reason);
        return;
    }
#elif !SDACS_CAPTURE_MQTT_ONLY_WHEN_NO_SD
    ESP_LOGW(TAG, "start_capture rejected: SD storage disabled and MQTT-only capture disabled");
    publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                           ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                           "capture rejected: SD storage disabled by build config");
    return;
#endif

    ctx.delay_ms = delay_ms;
    ctx.record_seconds = record_seconds;

    device_state_set(SDACS_MODE_ARMED);
    publish_capture_status(request_id, SDACS_MODE_ARMED, delay_ms, record_seconds, cal_offset_db,
                           ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                           ctx.sd_writes_enabled ? "capture command accepted" : "capture accepted: MQTT/features-only mode");
    (void)temp_humidity_publish_latest_once("capture_armed");
#if SDACS_FUEL_GAUGE_ENABLED
    (void)fuel_gauge_publish_latest_once("capture_armed");
#endif

    esp_err_t err = capture_task_start(&ctx);
    if (err != ESP_OK) {
        device_state_set(SDACS_MODE_IDLE);
        ESP_LOGE(TAG, "Failed to start capture: %s", esp_err_to_name(err));
        publish_capture_status(request_id, SDACS_MODE_ERROR, delay_ms, record_seconds, cal_offset_db,
                               ctx.storage_mode, ctx.sd_enabled, ctx.sd_writes_enabled,
                               "capture command rejected: start failed");
        return;
    }

    ESP_LOGI(TAG, "start_capture accepted storage_mode=%s sd_writes=%s",
             ctx.storage_mode,
             ctx.sd_writes_enabled ? "true" : "false");
}

static void handle_ota_update(const cJSON *root, const char *request_id)
{
    char url[256];
    char version[64];
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    url[0] = '\0';
    version[0] = '\0';
    if (cJSON_IsObject(params)) {
        (void)json_copy_string(params, "url", url, sizeof(url));
        (void)json_copy_string(params, "version", version, sizeof(version));
    }

    if (!device_state_can_start_ota()) {
        publish_response("ota_update", request_id, "rejected", "busy");
        return;
    }

    if (url[0] == '\0') {
        publish_response("ota_update", request_id, "rejected", "missing_url");
        return;
    }

    esp_err_t err = ota_manager_start(url, version[0] ? version : NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA: %s", esp_err_to_name(err));
        publish_response("ota_update", request_id, "rejected", "start_failed");
        return;
    }

    publish_response("ota_update", request_id, "accepted", "ota_started");
}

static void handle_report_status(const char *request_id)
{
    (void)wifi_mqtt_publish_heartbeat("online");
    (void)temp_humidity_publish_latest_once("report_status");
#if SDACS_FUEL_GAUGE_ENABLED
    (void)fuel_gauge_publish_latest_once("report_status");
#endif
    publish_response("report_status", request_id, "ok", "status_report");
}

static void handle_ble_advertise(const cJSON *root, const char *request_id)
{
    uint32_t duration_ms = SDACS_BLE_LOCATOR_DURATION_MS;

    if (json_copy_u32(root, "duration_ms", &duration_ms, false)) {
        if (duration_ms == 0 || duration_ms > 60000U) {
            publish_response("ble_advertise", request_id, "rejected", "invalid_duration_ms");
            return;
        }
    }

    esp_err_t err = sdacs_ble_locator_request_advertise(duration_ms);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ble_advertise rejected: %s", esp_err_to_name(err));
        publish_response("ble_advertise", request_id, "rejected", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "ble_advertise accepted duration_ms=%u", (unsigned)duration_ms);
    publish_response("ble_advertise", request_id, "accepted", "advertising_started");
}

static void handle_reboot(const char *request_id)
{
    if (device_state_get() == SDACS_MODE_CAPTURING) {
        publish_response("reboot", request_id, "rejected", "busy");
        return;
    }

    (void)wifi_mqtt_publish_heartbeat("restarting");
    publish_response("reboot", request_id, "accepted", "rebooting");
    esp_restart();
}

static void handle_set_cal_offset(const cJSON *root, const char *request_id)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "cal_offset_db");
    float cal_offset_db = 0.0f;

    if (device_state_get() == SDACS_MODE_CAPTURING) {
        publish_response("set_cal_offset", request_id, "rejected", "busy");
        return;
    }

    if (!cJSON_IsNumber(item)) {
        publish_response("set_cal_offset", request_id, "rejected", "missing_or_invalid_cal_offset_db");
        return;
    }

    if (item->valuedouble < 60.0 || item->valuedouble > 180.0) {
        publish_response("set_cal_offset", request_id, "rejected", "cal_offset_db_out_of_range");
        return;
    }

    cal_offset_db = (float)item->valuedouble;
    esp_err_t err = config_store_set_cal_offset_db(cal_offset_db);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save calibration offset %.2f dB: %s",
                 (double)cal_offset_db,
                 esp_err_to_name(err));
        publish_response("set_cal_offset", request_id, "rejected", "save_failed");
        return;
    }

    ESP_LOGI(TAG, "Calibration offset saved: %.2f dB", (double)cal_offset_db);
    publish_cal_offset_response(request_id, cal_offset_db);
}

void command_dispatcher_init(run_storage_t *storage, const char *node_id, const char *base_topic)
{
    s_storage = storage;

    s_node_id[0] = '\0';
    s_base_topic[0] = '\0';

    if (node_id) {
        strncpy(s_node_id, node_id, sizeof(s_node_id) - 1);
        s_node_id[sizeof(s_node_id) - 1] = '\0';
    }

    if (base_topic) {
        strncpy(s_base_topic, base_topic, sizeof(s_base_topic) - 1);
        s_base_topic[sizeof(s_base_topic) - 1] = '\0';
    }
}

void command_dispatcher_handle(const char *topic, const char *payload, int len)
{
    char cmd[64];
    char request_id[64];
    cJSON *root = NULL;

    (void)topic;

    if (!payload || len <= 0) {
        return;
    }

    cmd[0] = '\0';
    request_id[0] = '\0';

    root = cJSON_ParseWithLength(payload, (size_t)len);
    if (!root) {
        ESP_LOGW(TAG, "Ignoring invalid JSON command");
        publish_response("unknown", "", "rejected", "invalid_json");
        return;
    }

    (void)json_copy_string(root, "cmd", cmd, sizeof(cmd));
    (void)json_copy_string(root, "request_id", request_id, sizeof(request_id));

    ESP_LOGI(TAG, "Command received: topic=%s cmd=%s", topic ? topic : "(null)", cmd[0] ? cmd : "(missing)");

    if (strcmp(cmd, "start_capture") == 0) {
        handle_start_capture(root, request_id);
    } else if (strcmp(cmd, "set_cal_offset") == 0) {
        handle_set_cal_offset(root, request_id);
    } else if (strcmp(cmd, "ota_update") == 0) {
        handle_ota_update(root, request_id);
    } else if (strcmp(cmd, "report_status") == 0) {
        handle_report_status(request_id);
    } else if (strcmp(cmd, "storage_status") == 0) {
        handle_storage_status(request_id);
    } else if (strcmp(cmd, "storage_remount") == 0) {
        handle_storage_remount(request_id);
    } else if (strcmp(cmd, "ble_advertise") == 0) {
        handle_ble_advertise(root, request_id);
    } else if (strcmp(cmd, "reboot") == 0) {
        handle_reboot(request_id);
    } else {
        publish_response(cmd[0] ? cmd : "unknown", request_id, "rejected", "unsupported_cmd");
    }

    cJSON_Delete(root);
}
