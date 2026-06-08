#include "fuel_gauge.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "battery_leds.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config_store.h"
#include "node_identity.h"
#include "sdacs_config.h"
#include "shared_i2c_bus.h"
#include "wifi_mqtt.h"

static const char *TAG = "fuel_gauge";

#define MAX17048_REG_VCELL 0x02
#define MAX17048_REG_SOC   0x04
#define MAX17048_REG_VER   0x08
#define MAX17048_REG_CRATE 0x16

typedef struct
{
    int i2c_port;
    uint8_t addr;
    uint32_t period_ms;

    TaskHandle_t task;
    SemaphoreHandle_t lock;

    fuel_gauge_reading_t latest;
    bool running;
    bool charge_rate_supported;
    bool charge_rate_warned;
    char topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
    char node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1];
} fuel_gauge_ctx_t;

static fuel_gauge_ctx_t g_ctx = {0};

static void build_publish_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    (void)sdacs_build_topic(out, out_sz, "/fuel_gauge");
}

static void load_node_id(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    strncpy(out, sdacs_node_id(), out_sz - 1);
    out[out_sz - 1] = '\0';
}

static esp_err_t max17048_read_reg16(int port, uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t raw[2] = {0};
    esp_err_t err = ESP_OK;

    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    err = shared_i2c_write_read(
        port,
        addr,
        &reg,
        sizeof(reg),
        raw,
        sizeof(raw),
        pdMS_TO_TICKS(250)
    );
    if (err != ESP_OK) {
        return err;
    }

    *value = (uint16_t)((raw[0] << 8) | raw[1]);
    return ESP_OK;
}

static esp_err_t fuel_gauge_read_sensor(int port, uint8_t addr, fuel_gauge_reading_t *out)
{
    uint16_t raw_vcell = 0;
    uint16_t raw_soc = 0;
    uint16_t raw_crate = 0;
    esp_err_t err = ESP_OK;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    err = max17048_read_reg16(port, addr, MAX17048_REG_VCELL, &raw_vcell);
    if (err != ESP_OK) {
        return err;
    }

    err = max17048_read_reg16(port, addr, MAX17048_REG_SOC, &raw_soc);
    if (err != ESP_OK) {
        return err;
    }

    out->voltage_v = (float)raw_vcell * 78.125e-6f;
    out->soc_percent = (float)raw_soc / 256.0f;
    out->charge_rate_percent_per_hr = 0.0f;

    if (g_ctx.charge_rate_supported) {
        err = max17048_read_reg16(port, addr, MAX17048_REG_CRATE, &raw_crate);
        if (err == ESP_OK) {
            // TODO: Confirm MAX17048 charge-rate register support and scaling for this exact board/chip.
            // The placeholder assumes a signed 0.208 %/hr LSB, which is common for MAX1704x-family CRATE.
            out->charge_rate_percent_per_hr = (float)((int16_t)raw_crate) * 0.208f;
        } else {
            g_ctx.charge_rate_supported = false;
            if (!g_ctx.charge_rate_warned) {
                ESP_LOGW(TAG, "Charge-rate register unavailable (%s); continuing with voltage/SOC only",
                         esp_err_to_name(err));
                g_ctx.charge_rate_warned = true;
            }
        }
    }

    out->valid = true;
    return ESP_OK;
}

static void fuel_gauge_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task started (period=%ums, addr=0x%02X)",
             (unsigned)g_ctx.period_ms, g_ctx.addr);
    ESP_LOGI(TAG, "Fuel gauge configured: port=%d sda=%d scl=%d addr=0x%02X period=%u ms topic=%s",
             g_ctx.i2c_port,
             SDACS_TEMP_HUMIDITY_SDA_GPIO,
             SDACS_TEMP_HUMIDITY_SCL_GPIO,
             g_ctx.addr,
             (unsigned)g_ctx.period_ms,
             g_ctx.topic[0] ? g_ctx.topic : "(none)");
    if (g_ctx.topic[0] != '\0') {
        ESP_LOGI(TAG, "Battery snapshots available on topic '%s'", g_ctx.topic);
    } else {
        ESP_LOGW(TAG, "No MQTT battery topic configured; local sampling only.");
    }

    while (g_ctx.running) {
        fuel_gauge_reading_t sample = {0};
        fuel_gauge_reading_t snap = {0};
        esp_err_t err = fuel_gauge_read_sensor(g_ctx.i2c_port, g_ctx.addr, &sample);

        xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
        g_ctx.latest.sample_count++;
        g_ctx.latest.last_sample_time_us = esp_timer_get_time();

        if (err == ESP_OK) {
            sample.sample_count = g_ctx.latest.sample_count;
            sample.error_count = g_ctx.latest.error_count;
            sample.last_sample_time_us = g_ctx.latest.last_sample_time_us;
            g_ctx.latest = sample;
            battery_leds_show_percent(sample.soc_percent);
        } else {
            g_ctx.latest.error_count++;
        }

        snap = g_ctx.latest;
        xSemaphoreGive(g_ctx.lock);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "SOC=%.1f %% V=%.3f rate=%.2f %%/hr (n=%u)",
                     (double)snap.soc_percent,
                     (double)snap.voltage_v,
                     (double)snap.charge_rate_percent_per_hr,
                     (unsigned)snap.sample_count);
        } else {
            ESP_LOGW(TAG, "Fuel gauge read failed: %s (err_count=%u)",
                     esp_err_to_name(err), (unsigned)snap.error_count);
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(g_ctx.period_ms));
    }

    ESP_LOGI(TAG, "Task stopping");
    g_ctx.task = NULL;
    vTaskDelete(NULL);
}

bool fuel_gauge_start(int i2c_port,
                      uint8_t sensor_addr,
                      uint32_t period_ms)
{
    esp_err_t err = ESP_OK;
    BaseType_t ok = pdFAIL;
    uint16_t version = 0;

    if (g_ctx.running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    if (!shared_i2c_bus_is_ready()) {
        ESP_LOGE(TAG, "Shared I2C bus not initialized");
        return false;
    }

    if (period_ms < 500 || period_ms > 60000) {
        ESP_LOGE(TAG, "Invalid period_ms=%u", (unsigned)period_ms);
        return false;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.i2c_port = i2c_port;
    g_ctx.addr = sensor_addr;
    g_ctx.period_ms = period_ms;
    g_ctx.charge_rate_supported = true;
    g_ctx.charge_rate_warned = false;
    build_publish_topic(g_ctx.topic, sizeof(g_ctx.topic));
    load_node_id(g_ctx.node_id, sizeof(g_ctx.node_id));

    g_ctx.lock = xSemaphoreCreateMutex();
    if (!g_ctx.lock) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    g_ctx.latest = (fuel_gauge_reading_t){
        .soc_percent = 0.0f,
        .voltage_v = 0.0f,
        .charge_rate_percent_per_hr = 0.0f,
        .valid = false,
        .sample_count = 0,
        .error_count = 0,
        .last_sample_time_us = 0,
    };

    g_ctx.running = true;
    ok = xTaskCreate(
        fuel_gauge_task,
        "fuel_gauge",
        4096,
        NULL,
        5,
        &g_ctx.task
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        g_ctx.running = false;
        vSemaphoreDelete(g_ctx.lock);
        g_ctx.lock = NULL;
        return false;
    }

    err = max17048_read_reg16(i2c_port, sensor_addr, MAX17048_REG_VER, &version);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MAX17048 probe OK, version=0x%04X", version);
    } else {
        ESP_LOGW(TAG, "MAX17048 version probe failed at addr 0x%02X: %s",
                 sensor_addr, esp_err_to_name(err));
    }

    return true;
}

bool fuel_gauge_get_latest(fuel_gauge_reading_t *out)
{
    bool valid = false;

    if (!out || !g_ctx.lock) {
        return false;
    }

    xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
    *out = g_ctx.latest;
    valid = g_ctx.latest.valid;
    xSemaphoreGive(g_ctx.lock);
    return valid;
}

bool fuel_gauge_read_once(int i2c_port,
                          uint8_t sensor_addr,
                          fuel_gauge_reading_t *out)
{
    esp_err_t err = ESP_OK;

    if (!out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!shared_i2c_bus_is_ready()) {
        ESP_LOGE(TAG, "Shared I2C bus not initialized");
        return false;
    }

    g_ctx.charge_rate_supported = true;
    g_ctx.charge_rate_warned = false;
    err = fuel_gauge_read_sensor(i2c_port, sensor_addr, out);
    out->sample_count = 1;
    out->last_sample_time_us = esp_timer_get_time();

    if (err != ESP_OK) {
        out->error_count = 1;
        if (g_ctx.lock) {
            xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
            g_ctx.latest.sample_count++;
            g_ctx.latest.error_count++;
            g_ctx.latest.last_sample_time_us = out->last_sample_time_us;
            xSemaphoreGive(g_ctx.lock);
        }
        ESP_LOGW(TAG, "Final capture fuel gauge read failed: %s", esp_err_to_name(err));
        return false;
    }

    if (g_ctx.lock) {
        xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
        g_ctx.latest.sample_count++;
        out->sample_count = g_ctx.latest.sample_count;
        out->error_count = g_ctx.latest.error_count;
        g_ctx.latest = *out;
        xSemaphoreGive(g_ctx.lock);
    }
    ESP_LOGI(TAG, "Final capture SOC=%.1f %% V=%.3f rate=%.2f %%/hr",
             (double)out->soc_percent,
             (double)out->voltage_v,
             (double)out->charge_rate_percent_per_hr);
    battery_leds_show_percent(out->soc_percent);
    return true;
}

bool fuel_gauge_publish_latest_once(const char *phase)
{
    char payload[320];
    fuel_gauge_reading_t snap = {0};
    int len = 0;
    esp_err_t err = ESP_OK;

    if (!phase) {
        phase = "unknown";
    }
    if (!g_ctx.lock || g_ctx.topic[0] == '\0') {
        return false;
    }

    xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
    snap = g_ctx.latest;
    xSemaphoreGive(g_ctx.lock);

    if (!snap.valid) {
        ESP_LOGW(TAG, "No valid battery sample yet; skip %s publish", phase);
        return false;
    }

    len = snprintf(
        payload, sizeof(payload),
        "{"
        "\"node_id\":\"%s\","
        "\"record_type\":\"fuel_gauge\","
        "\"timestamp\":%" PRIi64 ","
        "\"fw_version\":\"%s\","
        "\"phase\":\"%s\","
        "\"soc_percent\":%.2f,"
        "\"voltage_v\":%.4f,"
        "\"charge_rate_percent_per_hr\":%.2f,"
        "\"sample_count\":%u,"
        "\"error_count\":%u,"
        "\"valid\":%s,"
        "\"t_us\":%" PRIi64
        "}",
        g_ctx.node_id,
        (int64_t)snap.last_sample_time_us,
        SDACS_FW_VERSION,
        phase,
        (double)snap.soc_percent,
        (double)snap.voltage_v,
        (double)snap.charge_rate_percent_per_hr,
        (unsigned)snap.sample_count,
        (unsigned)snap.error_count,
        snap.valid ? "true" : "false",
        (int64_t)snap.last_sample_time_us
    );
    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Battery snapshot payload truncated; dropping %s", phase);
        return false;
    }

    err = wifi_mqtt_publish_raw(g_ctx.topic, payload, (size_t)len, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Battery %s publish failed: %s", phase, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Battery %s snapshot published", phase);
    return true;
}

void fuel_gauge_stop(void)
{
    if (!g_ctx.running) {
        return;
    }

    g_ctx.running = false;
    if (g_ctx.task) {
        xTaskNotifyGive(g_ctx.task);
    }

    for (int i = 0; i < 120 && g_ctx.task != NULL; ++i) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (g_ctx.task != NULL) {
        ESP_LOGW(TAG, "Fuel gauge task did not exit cleanly");
        return;
    }

    if (g_ctx.lock) {
        vSemaphoreDelete(g_ctx.lock);
        g_ctx.lock = NULL;
    }
}
