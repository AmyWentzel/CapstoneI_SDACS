#include "audio_input.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#include "sdacs_config.h"

typedef struct {
    i2s_chan_handle_t rx_chan;
    int32_t raw[SDACS_I2S_FRAMES_PER_READ];
    bool initialized;
} audio_input_t;

static const char *TAG = "audio_input";
static audio_input_t s_audio = {0};

static inline int32_t i2s_word_to_s24(int32_t word)
{
    int32_t sample = (int32_t)((uint32_t)word >> 8);
    if (sample & 0x00800000) {
        sample |= ~0x00FFFFFF;
    }
    return sample;
}

esp_err_t audio_input_init(void)
{
    if (s_audio.initialized) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_audio.rx_chan), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SDACS_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SDACS_I2S_BCLK_GPIO,
            .ws = SDACS_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = SDACS_I2S_DIN_GPIO,
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx_chan, &std_cfg), TAG, "init std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx_chan), TAG, "enable failed");

    s_audio.initialized = true;
    ESP_LOGI(TAG, "I2S initialized: BCLK=%d WS=%d DIN=%d SR=%d",
             SDACS_I2S_BCLK_GPIO, SDACS_I2S_WS_GPIO, SDACS_I2S_DIN_GPIO, SDACS_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_input_read_s24(int32_t *dst,
                               size_t max_samples,
                               size_t *samples_read,
                               uint32_t timeout_ms)
{
    size_t bytes_read = 0;

    if (!dst || max_samples < SDACS_I2S_FRAMES_PER_READ || !samples_read) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_audio.initialized || !s_audio.rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2s_channel_read(
        s_audio.rx_chan,
        s_audio.raw,
        sizeof(s_audio.raw),
        &bytes_read,
        pdMS_TO_TICKS(timeout_ms)
    );
    if (err != ESP_OK) {
        *samples_read = 0;
        return err;
    }

    *samples_read = bytes_read / sizeof(int32_t);
    if (*samples_read > max_samples) {
        *samples_read = max_samples;
    }

    for (size_t i = 0; i < *samples_read; ++i) {
        dst[i] = i2s_word_to_s24(s_audio.raw[i]);
    }

    return ESP_OK;
}

void audio_input_deinit(void)
{
    if (!s_audio.initialized) {
        return;
    }

    if (s_audio.rx_chan) {
        i2s_channel_disable(s_audio.rx_chan);
        i2s_del_channel(s_audio.rx_chan);
    }

    memset(&s_audio, 0, sizeof(s_audio));
}
