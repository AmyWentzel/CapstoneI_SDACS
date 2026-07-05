#include "audio_input.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "sdacs_config.h"

typedef struct {
    i2s_chan_handle_t rx_chan;
    int32_t raw[SDACS_I2S_FRAMES_PER_READ * 2U];
    audio_input_debug_t last_debug;
    audio_input_counters_t counters;
    bool initialized;
    bool rx_enabled;
} audio_input_t;

static const char *TAG = "audio_input";
static audio_input_t s_audio = {0};
static uint32_t s_i2s_debug_read_count = 0;
static bool s_raw_diagnostics_enabled = true;

typedef struct {
    int32_t min;
    int32_t max;
    int32_t peak_abs;
    double sum_sq;
    uint32_t count;
} conversion_accum_t;

typedef struct {
    uint32_t raw_word0;
    uint32_t raw_word1;
    uint32_t raw_word_min;
    uint32_t raw_word_max;
    uint32_t raw_word_nonzero_count;
    uint32_t raw_word_repeated_count;
    uint32_t converted_zeros;
    uint32_t sample_count;
    int32_t converted_sample0;
    conversion_accum_t current;
    conversion_accum_t shift8;
    conversion_accum_t low24;
    conversion_accum_t shift16;
    bool have_sample;
    bool have_previous_raw;
    uint32_t previous_raw_word;
} raw_diag_accum_t;

static raw_diag_accum_t s_raw_diag = {0};

static size_t audio_input_raw_words_per_read(void)
{
#if SDACS_I2S_RX_MODE == SDACS_I2S_RX_MODE_STEREO_RAW
    return (size_t)SDACS_I2S_FRAMES_PER_READ * 2U;
#else
    return (size_t)SDACS_I2S_FRAMES_PER_READ;
#endif
}

static size_t audio_input_read_bytes(void)
{
    return audio_input_raw_words_per_read() * sizeof(int32_t);
}

static size_t audio_input_selected_sample_capacity(void)
{
    /* One selected mic sample is produced for each I2S frame. In stereo_raw,
     * each frame contains two raw 32-bit words: left then right.
     */
    return SDACS_I2S_FRAMES_PER_READ;
}

static void audio_input_note_read_elapsed(uint32_t elapsed_us)
{
    if (s_audio.counters.total_read_calls == 0U ||
        elapsed_us < s_audio.counters.min_read_elapsed_us) {
        s_audio.counters.min_read_elapsed_us = elapsed_us;
    }
    if (elapsed_us > s_audio.counters.max_read_elapsed_us) {
        s_audio.counters.max_read_elapsed_us = elapsed_us;
    }
    s_audio.counters.total_read_elapsed_us += elapsed_us;
}

static void audio_input_note_timeout_elapsed(uint32_t elapsed_us)
{
    if (s_audio.counters.timeouts <= 1U ||
        elapsed_us < s_audio.counters.min_timeout_elapsed_us) {
        s_audio.counters.min_timeout_elapsed_us = elapsed_us;
    }
    if (elapsed_us > s_audio.counters.max_timeout_elapsed_us) {
        s_audio.counters.max_timeout_elapsed_us = elapsed_us;
    }
    s_audio.counters.total_timeout_elapsed_us += elapsed_us;
    if (elapsed_us < 1000U) {
        s_audio.counters.timeout_immediate_count++;
        vTaskDelay(1);
    }
}

static void audio_input_note_error(esp_err_t err)
{
    s_audio.counters.last_error = err;
    snprintf(s_audio.counters.last_error_name,
             sizeof(s_audio.counters.last_error_name),
             "%s",
             esp_err_to_name(err));
}

static int32_t sign_extend_24(uint32_t x)
{
    x &= 0x00FFFFFFU;
    if ((x & 0x00800000U) != 0U) {
        x |= 0xFF000000U;
    }
    return (int32_t)x;
}

static int32_t sign_extend_16(uint32_t x)
{
    x &= 0x0000FFFFU;
    if ((x & 0x00008000U) != 0U) {
        x |= 0xFFFF0000U;
    }
    return (int32_t)x;
}

static int32_t convert_shift8(uint32_t raw_word)
{
    return sign_extend_24(raw_word >> 8);
}

static int32_t convert_low24(uint32_t raw_word)
{
    return sign_extend_24(raw_word & 0x00FFFFFFU);
}

static inline int32_t i2s_word_to_production_sample(uint32_t raw_word)
{
    /*
     * ICS-43432 outputs 24-bit two's-complement audio inside a 32-bit I2S
     * slot. Many MEMS mics place valid 24-bit data left-justified, but this
     * MEMS validation build can route either shift8 or low24 into production
     * RMS/dbFS/p2p/FFT metrics while keeping all diagnostic candidates.
     */
#if SDACS_I2S_SAMPLE_CONVERSION_MODE == SDACS_I2S_CONVERSION_LOW24
    return convert_low24(raw_word);
#else
    return convert_shift8(raw_word);
#endif
}

static void conversion_accum_reset(conversion_accum_t *acc)
{
    if (!acc) {
        return;
    }

    acc->min = INT32_MAX;
    acc->max = INT32_MIN;
    acc->peak_abs = 0;
    acc->sum_sq = 0.0;
    acc->count = 0;
}

static void conversion_accum_push(conversion_accum_t *acc, int32_t sample)
{
    int32_t abs_sample = (sample < 0) ? -sample : sample;

    if (!acc) {
        return;
    }

    if (sample < acc->min) {
        acc->min = sample;
    }
    if (sample > acc->max) {
        acc->max = sample;
    }
    if (abs_sample > acc->peak_abs) {
        acc->peak_abs = abs_sample;
    }
    acc->sum_sq += (double)sample * (double)sample;
    acc->count++;
}

static void conversion_diag_from_accum(audio_input_conversion_diag_t *out,
                                       const conversion_accum_t *acc,
                                       float full_scale)
{
    if (!out || !acc || acc->count == 0U) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->dbfs = -120.0f;
        }
        return;
    }

    float rms = sqrtf((float)(acc->sum_sq / (double)acc->count));
    float rms_norm = rms / full_scale;
    *out = (audio_input_conversion_diag_t){
        .min = acc->min,
        .max = acc->max,
        .peak_abs = acc->peak_abs,
        .p2p = acc->max - acc->min,
        .rms = rms,
        .dbfs = 20.0f * log10f(rms_norm + 1e-12f),
    };
}

static void raw_diag_accumulate(uint32_t raw_word, int32_t current_sample)
{
#if SDACS_ENABLE_RAW_SAMPLE_DIAGNOSTICS
    if (!s_raw_diagnostics_enabled) {
        return;
    }

    int32_t shift8_sample = convert_shift8(raw_word);
    int32_t low24_sample = convert_low24(raw_word);
    int32_t shift16_sample = sign_extend_16(raw_word >> 16);

    if (!s_raw_diag.have_sample) {
        s_raw_diag.raw_word0 = raw_word;
        s_raw_diag.raw_word_min = raw_word;
        s_raw_diag.raw_word_max = raw_word;
        s_raw_diag.converted_sample0 = current_sample;
        s_raw_diag.have_sample = true;
    } else {
        if (s_raw_diag.sample_count == 1U) {
            s_raw_diag.raw_word1 = raw_word;
        }
        if (raw_word < s_raw_diag.raw_word_min) {
            s_raw_diag.raw_word_min = raw_word;
        }
        if (raw_word > s_raw_diag.raw_word_max) {
            s_raw_diag.raw_word_max = raw_word;
        }
    }

    if (raw_word != 0U) {
        s_raw_diag.raw_word_nonzero_count++;
    }
    if (s_raw_diag.have_previous_raw && raw_word == s_raw_diag.previous_raw_word) {
        s_raw_diag.raw_word_repeated_count++;
    }
    s_raw_diag.previous_raw_word = raw_word;
    s_raw_diag.have_previous_raw = true;

    if (current_sample == 0) {
        s_raw_diag.converted_zeros++;
    }

    conversion_accum_push(&s_raw_diag.current, current_sample);
    conversion_accum_push(&s_raw_diag.shift8, shift8_sample);
    conversion_accum_push(&s_raw_diag.low24, low24_sample);
    conversion_accum_push(&s_raw_diag.shift16, shift16_sample);
    s_raw_diag.sample_count++;
#else
    (void)raw_word;
    (void)current_sample;
#endif
}

static void raw_diag_reset_internal(void)
{
    memset(&s_raw_diag, 0, sizeof(s_raw_diag));
    s_raw_diag.raw_word_min = UINT32_MAX;
    conversion_accum_reset(&s_raw_diag.current);
    conversion_accum_reset(&s_raw_diag.shift8);
    conversion_accum_reset(&s_raw_diag.low24);
    conversion_accum_reset(&s_raw_diag.shift16);
}

static esp_err_t audio_input_disable_rx(void)
{
    if (!s_audio.rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2s_channel_disable(s_audio.rx_chan);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_audio.rx_enabled = false;
        return ESP_OK;
    }

    audio_input_note_error(err);
    return err;
}

static esp_err_t audio_input_enable_rx(void)
{
    if (!s_audio.rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2s_channel_enable(s_audio.rx_chan);
    if (err != ESP_OK) {
        audio_input_note_error(err);
        return err;
    }

    s_audio.rx_enabled = true;
    return ESP_OK;
}

static void audio_input_warmup_drain(uint32_t warmup_ms)
{
    static int32_t warmup_buf[SDACS_I2S_FRAMES_PER_READ];
    size_t samples_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t warmup_us = (int64_t)warmup_ms * 1000LL;
    bool diag_was_enabled = s_raw_diagnostics_enabled;

    audio_input_set_raw_diagnostics_enabled(false);
    while ((esp_timer_get_time() - start_us) < warmup_us) {
        esp_err_t err = audio_input_read_s24(
            warmup_buf,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2S warmup read failed: %s", esp_err_to_name(err));
            break;
        }
    }
    audio_input_set_raw_diagnostics_enabled(diag_was_enabled);
    audio_input_reset_raw_diagnostics();
}

esp_err_t audio_input_init(void)
{
    if (s_audio.initialized) {
        return ESP_OK;
    }

    const size_t read_bytes = audio_input_read_bytes();
    if (read_bytes > 3500U) {
        ESP_LOGE(TAG,
                 "I2S read size too large for DMA: read_bytes=%u raw_words=%u frames=%u rx_mode=%s",
                 (unsigned)read_bytes,
                 (unsigned)audio_input_raw_words_per_read(),
                 (unsigned)SDACS_I2S_FRAMES_PER_READ,
                 SDACS_I2S_RX_MODE_LABEL);
        return ESP_ERR_INVALID_SIZE;
    }

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = SDACS_I2S_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = SDACS_I2S_DMA_FRAME_NUM;
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

#if SDACS_I2S_RX_MODE == SDACS_I2S_RX_MODE_STEREO_RAW
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
#elif SDACS_I2S_USE_RIGHT_SLOT
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#else
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx_chan, &std_cfg), TAG, "init std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx_chan), TAG, "enable failed");

    s_audio.initialized = true;
    s_audio.rx_enabled = true;
    raw_diag_reset_internal();
    ESP_LOGI(TAG,
             "I2S mic config: mic=%s format=philips frame=stereo selected_slot=%s conversion=%s sample_rate=%d clk_src=%d mclk_multiple=%d data_bits=%d slot_bits=%d valid_bits=%d BCLK=%d WS=%d DIN=%d slot_mask=%s driver_rx=%s dma_desc_num=%u dma_frame_num=%u read_frames=%u read_raw_words=%u read_bytes=%u expected_selected_samples_per_read=%u",
             SDACS_MIC_MODEL,
             SDACS_I2S_SELECTED_SLOT_LABEL,
             SDACS_I2S_SAMPLE_CONVERSION_LABEL,
             SDACS_SAMPLE_RATE_HZ,
             (int)std_cfg.clk_cfg.clk_src,
             (int)std_cfg.clk_cfg.mclk_multiple,
             32,
             SDACS_MIC_I2S_SLOT_BITS,
             SDACS_MIC_VALID_BITS,
             SDACS_I2S_BCLK_GPIO,
             SDACS_I2S_WS_GPIO,
             SDACS_I2S_DIN_GPIO,
             SDACS_I2S_DRIVER_SLOT_MASK_LABEL,
             SDACS_I2S_RX_MODE_LABEL,
             (unsigned)SDACS_I2S_DMA_DESC_NUM,
             (unsigned)SDACS_I2S_DMA_FRAME_NUM,
             (unsigned)SDACS_I2S_FRAMES_PER_READ,
             (unsigned)audio_input_raw_words_per_read(),
             (unsigned)read_bytes,
             (unsigned)audio_input_selected_sample_capacity());
    return ESP_OK;
}

esp_err_t audio_input_prepare_for_capture(void)
{
    esp_err_t err = ESP_OK;

    if (!s_audio.initialized || !s_audio.rx_chan) {
        err = audio_input_init();
        if (err != ESP_OK) {
            audio_input_note_error(err);
            return err;
        }
    }

    audio_input_reset_counters();
    audio_input_reset_raw_diagnostics();

    err = audio_input_disable_rx();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S prepare_for_capture disable failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    err = audio_input_enable_rx();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S prepare_for_capture enable failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(80));
    audio_input_warmup_drain(SDACS_I2S_PREFLIGHT_WARMUP_MS);
    audio_input_reset_counters();

    ESP_LOGI(TAG,
             "I2S prepare_for_capture: restart rx channel, warmup_ms=%u, selected_slot=%s, conversion=%s rx_mode=%s",
             (unsigned)SDACS_I2S_PREFLIGHT_WARMUP_MS,
             SDACS_I2S_SELECTED_SLOT_LABEL,
             SDACS_I2S_SAMPLE_CONVERSION_LABEL,
             SDACS_I2S_RX_MODE_LABEL);
    return ESP_OK;
}

esp_err_t audio_input_recover_rx(const char *reason)
{
    esp_err_t err = ESP_OK;

    if (!s_audio.initialized || !s_audio.rx_chan) {
        err = audio_input_init();
        if (err != ESP_OK) {
            audio_input_note_error(err);
            return err;
        }
    }

    ESP_LOGW(TAG,
             "I2S RX recovery: reason=%s action=disable_enable",
             reason ? reason : "");

    err = audio_input_disable_rx();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S RX recovery disable failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    err = audio_input_enable_rx();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX recovery enable failed: %s", esp_err_to_name(err));
        return err;
    }

    s_audio.counters.rx_restarts++;
    audio_input_warmup_drain(SDACS_I2S_PREFLIGHT_WARMUP_MS);
    audio_input_reset_counters();
    return ESP_OK;
}

esp_err_t audio_input_read_s24(int32_t *dst,
                               size_t max_samples,
                               size_t *samples_read,
                               uint32_t timeout_ms)
{
    size_t bytes_read = 0;

    if (!dst || max_samples < audio_input_selected_sample_capacity() || !samples_read) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_audio.initialized || !s_audio.rx_chan) {
        return ESP_ERR_INVALID_STATE;
    }

    int64_t before_us = esp_timer_get_time();
    esp_err_t err = i2s_channel_read(
        s_audio.rx_chan,
        s_audio.raw,
        audio_input_read_bytes(),
        &bytes_read,
        pdMS_TO_TICKS(timeout_ms)
    );
    int64_t after_us = esp_timer_get_time();
    uint32_t read_elapsed_us = (after_us > before_us) ? (uint32_t)(after_us - before_us) : 0U;
    audio_input_note_read_elapsed(read_elapsed_us);
    s_audio.counters.total_read_calls++;
    if (err != ESP_OK) {
        audio_input_note_error(err);
        *samples_read = 0;
        if (err == ESP_ERR_TIMEOUT) {
            s_audio.counters.timeouts++;
            audio_input_note_timeout_elapsed(read_elapsed_us);
#if SDACS_I2S_VERBOSE_TIMEOUT_LOGS
            if ((s_audio.counters.timeouts % 25U) == 0U) {
                ESP_LOGW(TAG,
                         "I2S read timeout count=%" PRIu32 " timeout_ms=%" PRIu32 " elapsed_us=%" PRIu32 " selected_slot=%s rx_mode=%s slot_mode=stereo BCLK=%d WS=%d DIN=%d",
                         s_audio.counters.timeouts,
                         timeout_ms,
                         read_elapsed_us,
                         SDACS_I2S_SELECTED_SLOT_LABEL,
                         SDACS_I2S_RX_MODE_LABEL,
                         SDACS_I2S_BCLK_GPIO,
                         SDACS_I2S_WS_GPIO,
                         SDACS_I2S_DIN_GPIO);
            }
#endif
        } else {
            s_audio.counters.errors++;
        }
        return err;
    }

    size_t raw_words_read = bytes_read / sizeof(int32_t);
#if SDACS_I2S_RX_MODE == SDACS_I2S_RX_MODE_STEREO_RAW
    size_t selected_samples_read = raw_words_read / 2U;
#else
    size_t selected_samples_read = raw_words_read;
#endif
    if (selected_samples_read > max_samples) {
        selected_samples_read = max_samples;
    }
    *samples_read = selected_samples_read;
    s_audio.counters.successful_reads++;
    s_audio.counters.total_bytes_read += bytes_read;
    s_audio.counters.total_raw_words += raw_words_read;
    s_audio.counters.total_selected_samples += selected_samples_read;

    int32_t min_sample = INT32_MAX;
    int32_t max_sample = INT32_MIN;
    int32_t peak_abs = 0;
    uint32_t zero_count = 0;
    uint32_t nonzero_count = 0;
    uint32_t repeated_count = 0;
    uint32_t prev_raw_word = 0;
    uint32_t raw_min = UINT32_MAX;
    uint32_t raw_max = 0;
    bool have_prev_raw = false;

    for (size_t i = 0; i < selected_samples_read; ++i) {
#if SDACS_I2S_RX_MODE == SDACS_I2S_RX_MODE_STEREO_RAW
        size_t raw_index = (i * 2U) + (SDACS_I2S_USE_RIGHT_SLOT ? 1U : 0U);
#else
        size_t raw_index = i;
#endif
        if (raw_index >= raw_words_read) {
            break;
        }
        uint32_t raw_word = (uint32_t)s_audio.raw[raw_index];
        int32_t sample = i2s_word_to_production_sample(raw_word);
        int32_t abs_sample = (sample < 0) ? -sample : sample;
        dst[i] = sample;
        raw_diag_accumulate(raw_word, sample);

        if (sample < min_sample) {
            min_sample = sample;
        }
        if (sample > max_sample) {
            max_sample = sample;
        }
        if (abs_sample > peak_abs) {
            peak_abs = abs_sample;
        }
        if (sample == 0) {
            ++zero_count;
        }
        if (raw_word != 0U) {
            ++nonzero_count;
        }
        if (have_prev_raw && raw_word == prev_raw_word) {
            ++repeated_count;
        }
        prev_raw_word = raw_word;
        have_prev_raw = true;
        if (raw_word < raw_min) {
            raw_min = raw_word;
        }
        if (raw_word > raw_max) {
            raw_max = raw_word;
        }
    }

    if (selected_samples_read == 0) {
        min_sample = 0;
        max_sample = 0;
        raw_min = 0;
    }

    ++s_i2s_debug_read_count;
    s_audio.last_debug = (audio_input_debug_t){
        .raw0 = (raw_words_read > 0) ? (uint32_t)s_audio.raw[0] : 0U,
        .raw1 = (raw_words_read > 1) ? (uint32_t)s_audio.raw[1] : 0U,
        .raw_min = raw_min,
        .raw_max = raw_max,
        .sample0 = (selected_samples_read > 0) ? dst[0] : 0,
        .min_sample = min_sample,
        .max_sample = max_sample,
        .peak_abs = peak_abs,
        .zero_count = zero_count,
        .nonzero_count = nonzero_count,
        .repeated_count = repeated_count,
        .bytes_read = bytes_read,
        .raw_words_read = raw_words_read,
        .samples_read = selected_samples_read,
    };

#if SDACS_I2S_DEBUG_LOGS
    if ((s_i2s_debug_read_count % 20U) == 0U) {
        ESP_LOGI(TAG,
                 "I2S debug: bytes_read=%" PRIu32 " raw_words_read=%" PRIu32 " selected_samples_read=%" PRIu32 " raw0=0x%08" PRIX32 " raw1=0x%08" PRIX32 " raw_min=0x%08" PRIX32 " raw_max=0x%08" PRIX32 " sample0=%" PRId32 " min=%" PRId32 " max=%" PRId32 " peak_abs=%" PRId32 " p2p_raw=%" PRId32 " zeros=%" PRIu32 " raw_nonzero=%" PRIu32 " raw_repeated=%" PRIu32,
                 (uint32_t)bytes_read,
                 (uint32_t)raw_words_read,
                 (uint32_t)selected_samples_read,
                 s_audio.last_debug.raw0,
                 s_audio.last_debug.raw1,
                 s_audio.last_debug.raw_min,
                 s_audio.last_debug.raw_max,
                 s_audio.last_debug.sample0,
                 min_sample,
                 max_sample,
                 peak_abs,
                 max_sample - min_sample,
                 zero_count,
                 nonzero_count,
                 repeated_count);
    }
#endif

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

bool audio_input_get_raw_diagnostics(audio_input_raw_diagnostics_t *out)
{
    if (!out || !s_audio.initialized) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->dbfs_normalization_bits = SDACS_MIC_VALID_BITS;

#if SDACS_ENABLE_RAW_SAMPLE_DIAGNOSTICS
    out->raw_word0 = s_raw_diag.raw_word0;
    out->raw_word1 = s_raw_diag.raw_word1;
    out->raw_word_min = s_raw_diag.have_sample ? s_raw_diag.raw_word_min : 0U;
    out->raw_word_max = s_raw_diag.have_sample ? s_raw_diag.raw_word_max : 0U;
    out->raw_word_nonzero_count = s_raw_diag.raw_word_nonzero_count;
    out->raw_word_repeated_count = s_raw_diag.raw_word_repeated_count;
    out->converted_zeros = s_raw_diag.converted_zeros;
    out->sample_count = s_raw_diag.sample_count;
    out->converted_sample0 = s_raw_diag.converted_sample0;

    conversion_diag_from_accum(&out->current, &s_raw_diag.current, 8388607.0f);
    conversion_diag_from_accum(&out->shift8, &s_raw_diag.shift8, 8388607.0f);
    conversion_diag_from_accum(&out->low24, &s_raw_diag.low24, 8388607.0f);
    conversion_diag_from_accum(&out->shift16, &s_raw_diag.shift16, 32767.0f);

    out->converted_sample_min = out->current.min;
    out->converted_sample_max = out->current.max;
    out->converted_peak_abs = out->current.peak_abs;
    out->converted_p2p_raw = out->current.p2p;
    if (s_raw_diag.current.count > 0U) {
        float rms = sqrtf((float)(s_raw_diag.current.sum_sq / (double)s_raw_diag.current.count));
        out->dbfs_norm_24bit = 20.0f * log10f((rms / 8388607.0f) + 1e-12f);
        out->dbfs_norm_32bit = 20.0f * log10f((rms / 2147483647.0f) + 1e-12f);
    } else {
        out->dbfs_norm_24bit = -120.0f;
        out->dbfs_norm_32bit = -120.0f;
    }
#else
    out->current.dbfs = -120.0f;
    out->shift8.dbfs = -120.0f;
    out->low24.dbfs = -120.0f;
    out->shift16.dbfs = -120.0f;
    out->dbfs_norm_24bit = -120.0f;
    out->dbfs_norm_32bit = -120.0f;
#endif

    return true;
}

void audio_input_set_raw_diagnostics_enabled(bool enabled)
{
    s_raw_diagnostics_enabled = enabled;
    if (!enabled) {
        raw_diag_reset_internal();
    }
}

void audio_input_get_counters(audio_input_counters_t *out)
{
    if (!out) {
        return;
    }

    *out = s_audio.counters;
}

void audio_input_reset_counters(void)
{
    uint32_t rx_restarts = s_audio.counters.rx_restarts;
    uint32_t rx_recreates = s_audio.counters.rx_recreates;

    memset(&s_audio.counters, 0, sizeof(s_audio.counters));
    s_audio.counters.rx_restarts = rx_restarts;
    s_audio.counters.rx_recreates = rx_recreates;
    snprintf(s_audio.counters.last_error_name,
             sizeof(s_audio.counters.last_error_name),
             "%s",
             "ESP_OK");
    audio_input_reset_raw_diagnostics();
}

esp_err_t audio_input_run_i2s_diag(uint32_t duration_ms, audio_input_i2s_diag_result_t *out)
{
    static int32_t diag_buf[SDACS_I2S_FRAMES_PER_READ];
    size_t samples_read = 0;
    int64_t start_us = 0;
    int64_t now_us = 0;
    int64_t last_success_us = 0;
    uint64_t gap_sum_us = 0;
    uint32_t gap_count = 0;
    esp_err_t ret = ESP_OK;
    bool diag_was_enabled = s_raw_diagnostics_enabled;

    if (!out || duration_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->duration_ms = duration_ms;
    out->min_gap_between_successful_reads_ms = UINT32_MAX;

    ret = audio_input_prepare_for_capture();
    if (ret != ESP_OK) {
        audio_input_note_error(ret);
        return ret;
    }

    audio_input_set_raw_diagnostics_enabled(false);
    audio_input_reset_counters();
    start_us = esp_timer_get_time();

    while ((esp_timer_get_time() - start_us) < ((int64_t)duration_ms * 1000LL)) {
        esp_err_t err = audio_input_read_s24(
            diag_buf,
            SDACS_I2S_FRAMES_PER_READ,
            &samples_read,
            SDACS_I2S_READ_TIMEOUT_MS
        );
        now_us = esp_timer_get_time();
        if (err == ESP_OK) {
            if (out->first_success_ms == 0U) {
                out->first_success_ms = (uint32_t)((now_us - start_us) / 1000LL);
            }
            if (last_success_us > 0) {
                uint32_t gap_ms = (uint32_t)((now_us - last_success_us) / 1000LL);
                uint64_t gap_us = (uint64_t)(now_us - last_success_us);
                if (gap_ms > out->max_gap_between_successful_reads_ms) {
                    out->max_gap_between_successful_reads_ms = gap_ms;
                }
                if (gap_ms < out->min_gap_between_successful_reads_ms) {
                    out->min_gap_between_successful_reads_ms = gap_ms;
                }
                gap_sum_us += gap_us;
                gap_count++;
            }
            last_success_us = now_us;
        } else if (err != ESP_ERR_TIMEOUT) {
            ret = err;
        }
    }

    audio_input_counters_t counters = {0};
    audio_input_get_counters(&counters);
    audio_input_get_last_debug(&out->debug);
    int64_t elapsed_us = esp_timer_get_time() - start_us;
    out->elapsed_ms = (uint32_t)(elapsed_us / 1000LL);
    out->read_calls = counters.total_read_calls;
    out->successful_reads = counters.successful_reads;
    out->timeout_reads = counters.timeouts;
    out->error_reads = counters.errors;
    out->bytes_per_successful_read = counters.successful_reads > 0U
        ? (uint32_t)(counters.total_bytes_read / counters.successful_reads)
        : 0U;
    out->selected_samples_per_successful_read = counters.successful_reads > 0U
        ? (uint32_t)(counters.total_selected_samples / counters.successful_reads)
        : 0U;
    out->total_bytes_read = counters.total_bytes_read;
    out->total_raw_words = counters.total_raw_words;
    out->total_selected_samples = counters.total_selected_samples;
    out->effective_selected_sample_rate_hz = elapsed_us > 0
        ? ((float)counters.total_selected_samples * 1000000.0f) / (float)elapsed_us
        : 0.0f;
    out->effective_raw_word_rate_hz = elapsed_us > 0
        ? ((float)counters.total_raw_words * 1000000.0f) / (float)elapsed_us
        : 0.0f;
    if (out->min_gap_between_successful_reads_ms == UINT32_MAX) {
        out->min_gap_between_successful_reads_ms = 0U;
    }
    out->avg_gap_between_successful_reads_ms = gap_count > 0U
        ? ((float)gap_sum_us / 1000.0f) / (float)gap_count
        : 0.0f;
    out->min_read_elapsed_us = counters.min_read_elapsed_us;
    out->max_read_elapsed_us = counters.max_read_elapsed_us;
    out->avg_read_elapsed_us = counters.total_read_calls > 0U
        ? (float)counters.total_read_elapsed_us / (float)counters.total_read_calls
        : 0.0f;
    out->min_timeout_elapsed_us = counters.min_timeout_elapsed_us;
    out->max_timeout_elapsed_us = counters.max_timeout_elapsed_us;
    out->avg_timeout_elapsed_us = counters.timeouts > 0U
        ? (float)counters.total_timeout_elapsed_us / (float)counters.timeouts
        : 0.0f;
    out->timeout_immediate_count = counters.timeout_immediate_count;
    strncpy(out->last_error_name, counters.last_error_name, sizeof(out->last_error_name) - 1);
    out->last_error_name[sizeof(out->last_error_name) - 1] = '\0';

    audio_input_set_raw_diagnostics_enabled(diag_was_enabled);
    audio_input_reset_raw_diagnostics();
    ESP_LOGI(TAG,
             "I2S diag summary: rx_mode=%s elapsed_ms=%u selected_samples=%" PRIu64 " raw_words=%" PRIu64 " eff_selected_sr=%.2f eff_raw_word_rate=%.2f reads=%u ok=%u timeouts=%u immediate_timeouts=%u errors=%u",
             SDACS_I2S_RX_MODE_LABEL,
             (unsigned)out->elapsed_ms,
             out->total_selected_samples,
             out->total_raw_words,
             (double)out->effective_selected_sample_rate_hz,
             (double)out->effective_raw_word_rate_hz,
             (unsigned)out->read_calls,
             (unsigned)out->successful_reads,
             (unsigned)out->timeout_reads,
             (unsigned)out->timeout_immediate_count,
             (unsigned)out->error_reads);
    return ret;
}

void audio_input_reset_raw_diagnostics(void)
{
    raw_diag_reset_internal();
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
    s_raw_diagnostics_enabled = true;
}
