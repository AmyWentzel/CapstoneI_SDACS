#include "fuel_gauge.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "fuel_gauge";

/* Metro ESP32-S3 onboard I2C for MAX17048 */
#define FUEL_GAUGE_I2C_PORT          0
#define FUEL_GAUGE_I2C_SDA_IO        47
#define FUEL_GAUGE_I2C_SCL_IO        48
#define FUEL_GAUGE_I2C_FREQ_HZ       100000

#define MAX17048_I2C_ADDR            0x36

#define MAX17048_REG_VCELL           0x02
#define MAX17048_REG_SOC             0x04
#define MAX17048_REG_VERSION         0x08

#define MAX17048_VCELL_LSB_VOLTS     (78.125e-6f)
#define MAX17048_SOC_LSB_PERCENT     (1.0f / 256.0f)

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_initialized = false;

static esp_err_t max17048_read_reg16(uint8_t reg, uint16_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized || s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2] = {0};

    esp_err_t err = i2c_master_transmit_receive(
        s_dev,
        &reg, 1,
        data, sizeof(data),
        pdMS_TO_TICKS(100)
    );
    if (err != ESP_OK) {
        return err;
    }

    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

esp_err_t fuel_gauge_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = FUEL_GAUGE_I2C_PORT,
        .sda_io_num = FUEL_GAUGE_I2C_SDA_IO,
        .scl_io_num = FUEL_GAUGE_I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX17048_I2C_ADDR,
        .scl_speed_hz = FUEL_GAUGE_I2C_FREQ_HZ,
    };

    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;

    uint16_t version = 0;
    err = fuel_gauge_get_version(&version);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MAX17048 probe failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "MAX17048 initialized, version=0x%04X", version);
    return ESP_OK;
}

esp_err_t fuel_gauge_get_voltage(float *voltage_v)
{
    if (voltage_v == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = 0;
    esp_err_t err = max17048_read_reg16(MAX17048_REG_VCELL, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *voltage_v = ((float)raw) * MAX17048_VCELL_LSB_VOLTS;
    return ESP_OK;
}

esp_err_t fuel_gauge_get_soc(float *soc_percent)
{
    if (soc_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = 0;
    esp_err_t err = max17048_read_reg16(MAX17048_REG_SOC, &raw);
    if (err != ESP_OK) {
        return err;
    }

    *soc_percent = ((float)raw) * MAX17048_SOC_LSB_PERCENT;

    if (*soc_percent < 0.0f) *soc_percent = 0.0f;
    if (*soc_percent > 100.0f) *soc_percent = 100.0f;

    return ESP_OK;
}

esp_err_t fuel_gauge_get_version(uint16_t *version_raw)
{
    if (version_raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return max17048_read_reg16(MAX17048_REG_VERSION, version_raw);
}

esp_err_t fuel_gauge_read(fuel_gauge_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    esp_err_t err_v = fuel_gauge_get_voltage(&out->voltage_v);
    esp_err_t err_s = fuel_gauge_get_soc(&out->soc_percent);
    esp_err_t err_ver = fuel_gauge_get_version(&out->version_raw);

    out->valid = (err_v == ESP_OK && err_s == ESP_OK && err_ver == ESP_OK);

    if (!out->valid) {
        if (err_v != ESP_OK) return err_v;
        if (err_s != ESP_OK) return err_s;
        return err_ver;
    }

    return ESP_OK;
}