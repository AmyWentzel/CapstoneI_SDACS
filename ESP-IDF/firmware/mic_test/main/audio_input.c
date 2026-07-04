#include "audio_input.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
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
    audio_input_counters_t counters;
    bool initialized;
} audio_input_t;

static const char *TAG = "audio_input";
static audio_input_t s_audio = {0};
static uint32_t s_i2s_debug_read_count = 0;

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

static inline int32_t i2s_word_to_s24(int32_t word)
{
    /*
     * ICS-43432 outputs 24-bit two's-complement audio inside a 32-bit I2S
     * slot. Many MEMS mics place valid 24-bit data left-justified, so
     * raw_word >> 8 is often the correct conversion. The raw diagnostics below
     * compare this production path against low-24 and upper-16 interpretations.
     */
    return sign_extend_24((uint32_t)word >> 8);
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
    int32_t shift8_sample = sign_extend_24(raw_word >> 8);
    int32_t low24_sample = sign_extend_24(raw_word & 0x00FFFFFFU);
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
#else
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
#endif
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx_chan, &std_cfg), TAG, "init std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.rx_chan), TAG, "enable failed");

    s_audio.initialized = true;
    raw_diag_reset_internal();
    ESP_LOGI(TAG,
             "I2S mic config: mic=%s format=philips frame=stereo selected_slot=%s sample_rate=%d data_bits=%d slot_bits=%d valid_bits=%d BCLK=%d WS=%d DIN=%d slot_mask=%s driver_rx=selected_slot_words",
             SDACS_MIC_MODEL,
             SDACS_I2S_SELECTED_SLOT_LABEL,
             SDACS_SAMPLE_RATE_HZ,
             32,
             SDACS_MIC_I2S_SLOT_BITS,
             SDACS_MIC_VALID_BITS,
             SDACS_I2S_BCLK_GPIO,
             SDACS_I2S_WS_GPIO,
             SDACS_I2S_DIN_GPIO,
             SDACS_I2S_SLOT_MASK_LABEL);
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
    s_audio.counters.total_read_calls++;
    if (err != ESP_OK) {
        *samples_read = 0;
        if (err == ESP_ERR_TIMEOUT) {
            s_audio.counters.timeouts++;
            if ((s_audio.counters.timeouts % 25U) == 0U) {
                ESP_LOGW(TAG,
                         "I2S read timeout count=%" PRIu32 " timeout_ms=%" PRIu32 " selected_slot=%s slot_mode=stereo BCLK=%d WS=%d DIN=%d",
                         s_audio.counters.timeouts,
                         timeout_ms,
                         SDACS_I2S_SELECTED_SLOT_LABEL,
                         SDACS_I2S_BCLK_GPIO,
                         SDACS_I2S_WS_GPIO,
                         SDACS_I2S_DIN_GPIO);
            }
        } else {
            s_audio.counters.errors++;
        }
        return err;
    }

    size_t raw_words_read = bytes_read / sizeof(int32_t);
    /*
     * ESP-IDF std I2S RX returns only words matching the configured single
     * slot_mask here; raw_words_read therefore equals selected mic samples.
     * If a future driver/config returns both stereo slots interleaved, this is
     * the point to deinterleave before incrementing selected_samples_read.
     */
    size_t selected_samples_read = raw_words_read;
    if (selected_samples_read > max_samples) {
        selected_samples_read = max_samples;
    }
    *samples_read = selected_samples_read;
    s_audio.counters.successful_reads++;
    s_audio.counters.total_bytes_read += bytes_read;
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
        uint32_t raw_word = (uint32_t)s_audio.raw[i];
        int32_t sample = i2s_word_to_s24(s_audio.raw[i]);
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
        .raw0 = (selected_samples_read > 0) ? (uint32_t)s_audio.raw[0] : 0U,
        .raw1 = (selected_samples_read > 1) ? (uint32_t)s_audio.raw[1] : 0U,
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

void audio_input_get_counters(audio_input_counters_t *out)
{
    if (!out) {
        return;
    }

    *out = s_audio.counters;
}

void audio_input_reset_counters(void)
{
    memset(&s_audio.counters, 0, sizeof(s_audio.counters));
    audio_input_reset_raw_diagnostics();
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
}
