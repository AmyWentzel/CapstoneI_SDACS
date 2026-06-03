#include "temp_humidity.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "driver/i2c.h"
#include "config_store.h"
#include "mqtt_publish.h"

static const char *TAG = "temp_humidity";

#define TEMP_HUMIDITY_HISTORY_CAP 128

typedef struct
{
    int i2c_port;
    uint8_t addr;
    uint32_t period_ms;

    TaskHandle_t task;
    SemaphoreHandle_t lock;

    temp_humidity_reading_t latest;
    temp_humidity_reading_t history[TEMP_HUMIDITY_HISTORY_CAP];
    size_t history_count;
    bool running;
    uint32_t publish_fail_count;
    char topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
    char node_id[CONFIG_STORE_MAX_NODE_ID_LEN + 1];
} temp_humidity_ctx_t;

static temp_humidity_ctx_t g_ctx = {0};

static void build_publish_topic(char *out, size_t out_sz)
{
    const char *base_topic = NULL;
    const char *broker_uri = NULL;
    if (!out || out_sz == 0) return;

    out[0] = '\0';
    esp_err_t err = config_store_get_mqtt(&broker_uri, &base_topic);
    (void)broker_uri;
    if (err != ESP_OK || !base_topic || base_topic[0] == '\0') return;

    int n = snprintf(out, out_sz, "%s/temp_humidity", base_topic);
    if (n <= 0 || n >= (int)out_sz) {
        out[0] = '\0';
    }
}

static void load_node_id(char *out, size_t out_sz)
{
    const char *node_id = NULL;
    if (!out || out_sz == 0) return;

    out[0] = '\0';
    if (config_store_get_node_id(&node_id) == ESP_OK && node_id && node_id[0] != '\0') {
        strncpy(out, node_id, out_sz - 1);
        out[out_sz - 1] = '\0';
    } else {
        strncpy(out, "node01", out_sz - 1);
        out[out_sz - 1] = '\0';
    }
}

static esp_err_t i2c_bus_init(int port, int sda_gpio, int scl_gpio, uint32_t freq_hz)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) return err;

    err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver already installed on port %d; continuing", port);
        return ESP_OK;
    }
    return err;
}

static esp_err_t i2c_write(int port, uint8_t addr, const uint8_t *data, size_t len)
{
    return i2c_master_write_to_device(port, addr, data, len, pdMS_TO_TICKS(250));
}

static esp_err_t i2c_read(int port, uint8_t addr, uint8_t *data, size_t len)
{
    return i2c_master_read_from_device(port, addr, data, len, pdMS_TO_TICKS(250));
}

static uint8_t sht4x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    const uint8_t poly = 0x31;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ poly) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t sht41_read_temp_rh(int port, uint8_t addr, float *temp_c, float *rh_percent)
{
    if (!temp_c || !rh_percent) return ESP_ERR_INVALID_ARG;

    const uint8_t cmd = 0xFD; // SHT41 high precision measurement, no heater.
    esp_err_t err = i2c_write(port, addr, &cmd, sizeof(cmd));
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t buf[6] = {0};
    err = i2c_read(port, addr, buf, sizeof(buf));
    if (err != ESP_OK) return err;

    if (sht4x_crc8(&buf[0], 2) != buf[2]) return ESP_ERR_INVALID_CRC;
    if (sht4x_crc8(&buf[3], 2) != buf[5]) return ESP_ERR_INVALID_CRC;

    const uint16_t raw_t  = (uint16_t)((buf[0] << 8) | buf[1]);
    const uint16_t raw_rh = (uint16_t)((buf[3] << 8) | buf[4]);
    const float denom = 65535.0f;

    *temp_c     = -45.0f + 175.0f * ((float)raw_t / denom);
    *rh_percent = -6.0f + 125.0f * ((float)raw_rh / denom);

    if (*rh_percent < 0.0f)   *rh_percent = 0.0f;
    if (*rh_percent > 100.0f) *rh_percent = 100.0f;

    return ESP_OK;
}

static void temp_humidity_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task started (period=%ums, addr=0x%02X)",
             (unsigned)g_ctx.period_ms, g_ctx.addr);
    if (g_ctx.topic[0] != '\0') {
        ESP_LOGI(TAG, "Publishing temp/humidity to topic '%s'", g_ctx.topic);
    } else {
        ESP_LOGW(TAG, "No MQTT topic configured for temp/humidity; local sampling only.");
    }

    while (g_ctx.running) {
        float t = 0.0f;
        float rh = 0.0f;

        esp_err_t err = sht41_read_temp_rh(g_ctx.i2c_port, g_ctx.addr, &t, &rh);

        temp_humidity_reading_t snap;

        xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
        g_ctx.latest.sample_count++;
        g_ctx.latest.last_sample_time_us = esp_timer_get_time();

        if (err == ESP_OK) {
            g_ctx.latest.temp_c = t;
            g_ctx.latest.rh_percent = rh;
            g_ctx.latest.valid = true;
        } else {
            g_ctx.latest.error_count++;
        }

        snap = g_ctx.latest;
        if (g_ctx.history_count < TEMP_HUMIDITY_HISTORY_CAP) {
            g_ctx.history[g_ctx.history_count++] = snap;
        } else {
            memmove(&g_ctx.history[0], &g_ctx.history[1],
                    sizeof(g_ctx.history[0]) * (TEMP_HUMIDITY_HISTORY_CAP - 1));
            g_ctx.history[TEMP_HUMIDITY_HISTORY_CAP - 1] = snap;
        }

        xSemaphoreGive(g_ctx.lock);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "T=%.2f C, RH=%.1f %% (n=%u)",
                     (double)snap.temp_c, (double)snap.rh_percent, (unsigned)snap.sample_count);
        } else {
            ESP_LOGW(TAG, "SHT41 read failed: %s (err_count=%u)",
                     esp_err_to_name(err), (unsigned)snap.error_count);
        }

        // Sleep until next period, but allow immediate wake on stop request.
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(g_ctx.period_ms));
    }

    ESP_LOGI(TAG, "Task stopping");
    g_ctx.task = NULL;
    vTaskDelete(NULL);
}

bool temp_humidity_start(int i2c_port,
                         int sda_gpio,
                         int scl_gpio,
                         uint32_t i2c_freq_hz,
                         uint8_t sensor_addr,
                         uint32_t period_ms)
{
    if (g_ctx.running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    if (period_ms < 200 || period_ms > 60000) {
        ESP_LOGE(TAG, "Invalid period_ms=%u", (unsigned)period_ms);
        return false;
    }

    esp_err_t err = i2c_bus_init(i2c_port, sda_gpio, scl_gpio, i2c_freq_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return false;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.i2c_port = i2c_port;
    g_ctx.addr = sensor_addr;
    g_ctx.period_ms = period_ms;
    build_publish_topic(g_ctx.topic, sizeof(g_ctx.topic));
    load_node_id(g_ctx.node_id, sizeof(g_ctx.node_id));

    g_ctx.lock = xSemaphoreCreateMutex();
    if (!g_ctx.lock) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    g_ctx.latest = (temp_humidity_reading_t){
        .temp_c = 0.0f,
        .rh_percent = 0.0f,
        .sample_count = 0,
        .error_count = 0,
        .last_sample_time_us = 0,
        .valid = false
    };

    g_ctx.running = true;

    BaseType_t ok = xTaskCreate(
        temp_humidity_task,
        "temp_humidity",
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

    return true;
}

bool temp_humidity_get_latest(temp_humidity_reading_t *out)
{
    if (!out || !g_ctx.lock) return false;

    xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
    *out = g_ctx.latest;
    bool valid = g_ctx.latest.valid;
    xSemaphoreGive(g_ctx.lock);

    return valid;
}

bool temp_humidity_get_history(temp_humidity_reading_t *out, size_t max_count, size_t *out_count)
{
    if (!out || !out_count || !g_ctx.lock) {
        return false;
    }

    xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
    size_t count = g_ctx.history_count;
    if (count > max_count) {
        count = max_count;
    }
    if (count > 0) {
        memcpy(out, g_ctx.history, count * sizeof(out[0]));
    }
    *out_count = count;
    xSemaphoreGive(g_ctx.lock);

    return count > 0;
}

bool temp_humidity_publish_latest_once(const char *phase)
{
    if (!phase) phase = "unknown";
    if (!g_ctx.lock || g_ctx.topic[0] == '\0') return false;

    temp_humidity_reading_t snap = {0};
    xSemaphoreTake(g_ctx.lock, portMAX_DELAY);
    snap = g_ctx.latest;
    xSemaphoreGive(g_ctx.lock);

    if (!snap.valid) {
        ESP_LOGW(TAG, "No valid temp/humidity sample yet; skip %s publish", phase);
        return false;
    }

    char payload[224];
    int len = snprintf(
        payload, sizeof(payload),
        "{\"phase\":\"%s\",\"node\":\"%s\",\"t_us\":%" PRIi64 ",\"seq\":%u,\"temp_c\":%.2f,\"rh_percent\":%.2f,\"err\":%u}",
        phase,
        g_ctx.node_id,
        (int64_t)snap.last_sample_time_us,
        (unsigned)snap.sample_count,
        (double)snap.temp_c,
        (double)snap.rh_percent,
        (unsigned)snap.error_count
    );
    if (len <= 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "Temp/humidity snapshot payload truncated; dropping %s", phase);
        return false;
    }

    esp_err_t perr = mqtt_publish_raw(g_ctx.topic, payload, (size_t)len, 0, 0);
    if (perr != ESP_OK) {
        ESP_LOGW(TAG, "Temp/humidity %s publish failed: %s", phase, esp_err_to_name(perr));
        return false;
    }

    ESP_LOGI(TAG, "Temp/humidity %s snapshot published", phase);
    return true;
}

void temp_humidity_stop(void)
{
    if (!g_ctx.running) return;

    g_ctx.running = false;
    if (g_ctx.task) {
        xTaskNotifyGive(g_ctx.task);
    }

    // Wait up to ~3s for clean exit.
    for (int i = 0; i < 120 && g_ctx.task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (g_ctx.task != NULL) {
        ESP_LOGW(TAG, "Temp/humidity task did not exit cleanly");
        return;
    }

    if (g_ctx.lock) {
        vSemaphoreDelete(g_ctx.lock);
        g_ctx.lock = NULL;
    }
}
