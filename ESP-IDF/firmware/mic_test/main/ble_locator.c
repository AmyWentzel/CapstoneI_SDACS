#include "ble_locator.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

#include "services/gap/ble_svc_gap.h"

#include "node_identity.h"
#include "sdacs_config.h"

static const char *TAG = "ble_locator";

static volatile bool s_ble_synced = false;
static volatile bool s_ble_host_stopped = false;

static void ble_host_task(void *param)
{
    (void)param;

    nimble_port_run();

    s_ble_host_stopped = true;
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void)
{
    s_ble_synced = true;
}

static void sdacs_ble_stop_host(void)
{
    uint32_t waited_ms = 0;
    int rc = nimble_port_stop();

    if (rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop returned %d", rc);
    }

    while (!s_ble_host_stopped && waited_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited_ms += 100;
    }

    if (!s_ble_host_stopped) {
        ESP_LOGW(TAG, "BLE host stop timeout; deinitializing anyway");
    }

    nimble_port_deinit();
}

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "BLE advertising complete");
            break;

        default:
            break;
    }

    return 0;
}

static esp_err_t sdacs_ble_start_advertising(const char *node_id)
{
    char device_name[32] = {0};
    char mfg_payload[32] = {0};
    const char *effective_node_id = sdacs_node_id();
    esp_err_t err = ESP_OK;

    (void)node_id;

    if (!sdacs_node_id_is_valid(effective_node_id)) {
        ESP_LOGE(TAG, "Invalid compiled BLE node ID: %s",
                 effective_node_id ? effective_node_id : "(null)");
        return ESP_ERR_INVALID_STATE;
    }

    err = sdacs_get_ble_name(device_name, sizeof(device_name));
    if (err != ESP_OK) {
        return err;
    }
    err = sdacs_get_ble_mfg_payload(mfg_payload, sizeof(mfg_payload));
    if (err != ESP_OK) {
        return err;
    }

    int rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return ESP_FAIL;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    /*
     * Manufacturer data is optional, but useful.
     * This lets the RPi scanner identify SDACS packets even if names are filtered.
     *
     * Payload format:
     *   "SDACS:" + node_id
     */
    int mfg_len = strlen(mfg_payload);

    if (mfg_len > 0 && mfg_len < (int)sizeof(mfg_payload)) {
        fields.mfg_data = (uint8_t *)mfg_payload;
        fields.mfg_data_len = (uint8_t)mfg_len;
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));

    /*
     * Non-connectable advertising:
     * The RPi5 only scans RSSI and node ID.
     * It does not pair or connect.
     */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(
        BLE_OWN_ADDR_PUBLIC,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event_handler,
        NULL
    );

    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE active node ID: %s", effective_node_id);
    ESP_LOGI(TAG, "BLE GAP name: %s", device_name);
    ESP_LOGI(TAG, "BLE manufacturer payload: %s", mfg_payload);
    ESP_LOGI(TAG, "BLE advertising as %s", device_name);
    return ESP_OK;
}

esp_err_t sdacs_ble_locator_advertise_for(const char *node_id, uint32_t duration_ms)
{
#if !SDACS_BLE_LOCATOR_ENABLED
    (void)node_id;
    (void)duration_ms;
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Starting SDACS BLE locator phase for %lu ms",
             (unsigned long)duration_ms);

    s_ble_synced = false;
    s_ble_host_stopped = false;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_svc_gap_init();
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);

    uint32_t waited_ms = 0;
    while (!s_ble_synced && waited_ms < 5000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited_ms += 100;
    }

    if (!s_ble_synced) {
        ESP_LOGE(TAG, "BLE host sync timeout");
        sdacs_ble_stop_host();
        return ESP_ERR_TIMEOUT;
    }

    err = sdacs_ble_start_advertising(node_id);
    if (err != ESP_OK) {
        sdacs_ble_stop_host();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "ble_gap_adv_stop returned %d", rc);
    }

    sdacs_ble_stop_host();

    ESP_LOGI(TAG, "SDACS BLE locator phase complete");

    return ESP_OK;
#endif
}
