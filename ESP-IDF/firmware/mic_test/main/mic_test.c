/*
    mic_test - ESP-IDF capture task for ICS-43434

    This version does both:
    1) Streams audio chunks over MQTT
    2) Logs run artifacts to SD card (raw, wav, metrics, calibration files)
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"

#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "config_store.h"
#include "wifi_mqtt.h"
#include "temp_humidity.h"

/* ================= USER CONFIG ================= */

#define I2S_BCLK_GPIO   GPIO_NUM_14
#define I2S_WS_GPIO     GPIO_NUM_15
#define I2S_DIN_GPIO    GPIO_NUM_16

#define SAMPLE_RATE_HZ        48000
#define I2S_FRAMES_PER_READ   512
#define RECORD_SECONDS        20
#define AUDIO_CHUNK_SAMPLES   2048
#define CAL_OFFSET_DB         94.0f
#define I2S_READ_TIMEOUT_MS   100

#define NODE_ID               "node01"
#define SD_MOUNT_POINT        "/sdcard"

#define PROVISION_WIFI_SSID   "Cheerios & Shreddies"
#define PROVISION_WIFI_PASS   "JesusisLord"
#define PROVISION_MQTT_URI    "mqtt://172.20.10.5:1883"
#define PROVISION_MQTT_TOPIC  "sdacs/node/node01/features"
#define PROVISION_ALWAYS_SYNC_MQTT 1

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;
static int32_t raw[I2S_FRAMES_PER_READ];
static sdmmc_card_t *g_card = NULL;

static char g_run_dir[160] = {0};
static char g_raw_path[256] = {0};
static char g_wav_path[256] = {0};
static char g_csv_path[256] = {0};
static char g_cal_csv_path[256] = {0};
static char g_cal_offset_path[256] = {0};

#define WAV_CHUNK_SIZE 1024

typedef struct __attribute__((packed)) {
    uint32_t magic;       // 'S''D''A''C'
    uint16_t ver;         // 1
    uint16_t flags;       // 0 for now
    uint32_t seq;         // chunk counter
    uint64_t t_us;        // esp_timer_get_time() when chunk completed
    uint32_t sample_rate; // 48000
    uint32_t n;           // samples in this chunk
} sdacs_audio_hdr_t;

typedef struct __attribute__((packed)) {
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

#define SDACS_MAGIC 0x43414453u  // 'SDAC' little-endian

static void get_iso8601_now(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(out, out_len, "%Y-%m-%dT%H:%M:%S", &timeinfo);
}

static bool sd_append_file(const char *fullpath, const void *buf, size_t len)
{
    FILE *f = fopen(fullpath, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", fullpath);
        return false;
    }

    size_t written = fwrite(buf, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    return (written == len);
}

static void append_metrics_csv(float laeq_db,
                               float peak_db,
                               float temp_c,
                               float humidity,
                               float fft_peak_hz)
{
    char ts[32];
    get_iso8601_now(ts, sizeof(ts));

    FILE *f = fopen(g_csv_path, "a");
    if (f) {
        fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.2f,%.1f\n",
                ts, NODE_ID, laeq_db, peak_db, temp_c, humidity, fft_peak_hz);
        fclose(f);
    } else {
        ESP_LOGW(TAG, "Could not append metrics.csv");
    }

    // Duplicate into a calibration CSV for traceability.
    f = fopen(g_cal_csv_path, "a");
    if (f) {
        fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.2f,%.1f\n",
                ts, NODE_ID, laeq_db, peak_db, temp_c, humidity, fft_peak_hz);
        fclose(f);
    }
}

static void log_file_stat(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "Missing file: %s", path);
        return;
    }
    ESP_LOGI(TAG, "File present: %s (%ld bytes)", path, (long)st.st_size);
}

static void log_dir_listing(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (!d) {
        ESP_LOGW(TAG, "Could not open directory: %s", dir_path);
        return;
    }

    ESP_LOGI(TAG, "Directory listing for %s:", dir_path);
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        ESP_LOGI(TAG, "  %s", ent->d_name);
    }
    closedir(d);
}

static void log_file_crc32(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Could not open for CRC32: %s", path);
        return;
    }

    uint8_t buf[256];
    uint32_t crc = 0;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = esp_rom_crc32_le(crc, buf, n);
    }
    fclose(f);

    ESP_LOGI(TAG, "CRC32 %s = 0x%08" PRIX32, path, crc);
}

static void verify_run_outputs(void)
{
    log_file_stat(g_raw_path);
    log_file_stat(g_csv_path);
    log_file_stat(g_cal_csv_path);
    log_file_stat(g_cal_offset_path);
    log_file_stat(g_wav_path);
    log_dir_listing(g_run_dir);
    log_file_crc32(g_raw_path);
    log_file_crc32(g_csv_path);
    log_file_crc32(g_wav_path);
}

static esp_err_t sd_card_init(void)
{
    esp_err_t ret;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_NUM_42,
        .miso_io_num = GPIO_NUM_21,
        .sclk_io_num = GPIO_NUM_39,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = GPIO_NUM_45;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &g_card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, g_card);
    return ESP_OK;
}

static esp_err_t create_run_directory(void)
{
    time_t now = time(NULL);
    char ts[32];
    if (now > 1700000000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &timeinfo);
    } else {
        // RTC not set yet; use boot-relative monotonic time for uniqueness.
        int64_t boot_ms = esp_timer_get_time() / 1000;
        snprintf(ts, sizeof(ts), "boot_%lld", (long long)boot_ms);
    }

    bool dir_created = false;
    for (int suffix = 0; suffix < 100; ++suffix) {
        int n = 0;
        if (suffix == 0) {
            n = snprintf(g_run_dir, sizeof(g_run_dir), "%s/%s_%s", SD_MOUNT_POINT, NODE_ID, ts);
        } else {
            n = snprintf(g_run_dir, sizeof(g_run_dir), "%s/%s_%s_%02d", SD_MOUNT_POINT, NODE_ID, ts, suffix);
        }

        if (n < 0 || n >= (int)sizeof(g_run_dir)) {
            ESP_LOGE(TAG, "Run directory path overflow");
            return ESP_ERR_INVALID_SIZE;
        }

        if (mkdir(g_run_dir, 0775) == 0) {
            dir_created = true;
            break;
        }

        if (errno == EEXIST) {
            continue;
        }

        ESP_LOGE(TAG, "Failed to create run directory: %s (errno=%d: %s)",
                 g_run_dir, errno, strerror(errno));
        return ESP_FAIL;
    }

    if (!dir_created) {
        ESP_LOGE(TAG, "Could not find unique run directory name for base '%s'", ts);
        return ESP_FAIL;
    }

    if (snprintf(g_raw_path, sizeof(g_raw_path), "%s/audio.raw", g_run_dir) >= (int)sizeof(g_raw_path) ||
        snprintf(g_wav_path, sizeof(g_wav_path), "%s/audio_%s.wav", g_run_dir, ts) >= (int)sizeof(g_wav_path) ||
        snprintf(g_csv_path, sizeof(g_csv_path), "%s/metrics_%s.csv", g_run_dir, ts) >= (int)sizeof(g_csv_path) ||
        snprintf(g_cal_csv_path, sizeof(g_cal_csv_path), "%s/calibration_run1.csv", g_run_dir) >= (int)sizeof(g_cal_csv_path) ||
        snprintf(g_cal_offset_path, sizeof(g_cal_offset_path), "%s/calibration_offset.txt", g_run_dir) >= (int)sizeof(g_cal_offset_path)) {
        ESP_LOGE(TAG, "Run path generation overflow");
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *csv = fopen(g_csv_path, "w");
    if (!csv) {
        ESP_LOGE(TAG, "Failed to create metrics CSV");
        return ESP_FAIL;
    }
    fprintf(csv, "timestamp,node_id,LAeq_dB,peak_dB,temp_C,humidity,fft_peak_Hz\n");
    fclose(csv);

    FILE *cal_csv = fopen(g_cal_csv_path, "w");
    if (!cal_csv) {
        ESP_LOGE(TAG, "Failed to create calibration_run1.csv");
        return ESP_FAIL;
    }
    fprintf(cal_csv, "timestamp,node_id,LAeq_dB,peak_dB,temp_C,humidity,fft_peak_Hz\n");
    fclose(cal_csv);

    FILE *cal_txt = fopen(g_cal_offset_path, "w");
    if (!cal_txt) {
        ESP_LOGE(TAG, "Failed to create calibration_offset.txt");
        return ESP_FAIL;
    }
    fprintf(cal_txt, "node_id=%s\n", NODE_ID);
    fprintf(cal_txt, "sample_rate_hz=%d\n", SAMPLE_RATE_HZ);
    fprintf(cal_txt, "record_seconds=%d\n", RECORD_SECONDS);
    fprintf(cal_txt, "i2s_bclk_gpio=%d\n", I2S_BCLK_GPIO);
    fprintf(cal_txt, "i2s_ws_gpio=%d\n", I2S_WS_GPIO);
    fprintf(cal_txt, "i2s_din_gpio=%d\n", I2S_DIN_GPIO);
    fprintf(cal_txt, "cal_signal=TO_FILL_IN\n");
    fprintf(cal_txt, "reference_meter_db=TO_FILL_IN\n");
    fprintf(cal_txt, "offset_db=TO_FILL_IN\n");
    fclose(cal_txt);

    ESP_LOGI(TAG, "Created run folder: %s", g_run_dir);
    return ESP_OK;
}

static void maybe_provision_network_config(void)
{
    const char *ssid = NULL;
    const char *pass = NULL;
    const char *broker_uri = NULL;
    const char *mqtt_topic = NULL;
    esp_err_t err = config_store_get_wifi(&ssid, &pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_wifi failed: %s", esp_err_to_name(err));
        return;
    }

    if (strcmp(PROVISION_WIFI_SSID, "YOUR_WIFI_SSID") == 0 ||
        strcmp(PROVISION_WIFI_PASS, "YOUR_WIFI_PASSWORD") == 0) {
        ESP_LOGW(TAG, "Provisioning skipped: set PROVISION_WIFI_SSID/PROVISION_WIFI_PASS in mic_test.c");
        return;
    }

    if (!ssid || ssid[0] == '\0') {
        ESP_ERROR_CHECK(config_store_set_wifi(PROVISION_WIFI_SSID, PROVISION_WIFI_PASS));
        ESP_LOGI(TAG, "Provisioned WiFi defaults into NVS (one-time).");
    } else {
        ESP_LOGI(TAG, "WiFi already provisioned; keeping existing SSID.");
    }

    err = config_store_get_mqtt(&broker_uri, &mqtt_topic);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_store_get_mqtt failed: %s", esp_err_to_name(err));
        return;
    }

#if PROVISION_ALWAYS_SYNC_MQTT
    if (!broker_uri || !mqtt_topic ||
        strcmp(broker_uri, PROVISION_MQTT_URI) != 0 ||
        strcmp(mqtt_topic, PROVISION_MQTT_TOPIC) != 0) {
        ESP_ERROR_CHECK(config_store_set_mqtt(PROVISION_MQTT_URI, PROVISION_MQTT_TOPIC));
        broker_uri = PROVISION_MQTT_URI;
        mqtt_topic = PROVISION_MQTT_TOPIC;
        ESP_LOGW(TAG, "Synced MQTT settings in NVS to firmware defaults.");
    }
#endif

    ESP_LOGI(TAG, "Active MQTT config: broker=%s topic=%s",
             broker_uri ? broker_uri : "(null)",
             mqtt_topic ? mqtt_topic : "(null)");
}

static inline int32_t i2s_word_to_s24(int32_t w)
{
    int32_t s = (int32_t)((uint32_t)w >> 8);
    if (s & 0x00800000) {
        s |= ~0x00FFFFFF;
    }
    return s;
}

static void i2s_mic_init(void)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN_GPIO,
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    ESP_LOGI(TAG, "I2S initialized: BCLK=%d WS=%d DIN=%d SR=%d",
             I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DIN_GPIO, SAMPLE_RATE_HZ);
}

static void convert_raw_to_wav(void)
{
    FILE *raw_file = fopen(g_raw_path, "rb");
    if (!raw_file) {
        ESP_LOGE(TAG, "Failed to open raw file %s", g_raw_path);
        return;
    }

    fseek(raw_file, 0, SEEK_END);
    long raw_size_l = ftell(raw_file);
    fseek(raw_file, 0, SEEK_SET);
    if (raw_size_l <= 0) {
        ESP_LOGE(TAG, "Raw file is empty");
        fclose(raw_file);
        return;
    }
    size_t raw_size = (size_t)raw_size_l;

    FILE *wav_file = fopen(g_wav_path, "wb");
    if (!wav_file) {
        ESP_LOGE(TAG, "Failed to create WAV file %s", g_wav_path);
        fclose(raw_file);
        return;
    }

    size_t num_samples = raw_size / sizeof(int32_t);
    wav_header_t header;
    memcpy(header.riff, "RIFF", 4);
    header.file_size = 36 + (uint32_t)(num_samples * 3);
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
    header.data_size = (uint32_t)(num_samples * 3);

    fwrite(&header, sizeof(wav_header_t), 1, wav_file);

    int32_t chunk_buf[WAV_CHUNK_SIZE];
    size_t samples_left = num_samples;
    while (samples_left > 0) {
        size_t to_read = (samples_left > WAV_CHUNK_SIZE) ? WAV_CHUNK_SIZE : samples_left;
        size_t rd = fread(chunk_buf, sizeof(int32_t), to_read, raw_file);
        if (rd == 0) {
            break;
        }
        for (size_t i = 0; i < rd; ++i) {
            int32_t sample = chunk_buf[i];
            uint8_t bytes[3];
            bytes[0] = (uint8_t)(sample & 0xFF);
            bytes[1] = (uint8_t)((sample >> 8) & 0xFF);
            bytes[2] = (uint8_t)((sample >> 16) & 0xFF);
            fwrite(bytes, 1, 3, wav_file);
        }
        samples_left -= rd;
    }

    fclose(raw_file);
    fflush(wav_file);
    fsync(fileno(wav_file));
    fclose(wav_file);

    ESP_LOGI(TAG, "WAV file created: %s", g_wav_path);
}

static void mic_test_task(void *arg)
{
    (void)arg;

    const char *base_topic = NULL;
    const char *broker = NULL;
    ESP_ERROR_CHECK(config_store_get_mqtt(&broker, &base_topic));
    (void)broker;
    if (!base_topic || base_topic[0] == '\0') {
        ESP_LOGE(TAG, "MQTT base topic is empty.");
        vTaskDelete(NULL);
        return;
    }

    char audio_topic[CONFIG_STORE_MAX_MQTT_TOPIC_LEN + 16];
    int topic_len = snprintf(audio_topic, sizeof(audio_topic), "%s/audio", base_topic);
    if (topic_len <= 0 || topic_len >= (int)sizeof(audio_topic)) {
        ESP_LOGE(TAG, "Audio topic too long; base topic='%s'", base_topic ? base_topic : "(null)");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for WiFi+MQTT before streaming...");
    esp_err_t werr = wifi_mqtt_wait_connected(15000);
    if (werr != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not ready (%s). Continuing with local SD logging.", esp_err_to_name(werr));
    } else {
        ESP_LOGI(TAG, "WiFi+MQTT ready. Starting stream + SD logging.");
    }
    if (werr == ESP_OK) {
        (void)temp_humidity_publish_latest_once("start");
    }

    size_t bytes_read = 0;
    int64_t start_us = esp_timer_get_time();
    int64_t end_us = start_us + ((int64_t)RECORD_SECONDS * 1000000LL);
    int64_t next_metrics_us = start_us + 1000000LL;
    uint32_t samples_streamed = 0;

    double sum_sq = 0.0;
    int32_t peak_abs = 0;
    uint32_t count = 0;

    sdacs_audio_hdr_t hdr = {
        .magic = SDACS_MAGIC,
        .ver = 1,
        .flags = 0,
        .seq = 0,
        .sample_rate = SAMPLE_RATE_HZ,
    };

    static int32_t chunk[AUDIO_CHUNK_SAMPLES];
    static uint8_t payload[sizeof(sdacs_audio_hdr_t) + (AUDIO_CHUNK_SAMPLES * sizeof(int32_t))];
    size_t chunk_fill = 0;

    while (esp_timer_get_time() < end_us) {
        esp_err_t err = i2s_channel_read(
            rx_chan,
            raw,
            sizeof(raw),
            &bytes_read,
            pdMS_TO_TICKS(I2S_READ_TIMEOUT_MS)
        );
        if (err != ESP_OK) {
            if (err == ESP_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(err));
            continue;
        }

        int n_raw = (int)(bytes_read / sizeof(int32_t));
        if (n_raw <= 0) {
            continue;
        }

        for (int i = 0; i < n_raw; i++) {
            int32_t s24 = i2s_word_to_s24(raw[i]);
            chunk[chunk_fill++] = s24;

            int32_t a = (s24 < 0) ? -s24 : s24;
            if (a > peak_abs) {
                peak_abs = a;
            }
            sum_sq += (double)s24 * (double)s24;
            count++;

            if (chunk_fill >= AUDIO_CHUNK_SAMPLES) {
                // 1) Primary local logging: append raw chunk to SD.
                if (!sd_append_file(g_raw_path, chunk, chunk_fill * sizeof(int32_t))) {
                    ESP_LOGW(TAG, "Failed SD append for raw chunk");
                }

                // Keep MQTT chunk streaming behavior.
                hdr.n = (uint32_t)chunk_fill;
                hdr.t_us = (uint64_t)esp_timer_get_time();
                memcpy(payload, &hdr, sizeof(hdr));
                memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));
                esp_err_t perr = wifi_mqtt_publish_raw(
                    audio_topic,
                    payload,
                    sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
                    0,
                    0
                );
                if (perr != ESP_OK && (hdr.seq % 20u == 0u)) {
                    ESP_LOGW(TAG, "Audio chunk publish dropped: %s", esp_err_to_name(perr));
                }

                samples_streamed += (uint32_t)chunk_fill;
                hdr.seq++;
                chunk_fill = 0;
            }
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_metrics_us && count > 0) {
            float rms = sqrtf((float)(sum_sq / (double)count));
            float rms_norm = rms / 8388608.0f;
            float laeq_db = 20.0f * log10f(rms_norm + 1e-12f) + CAL_OFFSET_DB;
            float peak_norm = (float)peak_abs / 8388608.0f;
            float peak_db = 20.0f * log10f(peak_norm + 1e-12f) + CAL_OFFSET_DB;

            float temp_c = NAN;
            float humidity = NAN;
            temp_humidity_reading_t th = {0};
            if (temp_humidity_get_latest(&th)) {
                temp_c = th.temp_c;
                humidity = th.rh_percent;
            }

            float fft_peak_hz = 0.0f;
            append_metrics_csv(laeq_db, peak_db, temp_c, humidity, fft_peak_hz);

            ESP_LOGI(TAG, "LAeq=%.2f dB peak=%.2f dB streamed=%u",
                     laeq_db, peak_db, (unsigned)samples_streamed);

            next_metrics_us += 1000000LL;
            sum_sq = 0.0;
            peak_abs = 0;
            count = 0;
        }
    }

    if (chunk_fill > 0) {
        if (!sd_append_file(g_raw_path, chunk, chunk_fill * sizeof(int32_t))) {
            ESP_LOGW(TAG, "Failed final SD append");
        }

        hdr.n = (uint32_t)chunk_fill;
        hdr.t_us = (uint64_t)esp_timer_get_time();
        memcpy(payload, &hdr, sizeof(hdr));
        memcpy(payload + sizeof(hdr), chunk, chunk_fill * sizeof(int32_t));
        (void)wifi_mqtt_publish_raw(
            audio_topic,
            payload,
            sizeof(hdr) + (chunk_fill * sizeof(int32_t)),
            0,
            0
        );
        hdr.seq++;
    }

    ESP_ERROR_CHECK(i2s_channel_disable(rx_chan));

    // Stop temp/humidity sampling as soon as capture ends.
    temp_humidity_stop();

    // 2) Algorithm validation artifact: generate WAV from raw.
    convert_raw_to_wav();

    ESP_LOGI(TAG, "Recording complete: %.2f s, chunk_msgs=%u",
             (float)(esp_timer_get_time() - start_us) / 1000000.0f,
             (unsigned)hdr.seq);

    if (wifi_mqtt_is_connected()) {
        (void)temp_humidity_publish_latest_once("end");
    }

    // On-device verification without removing SD card.
    verify_run_outputs();

    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());
    maybe_provision_network_config();

    if (sd_card_init() != ESP_OK) {
        ESP_LOGE(TAG, "SD initialization failed");
        return;
    }
    if (create_run_directory() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create run folder");
        return;
    }

    ESP_ERROR_CHECK(wifi_mqtt_start(NULL));

    bool th_ok = temp_humidity_start(
        0,       // I2C_NUM_0
        47,      // SDA
        48,      // SCL
        100000,  // 100kHz
        0x44,    // HDC302x default address
        2000     // sample period ms
    );
    if (!th_ok) {
        ESP_LOGW(TAG, "temp_humidity_start failed; metrics will show NAN for temp/humidity");
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

    ESP_LOGI(TAG, "Capture started (%d s): MQTT stream + SD logging", RECORD_SECONDS);
}
