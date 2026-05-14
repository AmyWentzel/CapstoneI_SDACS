#include "wifi_station.h"

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "config_store.h"

static const char *TAG = "wifi_station";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static bool s_started = false;
static int s_retry_num = 0;
static const int WIFI_MAX_RETRY = 3;

static const char *authmode_to_str(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
        case WIFI_AUTH_ENTERPRISE: return "ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
        default: return "UNKNOWN";
    }
}

static void log_sta_ap_info(const char *prefix)
{
    wifi_ap_record_t ap_info = {0};
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: esp_wifi_sta_get_ap_info failed: %s",
                 prefix, esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG,
             "%s: AP='%s' RSSI=%d dBm CH=%u AUTH=%s",
             prefix,
             (const char *)ap_info.ssid,
             (int)ap_info.rssi,
             (unsigned)ap_info.primary,
             authmode_to_str(ap_info.authmode));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi STA start -> connect");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
        if (evt) {
            ESP_LOGI(TAG,
                     "WiFi connected: SSID='%.*s' channel=%u authmode=%s",
                     (int)evt->ssid_len,
                     (const char *)evt->ssid,
                     (unsigned)evt->channel,
                     authmode_to_str(evt->authmode));
        } else {
            ESP_LOGI(TAG, "WiFi connected");
        }
        log_sta_ap_info("WiFi link");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc =
            (wifi_event_sta_disconnected_t *)event_data;

        ESP_LOGW(TAG, "WiFi disconnected (reason=%d)", disc ? disc->reason : -1);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d",
                     s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_num = 0;
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        log_sta_ap_info("WiFi IP");
    }
}

esp_err_t sdacs_wifi_station_start(void)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    esp_err_t err = ESP_OK;

    if (s_started) {
        return ESP_OK;
    }

    err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!ssid || ssid[0] == '\0' || !pass) {
        ESP_LOGE(TAG, "Missing WiFi settings in config_store.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    s_started = true;
    ESP_LOGI(TAG, "WiFi init done. SSID=%s", ssid);
    return ESP_OK;
}

esp_err_t sdacs_wifi_station_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_event_group) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        timeout_ms == UINT32_MAX ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }
    if (!(bits & WIFI_CONNECTED_BIT)) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
