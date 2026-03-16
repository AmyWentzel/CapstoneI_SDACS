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

    esp_err_t err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return err;
    }

    if (strcmp(SDACS_PROVISION_WIFI_SSID, "YOUR_WIFI_SSID") == 0 ||
        strcmp(SDACS_PROVISION_WIFI_PASS, "YOUR_WIFI_PASSWORD") == 0) {
        ESP_LOGW(TAG, "Provisioning skipped: update SDACS_PROVISION_WIFI_* in sdacs_config.h");
        return ESP_OK;
    }

    if (!ssid || ssid[0] == '\0') {
        ESP_RETURN_ON_ERROR(
            config_store_set_wifi(SDACS_PROVISION_WIFI_SSID, SDACS_PROVISION_WIFI_PASS),
            TAG,
            "Failed to provision WiFi defaults");
        ESP_LOGI(TAG, "Provisioned WiFi defaults into NVS.");
    } else {
        ESP_LOGI(TAG, "WiFi already provisioned; keeping existing SSID.");
    }

    err = config_store_get_mqtt(&broker_uri, &mqtt_topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return err;
    }

#if SDACS_PROVISION_ALWAYS_SYNC_MQTT
    if (!broker_uri || !mqtt_topic ||
        strcmp(broker_uri, SDACS_PROVISION_MQTT_URI) != 0 ||
        strcmp(mqtt_topic, SDACS_PROVISION_MQTT_TOPIC) != 0) {
        ESP_RETURN_ON_ERROR(
            config_store_set_mqtt(SDACS_PROVISION_MQTT_URI, SDACS_PROVISION_MQTT_TOPIC),
            TAG,
            "Failed to sync MQTT defaults");
        broker_uri = SDACS_PROVISION_MQTT_URI;
        mqtt_topic = SDACS_PROVISION_MQTT_TOPIC;
        ESP_LOGW(TAG, "Synced MQTT settings in NVS to firmware defaults.");
    }
#endif

    ESP_LOGI(TAG, "Active MQTT config: broker=%s topic=%s",
             broker_uri ? broker_uri : "(null)",
             mqtt_topic ? mqtt_topic : "(null)");
    return ESP_OK;
}
