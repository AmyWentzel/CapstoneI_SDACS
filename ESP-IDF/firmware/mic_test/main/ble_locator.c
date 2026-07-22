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
#include "services/gatt/ble_svc_gatt.h"

#include "sdacs_config.h"

static const char *TAG = "ble_locator";

static volatile bool s_ble_synced = false;
static volatile bool s_ble_host_stopped = false;
static bool s_ble_busy = false;
static portMUX_TYPE s_ble_lock = portMUX_INITIALIZER_UNLOCKED;

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

static esp_err_t sdacs_ble_start_advertising(void)
{
    char device_name[32];

    snprintf(
        device_name,
        sizeof(device_name),
        "SDACS-%s",
        SDACS_NODE_ID
    );

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
    char mfg_payload[32];
    int mfg_len = snprintf(
        mfg_payload,
        sizeof(mfg_payload),
        "SDACS:%s",
        SDACS_NODE_ID
    );

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

    uint8_t own_addr_type;
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "No usable BLE identity address: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Unable to select BLE own-address type: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gap_adv_start(
        own_addr_type,
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

    ESP_LOGI(TAG, "BLE advertising as %s", device_name);
    return ESP_OK;
}

bool sdacs_ble_locator_is_busy(void)
{
    bool busy;
    taskENTER_CRITICAL(&s_ble_lock);
    busy = s_ble_busy;
    taskEXIT_CRITICAL(&s_ble_lock);
    return busy;
}

static void ble_locator_task(void *param)
{
    uint32_t duration_ms = (uint32_t)(uintptr_t)param;
    esp_err_t err = sdacs_ble_locator_advertise_for(duration_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Asynchronous BLE advertisement failed: %s", esp_err_to_name(err));
    }
    taskENTER_CRITICAL(&s_ble_lock);
    s_ble_busy = false;
    taskEXIT_CRITICAL(&s_ble_lock);
    vTaskDelete(NULL);
}

esp_err_t sdacs_ble_locator_start_async(uint32_t duration_ms)
{
#if !SDACS_BLE_LOCATOR_ENABLED
    (void)duration_ms;
    return ESP_ERR_NOT_SUPPORTED;
#else
    taskENTER_CRITICAL(&s_ble_lock);
    if (s_ble_busy) {
        taskEXIT_CRITICAL(&s_ble_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_ble_busy = true;
    taskEXIT_CRITICAL(&s_ble_lock);

    if (xTaskCreate(ble_locator_task, "ble_locator", 6144,
                    (void *)(uintptr_t)duration_ms, 5, NULL) != pdPASS) {
        taskENTER_CRITICAL(&s_ble_lock);
        s_ble_busy = false;
        taskEXIT_CRITICAL(&s_ble_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#endif
}

esp_err_t sdacs_ble_locator_advertise_for(uint32_t duration_ms)
{
#if !SDACS_BLE_LOCATOR_ENABLED
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
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(ble_host_task);

    uint32_t waited_ms = 0;
    while (!s_ble_synced && waited_ms < 5000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited_ms += 100;
    }

    if (!s_ble_synced) {
        ESP_LOGE(TAG, "BLE host sync timeout");
        nimble_port_stop();
        nimble_port_deinit();
        return ESP_ERR_TIMEOUT;
    }

    err = sdacs_ble_start_advertising();
    if (err != ESP_OK) {
        nimble_port_stop();
        nimble_port_deinit();
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ble_gap_adv_stop();

    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "nimble_port_stop returned %d", rc);
    }

    waited_ms = 0;
    while (!s_ble_host_stopped && waited_ms < 3000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        waited_ms += 100;
    }

    nimble_port_deinit();

    ESP_LOGI(TAG, "SDACS BLE locator phase complete");

    return ESP_OK;
#endif
}
