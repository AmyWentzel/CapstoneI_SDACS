#include "recordAudio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdCard.h"
#include "sdacs_config.h"

static const char *TAG = "recordAudio";
static bool s_initialized = false;

typedef struct {
    i2s_chan_handle_t rx_chan;
    uint8_t raw[SDACS_I2S_FRAMES_PER_READ * 4];
    bool initialized;
} audio_input_t;

static audio_input_t s_audio = {0};

static inline int32_t raw_bytes_to_s24(const uint8_t bytes[3])
{
    int32_t sample = (int32_t)bytes[0] |
                     ((int32_t)bytes[1] << 8) |
                     ((int32_t)bytes[2] << 16);
    if (sample & 0x00800000) {
        sample |= ~0x00FFFFFF;
    }
    return sample;
}

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

    i2s_pdm_rx_clk_config_t clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SDACS_SAMPLE_RATE_HZ);

    i2s_pdm_rx_slot_config_t slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_MONO);

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .clk = SDACS_I2S_BCLK_GPIO,
            .din = SDACS_I2S_DIN_GPIO,
            .invert_flags = {0},
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_audio.rx_chan, &pdm_cfg), TAG, "init pdm rx mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx_chan), TAG, "enable failed");

    s_audio.initialized = true;
    ESP_LOGI(TAG, "PDM RX initialized: BCLK=%d DIN=%d WS=%d SR=%d",
             SDACS_I2S_BCLK_GPIO, SDACS_I2S_DIN_GPIO, SDACS_I2S_WS_GPIO, SDACS_SAMPLE_RATE_HZ);
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

    size_t sample_count = 0;
    if (bytes_read % 2 == 0 && (bytes_read / 2) <= max_samples) {
        sample_count = bytes_read / 2;
        for (size_t i = 0; i < sample_count; ++i) {
            uint16_t word = (uint16_t)s_audio.raw[i * 2] | ((uint16_t)s_audio.raw[i * 2 + 1] << 8);
            int32_t sample = (int16_t)word;
            dst[i] = sample << 8;
        }
    } else if (bytes_read % 3 == 0 && (bytes_read / 3) <= max_samples) {
        sample_count = bytes_read / 3;
        for (size_t i = 0; i < sample_count; ++i) {
            dst[i] = raw_bytes_to_s24(&s_audio.raw[i * 3]);
        }
    } else if (bytes_read % 4 == 0 && (bytes_read / 4) <= max_samples) {
        sample_count = bytes_read / 4;
        for (size_t i = 0; i < sample_count; ++i) {
            int32_t word = 0;
            memcpy(&word, &s_audio.raw[i * 4], sizeof(word));
            dst[i] = i2s_word_to_s24(word);
        }
    } else {
        sample_count = bytes_read / 2;
        if (sample_count > max_samples) {
            sample_count = max_samples;
        }
        for (size_t i = 0; i < sample_count; ++i) {
            uint16_t word = (uint16_t)s_audio.raw[i * 2] | ((uint16_t)s_audio.raw[i * 2 + 1] << 8);
            int32_t sample = (int16_t)word;
            dst[i] = sample << 8;
        }
    }

    *samples_read = sample_count;
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

bool recordAudio_init(void)
{
    if (s_initialized) {
        return true;
    }

    if (!sdCard_init()) {
        ESP_LOGE(TAG, "SD card initialization failed");
        return false;
    }

    esp_err_t err = audio_input_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio input init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_initialized = true;
    return true;
}

bool recordAudio_capture(uint32_t seconds)
{
    if (!recordAudio_init()) {
        return false;
    }

    if (!sdCard_create_session()) {
        ESP_LOGE(TAG, "SD card session creation failed");
        return false;
    }

    if (!sdCard_open_raw()) {
        ESP_LOGE(TAG, "Unable to open raw audio file");
        return false;
    }

    int64_t start_time = esp_timer_get_time();
    int64_t end_time = start_time + (int64_t)seconds * 1000000LL;
    static int32_t sample_buffer[SDACS_I2S_FRAMES_PER_READ];
    static int32_t chunk_buffer[SDACS_AUDIO_CHUNK_SAMPLES];
    static uint8_t raw_output[SDACS_AUDIO_CHUNK_SAMPLES * 3];

    size_t chunk_fill = 0;
    size_t total_samples = 0;
    size_t total_writes = 0;
    size_t total_timeouts = 0;

    ESP_LOGI(TAG, "Starting capture: %u s target", (unsigned)seconds);
    while (esp_timer_get_time() < end_time) {
        size_t samples_read = 0;
        esp_err_t err = audio_input_read_s24(
            sample_buffer,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );

        if (err == ESP_ERR_TIMEOUT) {
            total_timeouts++;
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }
        if (samples_read == 0) {
            ESP_LOGW(TAG, "I2S read returned 0 samples");
            continue;
        }

        total_samples += samples_read;
        for (size_t i = 0; i < samples_read; ++i) {
            chunk_buffer[chunk_fill++] = sample_buffer[i];
            if (chunk_fill >= SDACS_AUDIO_CHUNK_SAMPLES) {
                size_t bytes = chunk_fill * 3;
                for (size_t j = 0; j < chunk_fill; ++j) {
                    int32_t sample = chunk_buffer[j];
                    raw_output[j * 3 + 0] = (uint8_t)(sample & 0xFF);
                    raw_output[j * 3 + 1] = (uint8_t)((sample >> 8) & 0xFF);
                    raw_output[j * 3 + 2] = (uint8_t)((sample >> 16) & 0xFF);
                }
                if (!sdCard_append_raw(raw_output, bytes)) {
                    ESP_LOGW(TAG, "Failed to write raw audio chunk to SD");
                } else {
                    total_writes++;
                }
                chunk_fill = 0;
            }
        }
    }

    if (chunk_fill > 0) {
        size_t bytes = chunk_fill * 3;
        for (size_t j = 0; j < chunk_fill; ++j) {
            int32_t sample = chunk_buffer[j];
            raw_output[j * 3 + 0] = (uint8_t)(sample & 0xFF);
            raw_output[j * 3 + 1] = (uint8_t)((sample >> 8) & 0xFF);
            raw_output[j * 3 + 2] = (uint8_t)((sample >> 16) & 0xFF);
        }
        if (!sdCard_append_raw(raw_output, bytes)) {
            ESP_LOGW(TAG, "Failed to write final raw audio chunk to SD");
        } else {
            total_writes++;
        }
    }

    if (!sdCard_close_raw()) {
        ESP_LOGW(TAG, "Failed to close raw audio file cleanly");
    }

    float recorded_s = (esp_timer_get_time() - start_time) / 1000000.0f;
    ESP_LOGI(TAG, "Audio capture: %.2f s, %u samples, %u SD writes, %u timeouts", recorded_s, (unsigned)total_samples, (unsigned)total_writes, (unsigned)total_timeouts);

    if (total_writes == 0) {
        ESP_LOGE(TAG, "Audio capture completed but wrote no raw chunks to SD");
        return false;
    }

    if (recorded_s < (float)seconds - 0.1f) {
        ESP_LOGW(TAG, "Capture finished early: expected %u s, got %.2f s", (unsigned)seconds, recorded_s);
    }

    return true;
}

bool recordAudio_shutdown(void)
{
    if (!s_initialized) {
        return true;
    }

    audio_input_deinit();
    s_initialized = false;
    return true;
}
