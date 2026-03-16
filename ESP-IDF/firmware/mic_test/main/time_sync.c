#include "time_sync.h"

#include <time.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "sdacs_config.h"
#include "wifi_mqtt.h"

static const char *TAG = "time_sync";

bool time_sync_is_valid(void)
{
    return time(NULL) > SDACS_VALID_UNIX_TIME_EPOCH;
}

void time_sync_get_iso8601(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm timeinfo;

    if (!out || out_len == 0) {
        return;
    }

    localtime_r(&now, &timeinfo);
    strftime(out, out_len, "%Y-%m-%dT%H:%M:%S", &timeinfo);
}

void time_sync_log_current(const char *prefix)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    char buf[32] = "invalid";

    if (localtime_r(&now, &timeinfo) != NULL) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    ESP_LOGI(TAG, "%s%s", prefix ? prefix : "", buf);
}

void time_sync_try_sntp(uint32_t wait_ms)
{
    if (time_sync_is_valid()) {
        time_sync_log_current("System time already valid: ");
        return;
    }

    ESP_LOGI(TAG, "Waiting for WiFi before SNTP time sync...");
    esp_err_t err = wifi_mqtt_wait_wifi(wait_ms);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not ready for SNTP sync: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Starting SNTP time sync...");
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(wait_ms));
    if (err == ESP_OK && time_sync_is_valid()) {
        time_sync_log_current("SNTP synced time: ");
    } else {
        ESP_LOGW(TAG, "SNTP sync timed out; SD timestamps may still be inaccurate.");
    }
    esp_netif_sntp_deinit();
}
