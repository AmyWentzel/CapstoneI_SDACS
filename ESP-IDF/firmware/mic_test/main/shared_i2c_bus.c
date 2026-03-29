#include "shared_i2c_bus.h"

#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "shared_i2c_bus";

static bool s_ready = false;
static shared_i2c_bus_config_t s_cfg = {0};

esp_err_t shared_i2c_bus_init(const shared_i2c_bus_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ready) {
        if (s_cfg.port == cfg->port &&
            s_cfg.sda_gpio == cfg->sda_gpio &&
            s_cfg.scl_gpio == cfg->scl_gpio &&
            s_cfg.freq_hz == cfg->freq_hz) {
            ESP_LOGI(TAG, "I2C bus already initialized");
            return ESP_OK;
        }
        ESP_LOGE(TAG, "I2C bus already initialized with different config");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = cfg->freq_hz,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(cfg->port, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(cfg->port, conf.mode, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_cfg = *cfg;
    s_ready = true;

    ESP_LOGI(TAG, "I2C bus ready: port=%d SDA=%d SCL=%d freq=%lu",
             s_cfg.port, s_cfg.sda_gpio, s_cfg.scl_gpio, (unsigned long)s_cfg.freq_hz);

    return ESP_OK;
}

bool shared_i2c_bus_is_ready(void)
{
    return s_ready;
}

int shared_i2c_bus_port(void)
{
    return s_cfg.port;
}

esp_err_t shared_i2c_write(int port, uint8_t addr, const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_write_to_device(port, addr, data, len, timeout_ticks);
}

esp_err_t shared_i2c_read(int port, uint8_t addr, uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_read_from_device(port, addr, data, len, timeout_ticks);
}

esp_err_t shared_i2c_write_read(int port, uint8_t addr,
                                const uint8_t *tx_data, size_t tx_len,
                                uint8_t *rx_data, size_t rx_len,
                                TickType_t timeout_ticks)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_write_read_device(port, addr, tx_data, tx_len, rx_data, rx_len, timeout_ticks);
}
