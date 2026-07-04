#include "audio_input.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#include "sdacs_config.h"

typedef struct {
    i2s_chan_handle_t rx_chan;
    int32_t raw[SDACS_I2S_FRAMES_PER_READ];
    audio_input_debug_t last_debug;
    bool initialized;
} audio_input_t;

static const char *TAG = "audio_input";
static audio_input_t s_audio = {0};
static uint32_t s_i2s_debug_read_count = 0;

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
        /*
         * ICS-43432 is a mono mic using stereo I2S frame timing. Its LR pin
         * selects which left/right slot carries valid data.
         */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SDACS_I2S_BCLK_GPIO,
            .ws = SDACS_I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = SDACS_I2S_DIN_GPIO,
        },
    };

#if SDACS_I2S_USE_RIGHT_SLOT
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    const char *selected_slot = "right";
#else
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    const char *selected_slot = "left";
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx_chan, &std_cfg), TAG, "init std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx_chan), TAG, "enable failed");

    s_audio.initialized = true;
    ESP_LOGI(TAG, "I2S initialized: mic=%s format=philips slot_mode=stereo_frame selected_slot=%s BCLK=%d WS=%d DIN=%d SR=%d valid_bits=%d slot_bits=%d",
             SDACS_MIC_MODEL,
             selected_slot,
             SDACS_I2S_BCLK_GPIO,
             SDACS_I2S_WS_GPIO,
             SDACS_I2S_DIN_GPIO,
             SDACS_SAMPLE_RATE_HZ,
             SDACS_MIC_VALID_BITS,
             SDACS_MIC_I2S_SLOT_BITS);
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

    size_t raw_words_read = bytes_read / sizeof(int32_t);
    size_t selected_samples_read = raw_words_read;
    if (selected_samples_read > max_samples) {
        selected_samples_read = max_samples;
    }
    *samples_read = selected_samples_read;

    int32_t min_sample = INT32_MAX;
    int32_t max_sample = INT32_MIN;
    uint32_t zero_count = 0;

    for (size_t i = 0; i < selected_samples_read; ++i) {
        int32_t sample = i2s_word_to_s24(s_audio.raw[i]);
        dst[i] = sample;

        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
        if (sample == 0) {
            ++zero_count;
        }
    }

    if (selected_samples_read == 0) {
        min_sample = 0;
        max_sample = 0;
    }

    ++s_i2s_debug_read_count;
    s_audio.last_debug = (audio_input_debug_t){
        .raw0 = (selected_samples_read > 0) ? (uint32_t)s_audio.raw[0] : 0U,
        .sample0 = (selected_samples_read > 0) ? dst[0] : 0,
        .min_sample = min_sample,
        .max_sample = max_sample,
        .zero_count = zero_count,
        .bytes_read = bytes_read,
        .raw_words_read = raw_words_read,
        .samples_read = selected_samples_read,
    };

    if ((s_i2s_debug_read_count % 20U) == 0U) {
        ESP_LOGI(TAG,
                 "I2S debug: bytes_read=%" PRIu32 " raw_words_read=%" PRIu32 " selected_samples_read=%" PRIu32 " raw0=0x%08" PRIX32 " sample0=%" PRId32 " min=%" PRId32 " max=%" PRId32 " p2p_raw=%" PRId32 " zeros=%" PRIu32,
                 (uint32_t)bytes_read,
                 (uint32_t)raw_words_read,
                 (uint32_t)selected_samples_read,
                 s_audio.last_debug.raw0,
                 s_audio.last_debug.sample0,
                 min_sample,
                 max_sample,
                 max_sample - min_sample,
                 zero_count);
    }

    return ESP_OK;
}

bool audio_input_get_last_debug(audio_input_debug_t *out)
{
    if (!out || !s_audio.initialized) {
        return false;
    }

    *out = s_audio.last_debug;
    return true;
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
