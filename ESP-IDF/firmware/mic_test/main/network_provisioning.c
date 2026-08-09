/*
 * SDACS module: Boot-time synchronization of compiled network defaults into NVS
 *
 * Purpose:
 *   Applies compiled Wi-Fi and MQTT provisioning values while preserving configurable persistence behavior.
 *
 * Design note:
 *   Provisioning is separated from storage so deployment-specific secrets remain outside tracked source.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#include "network_provisioning.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "config_store.h"
#include "sdacs_config.h"

static const char *TAG = "network_prov";

esp_err_t network_provisioning_apply_defaults(void)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    const char *broker_uri = NULL;
    const char *mqtt_topic = NULL;
    bool have_wifi_defaults = false;
    bool have_mqtt_defaults = false;

    esp_err_t err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return err;
    }

    have_wifi_defaults = (SDACS_PROVISION_WIFI_SSID[0] != '\0' &&
                          SDACS_PROVISION_WIFI_PASS[0] != '\0');
    have_mqtt_defaults = (SDACS_PROVISION_MQTT_URI[0] != '\0');

    if (have_wifi_defaults &&
        (!ssid || !pass ||
         strcmp(ssid, SDACS_PROVISION_WIFI_SSID) != 0 ||
         strcmp(pass, SDACS_PROVISION_WIFI_PASS) != 0)) {
        ESP_RETURN_ON_ERROR(
            config_store_set_wifi(SDACS_PROVISION_WIFI_SSID, SDACS_PROVISION_WIFI_PASS),
            TAG,
            "Failed to sync WiFi defaults");
        ssid = SDACS_PROVISION_WIFI_SSID;
        pass = SDACS_PROVISION_WIFI_PASS;
        ESP_LOGW(TAG, "Synced WiFi credentials in NVS to firmware defaults.");
    } else if (!ssid || ssid[0] == '\0') {
        if (!have_wifi_defaults) {
            ESP_LOGW(TAG, "No default WiFi credentials compiled in; skipping WiFi provisioning.");
        } else {
            ESP_RETURN_ON_ERROR(
                config_store_set_wifi(SDACS_PROVISION_WIFI_SSID, SDACS_PROVISION_WIFI_PASS),
                TAG,
                "Failed to provision WiFi defaults");
            ESP_LOGI(TAG, "Provisioned WiFi defaults into NVS.");
        }
    } else {
        ESP_LOGI(TAG, "WiFi already provisioned; keeping existing SSID.");
    }

    err = config_store_get_mqtt(&broker_uri, &mqtt_topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return err;
    }

#if SDACS_PROVISION_ALWAYS_SYNC_MQTT
    if (have_mqtt_defaults &&
        (!broker_uri || strcmp(broker_uri, SDACS_PROVISION_MQTT_URI) != 0)) {
        ESP_RETURN_ON_ERROR(
            config_store_set_mqtt(SDACS_PROVISION_MQTT_URI, ""),
            TAG,
            "Failed to sync MQTT defaults");
        broker_uri = SDACS_PROVISION_MQTT_URI;
        mqtt_topic = "";
        ESP_LOGW(TAG, "Synced MQTT settings in NVS to firmware defaults.");
    }
#endif

    if ((!broker_uri || broker_uri[0] == '\0') && have_mqtt_defaults) {
        ESP_RETURN_ON_ERROR(
            config_store_set_mqtt(SDACS_PROVISION_MQTT_URI, ""),
            TAG,
            "Failed to provision MQTT defaults");
        broker_uri = SDACS_PROVISION_MQTT_URI;
        mqtt_topic = "";
        ESP_LOGI(TAG, "Provisioned MQTT defaults into NVS.");
    }

    if (mqtt_topic && mqtt_topic[0] != '\0') {
        ESP_LOGW(TAG, "Deprecated NVS mqtt_topic found but ignored. Runtime topics come from compiled Node ID.");
    }

    ESP_LOGI(TAG, "Active MQTT config: broker=%s topic=%s",
             broker_uri ? broker_uri : "(null)",
             "(compiled from Node ID)");
    return ESP_OK;
}
