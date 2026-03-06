/*
    mic_test - ESP-IDF bring-up test for ICS-43434 I2S microphone
    ------------------------------------------------------------
    Target: ESP32-S3 (Freenove ESP32-S3 WROOM)
    Framework: ESP-IDF v5.x
    Language: C (not Arduino, not C++)

    PURPOSE:
      - Verify I2S clocking and data wiring
      - Confirm microphone is producing real audio samples
      - Record a fixed capture window to RAM for later processing

    WIRING (ICS-43434 → ESP32-S3):
      3V     -> 3.3V
      GND    -> GND
      SEL    -> GND        (LEFT channel)
      BCLK   -> GPIO6
      LRCLK  -> GPIO5
      DOUT   -> GPIO4
*/

#include <stdio.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

/* SD card (FAT) support */
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include "esp_vfs_fat.h"
#include <string.h>
#include "mqtt_client.h"

/* ================= USER CONFIG ================= */

// I2S pin mapping (safe pins for ESP32-S3)
#define I2S_BCLK_GPIO   GPIO_NUM_6
#define I2S_WS_GPIO     GPIO_NUM_5
#define I2S_DIN_GPIO    GPIO_NUM_4

// Audio sample rate
#define SAMPLE_RATE_HZ  48000

// I2S read size
#define I2S_FRAMES_PER_READ  512

// One-shot recording duration in seconds.
#define RECORD_SECONDS 20

/* ============================================== */

static const char *TAG = "MIC_TEST";

/* mount point for FAT filesystem */
static const char *SD_MOUNT_POINT = "/sdcard";

/*
 * Initialize the SD card and mount FAT filesystem at SD_MOUNT_POINT.
 * Returns ESP_OK on success, error code otherwise.
 */
static esp_err_t sd_card_init(void)
{
    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Use SPI mode for SD card
#include "driver/sdspi_host.h"
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_5; // adjust CS pin as needed

    /* mount configuration: allow format on failure and max files */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

/*
 * Write raw bytes to a file on the SD card at a given byte offset.
 * The file is created if it does not exist.  Returns true on success.
 */
static bool sd_write_raw(const char *path, uint32_t addr, const void *buf, size_t len)
{
    char fullpath[64];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", SD_MOUNT_POINT, path);
    FILE *f = fopen(fullpath, "r+b");
    if (!f) {
        /* create file if it doesn't exist */
        f = fopen(fullpath, "w+b");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open %s", fullpath);
            return false;
        }
    }
    if (fseek(f, addr, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "fseek failed");
        fclose(f);
        return false;
    }
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    return (written == len);
}

static i2s_chan_handle_t rx_chan = NULL;

#define WAV_CHUNK_SIZE 512

static int32_t raw[I2S_FRAMES_PER_READ];

/* SD card audio address tracking (byte offsets) */
static uint32_t g_audio_start_addr = UINT32_MAX;
static uint32_t g_audio_end_addr = 0;
static uint32_t g_audio_write_ptr = 0;    // current write offset in file

/*
    Convert a 32-bit I2S slot word into a signed 24-bit sample.

    The ICS-43434 outputs 24-bit two's-complement audio.
    Depending on I2S packing, the valid sample may appear in the
    lower 24 bits of the 32-bit slot word.
*/
static inline int32_t i2s_word_to_s24(int32_t w)
{
    // Keep only 24 payload bits.
    int32_t s = w & 0x00FFFFFF;
    // If bit23 is set, sign-extend to 32-bit.
    if (s & 0x00800000) {
        s |= ~0x00FFFFFF;
    }
    return s;
}

/* ------------------------------------------------------------
   I2S initialization
------------------------------------------------------------ */
static void i2s_mic_init(void)
{
    // Create an I2S channel on controller 0 in MASTER mode.
    // MASTER means the ESP32 generates BCLK and WS.
    // We request only RX because this test is mic input only.
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    // Allocate channel resources and return an RX handle in rx_chan.
    // TX handle is NULL because this application does not transmit audio.
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    // Configure standard I2S (MSB/Philips style) timing and pin routing.
    i2s_std_config_t std_cfg = {
        // Derive clock configuration for the requested sampling rate.
        // The driver sets WS to SAMPLE_RATE_HZ and BCLK accordingly.
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),

        // Slot format on the wire:
        // - 32-bit slot width (container per channel slot)
        // - MONO mode (single logical channel for analysis)
        // The ICS-43434 produces 24-bit PCM values packed into this 32-bit slot.
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),

        // Map I2S signals to ESP32 pins.
        .gpio_cfg = {
            // Not used for this microphone setup.
            .mclk = I2S_GPIO_UNUSED,
            // Bit clock output from ESP32 to microphone.
            .bclk = I2S_BCLK_GPIO,
            // Word-select (LRCLK/WS) output from ESP32 to microphone.
            .ws   = I2S_WS_GPIO,
            // TX data line unused because this is RX-only.
            .dout = I2S_GPIO_UNUSED,
            // RX data line from microphone to ESP32.
            .din  = I2S_DIN_GPIO,
        },
    };

    // ICS-43434 with SEL=GND transmits on LEFT WS phase.
    // Restrict slot mask to LEFT so we don't ingest the inactive phase as data.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // Apply configuration to hardware channel.
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));

    // Enable channel + DMA engine so reads can start returning samples.
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    // Startup summary line for quick wiring/config sanity check in monitor logs.
    ESP_LOGI(TAG,
        "I2S initialized: BCLK=%d WS=%d DIN=%d SR=%d",
        I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, SAMPLE_RATE_HZ);
}

/* ------------------------------------------------------------
   Microphone test task
------------------------------------------------------------ */
// Dummy MQTT symbols for build (replace with real values/initialization as needed)
#define MQTT_TOPIC_AUDIO "audio"
#define NODE_ID "Node01"
void *mqtt_client = NULL;

static void mic_test_task(void *arg)
{
    size_t bytes_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)RECORD_SECONDS * 1000000LL);
    int64_t next_progress_us = start_us + 1000000LL;

    int32_t write_buf[I2S_FRAMES_PER_READ/2];

    while (esp_timer_get_time() < end_us) {
        esp_err_t err = i2s_channel_read(
            rx_chan,
            raw,
            sizeof(raw),
            &bytes_read,
            portMAX_DELAY
        );
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }
        int n_raw = bytes_read / sizeof(int32_t);
        if (n_raw <= 0) continue;
        int write_count = 0;
        for (int i = 0; i < n_raw; i += 2) {
            int32_t s24 = i2s_word_to_s24(raw[i]);
            write_buf[write_count++] = s24;
        }
        if (write_count > 0) {
            if (g_audio_start_addr == UINT32_MAX) {
                g_audio_start_addr = g_audio_write_ptr;
            }
            size_t bytes = write_count * sizeof(int32_t);
            if (sd_write_raw("audio.bin", g_audio_write_ptr, write_buf, bytes)) {
                g_audio_write_ptr += bytes;
            } else {
                ESP_LOGW(TAG, "SD write failed at offset %lu", (unsigned long)g_audio_write_ptr);
            }
            // Stream chunk over MQTT
            if (mqtt_client != NULL) {
                char topic[64];
                snprintf(topic, sizeof(topic), "%s/%s", MQTT_TOPIC_AUDIO, NODE_ID);
                int msg_id = esp_mqtt_client_publish(mqtt_client, topic, (const char*)write_buf, bytes, 0, 0);
                if (msg_id < 0) {
                    ESP_LOGW(TAG, "MQTT publish failed");
                }
            }
        }
        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_progress_us) {
            next_progress_us += 1000000LL;
            ESP_LOGI(TAG, "Recording... %lu bytes written", (unsigned long)g_audio_write_ptr);
        }
    }
    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));
    if (g_audio_start_addr != UINT32_MAX) {
        g_audio_end_addr = g_audio_write_ptr;
    }
    ESP_LOGI(TAG, "Recording complete: %.2f s, bytes=%lu",
        (float)(esp_timer_get_time() - start_us) / 1000000.0f,
        (unsigned long)g_audio_write_ptr);
    if (g_audio_start_addr != UINT32_MAX) {
        ESP_LOGI(TAG, "Audio data written to SD from %lu to %lu bytes",
            (unsigned long)g_audio_start_addr, (unsigned long)g_audio_end_addr);
    }
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------
   app_main
------------------------------------------------------------ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    /* initialize SD card so raw write helper will work */
    if (sd_card_init() != ESP_OK) {
        ESP_LOGW(TAG, "SD initialization failed, continuing without storage");
    }

    i2s_mic_init();

    xTaskCreatePinnedToCore(
        mic_test_task,
        "mic_test",
        8192,
        NULL,
        5,
        NULL,
        1
    );

    ESP_LOGI(TAG, "ICS-43434 one-shot capture started (%d s).", RECORD_SECONDS);

    /* example usage of sd_write_raw (write zeros at offset 0) */
    uint8_t zeros[512] = {0};
    if (sd_write_raw("data.bin", 0, zeros, sizeof(zeros))) {
        ESP_LOGI(TAG, "Wrote 512 bytes of zeros to data.bin");
    }
}

// WAV file header structure
typedef struct {
    char riff[4];
    uint32_t file_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} wav_header_t;

void convert_raw_to_wav(const char *raw_filename, const char *node_id) {
    // Get current time for filename
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char date_time[20];
    strftime(date_time, sizeof(date_time), "%Y%m%d_%H%M%S", &timeinfo);
    char wav_filename[64];
    snprintf(wav_filename, sizeof(wav_filename), "%s_%s.wav", node_id, date_time);

    char raw_path[128];
    snprintf(raw_path, sizeof(raw_path), "%s/%s", SD_MOUNT_POINT, raw_filename);
    char wav_path[128];
    snprintf(wav_path, sizeof(wav_path), "%s/%s", SD_MOUNT_POINT, wav_filename);

    FILE *raw_file = fopen(raw_path, "rb");
    if (!raw_file) {
        ESP_LOGE(TAG, "Failed to open raw file %s", raw_path);
        return;
    }
    fseek(raw_file, 0, SEEK_END);
    size_t raw_size = ftell(raw_file);
    fseek(raw_file, 0, SEEK_SET);
    if (raw_size == 0) {
        ESP_LOGE(TAG, "Raw file is empty");
        fclose(raw_file);
        return;
    }

    FILE *wav_file = fopen(wav_path, "wb");
    if (!wav_file) {
        ESP_LOGE(TAG, "Failed to create WAV file %s", wav_path);
        fclose(raw_file);
        return;
    }

    // Prepare WAV header (24-bit mono PCM)
    size_t num_samples = raw_size / sizeof(int32_t);
    wav_header_t header;
    memcpy(header.riff, "RIFF", 4);
    header.file_size = 36 + num_samples * 3;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.format = 1;
    header.channels = 1;
    header.sample_rate = SAMPLE_RATE_HZ;
    header.byte_rate = SAMPLE_RATE_HZ * 3;
    header.block_align = 3;
    header.bits_per_sample = 24;
    memcpy(header.data, "data", 4);
    header.data_size = num_samples * 3;

    fwrite(&header, sizeof(wav_header_t), 1, wav_file);

    // Convert and write in chunks
    int32_t chunk_buf[WAV_CHUNK_SIZE];
    size_t samples_left = num_samples;
    while (samples_left > 0) {
        size_t to_read = (samples_left > WAV_CHUNK_SIZE) ? WAV_CHUNK_SIZE : samples_left;
        size_t read = fread(chunk_buf, sizeof(int32_t), to_read, raw_file);
        if (read == 0) break;
        for (size_t i = 0; i < read; ++i) {
            int32_t sample = chunk_buf[i];
            uint8_t bytes[3];
            bytes[0] = sample & 0xFF;
            bytes[1] = (sample >> 8) & 0xFF;
            bytes[2] = (sample >> 16) & 0xFF;
            fwrite(bytes, 1, 3, wav_file);
        }
        samples_left -= read;
    }
    fclose(raw_file);
    fclose(wav_file);
    ESP_LOGI(TAG, "WAV file created: %s", wav_filename);
}

