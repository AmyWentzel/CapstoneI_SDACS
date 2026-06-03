#include "sdCard.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "esp_timer.h"

#include "network.h"
#include "sdacs_config.h"

static const char *TAG = "sdCard";
static const char *TAG_RUN = "run_storage";
static char s_run_dir[128];
static char s_raw_path[256];
static char s_wav_path[256];
static char s_metrics_path[256];
static FILE *s_raw_file;
static bool s_mounted;

bool sdCard_init(void)
{
    if (s_mounted) {
        return true;
    }

    esp_err_t err;
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDACS_SD_MOSI_GPIO,
        .miso_io_num = SDACS_SD_MISO_GPIO,
        .sclk_io_num = SDACS_SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDACS_SD_CS_GPIO;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount(SDACS_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(err));
        return false;
    }

    s_mounted = true;
    return true;
}

bool sdCard_create_session(void)
{
    if (!sdCard_init()) {
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_info;
    char date_prefix[32];
    if (gmtime_r(&now, &tm_info) != NULL) {
        strftime(date_prefix, sizeof(date_prefix), "%Y%m%d", &tm_info);
    } else {
        int64_t boot_ms = esp_timer_get_time() / 1000;
        snprintf(date_prefix, sizeof(date_prefix), "boot_%lld", (long long)boot_ms);
    }

    int next_index = 1;
    DIR *dir = opendir(SDACS_SD_MOUNT_POINT);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, date_prefix, strlen(date_prefix)) == 0 && entry->d_name[strlen(date_prefix)] == '_') {
                int candidate = atoi(entry->d_name + strlen(date_prefix) + 1);
                if (candidate >= next_index) {
                    next_index = candidate + 1;
                }
            }
        }
        closedir(dir);
    }

    for (;;) {
        snprintf(s_run_dir, sizeof(s_run_dir), SDACS_SD_MOUNT_POINT "/%s_%02d", date_prefix, next_index);
        if (mkdir(s_run_dir, 0777) == 0) {
            break;
        }
        if (errno == EEXIST) {
            next_index++;
            continue;
        }
        ESP_LOGE(TAG, "Unable to create run directory %s: %s", s_run_dir, strerror(errno));
        return false;
    }

    snprintf(s_raw_path, sizeof(s_raw_path), "%s/audio.raw", s_run_dir);
    snprintf(s_wav_path, sizeof(s_wav_path), "%s/audio.wav", s_run_dir);
    snprintf(s_metrics_path, sizeof(s_metrics_path), "%s/metrics.csv", s_run_dir);
    return true;
}

bool sdCard_open_raw(void)
{
    if (s_raw_file != NULL) {
        return true;
    }

    s_raw_file = fopen(s_raw_path, "wb");
    if (s_raw_file == NULL) {
        ESP_LOGE(TAG, "Failed to open raw audio file %s", s_raw_path);
        return false;
    }
    return true;
}

bool sdCard_append_raw(const void *data, size_t len)
{
    if (s_raw_file == NULL || data == NULL || len == 0) {
        return false;
    }

    size_t written = fwrite(data, 1, len, s_raw_file);
    return written == len;
}

bool sdCard_close_raw(void)
{
    if (s_raw_file == NULL) {
        return true;
    }

    int result = fflush(s_raw_file);
    if (result != 0) {
        ESP_LOGW(TAG, "Failed to flush raw audio file");
    }
    result = fclose(s_raw_file);
    s_raw_file = NULL;
    return result == 0;
}

static bool write_wav_header(FILE *out, uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels, uint32_t data_size)
{
    if (out == NULL) {
        return false;
    }

    uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align = channels * (bits_per_sample / 8);
    uint32_t chunk_size = 36 + data_size;

    fwrite("RIFF", 1, 4, out);
    fwrite(&chunk_size, sizeof(chunk_size), 1, out);
    fwrite("WAVE", 1, 4, out);
    fwrite("fmt ", 1, 4, out);

    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    fwrite(&fmt_size, sizeof(fmt_size), 1, out);
    fwrite(&audio_format, sizeof(audio_format), 1, out);
    fwrite(&channels, sizeof(channels), 1, out);
    fwrite(&sample_rate, sizeof(sample_rate), 1, out);
    fwrite(&byte_rate, sizeof(byte_rate), 1, out);
    fwrite(&block_align, sizeof(block_align), 1, out);
    fwrite(&bits_per_sample, sizeof(bits_per_sample), 1, out);
    fwrite("data", 1, 4, out);
    fwrite(&data_size, sizeof(data_size), 1, out);
    return true;
}

bool sdCard_convert_raw_to_wav(uint32_t sample_rate_hz)
{
    FILE *input = fopen(s_raw_path, "rb");
    if (input == NULL) {
        ESP_LOGE(TAG, "Unable to open raw audio for conversion: %s", s_raw_path);
        return false;
    }

    fseek(input, 0, SEEK_END);
    long raw_size = ftell(input);
    if (raw_size < 0) {
        fclose(input);
        return false;
    }
    fseek(input, 0, SEEK_SET);

    FILE *output = fopen(s_wav_path, "wb");
    if (output == NULL) {
        fclose(input);
        ESP_LOGE(TAG, "Unable to create WAV file: %s", s_wav_path);
        return false;
    }

    if (!write_wav_header(output, sample_rate_hz, 24, 1, (uint32_t)raw_size)) {
        fclose(input);
        fclose(output);
        return false;
    }

    const size_t buffer_size = 4096;
    uint8_t *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate WAV conversion buffer");
        fclose(input);
        fclose(output);
        return false;
    }

    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, buffer_size, input)) > 0) {
        if (fwrite(buffer, 1, read_bytes, output) != read_bytes) {
            ESP_LOGE(TAG, "Failed to write WAV data chunk");
            free(buffer);
            fclose(input);
            fclose(output);
            return false;
        }
    }

    free(buffer);
    fclose(input);
    fclose(output);
    return true;
}

bool sdCard_verify_run(void)
{
    struct stat st;
    if (stat(s_wav_path, &st) != 0 || st.st_size <= 44) {
        return false;
    }
    if (stat(s_metrics_path, &st) != 0 || st.st_size <= 0) {
        return false;
    }
    return true;
}

const char *sdCard_get_run_dir(void)
{
    return s_run_dir[0] ? s_run_dir : NULL;
}

const char *sdCard_get_wav_path(void)
{
    return s_wav_path[0] ? s_wav_path : NULL;
}

const char *sdCard_get_metrics_path(void)
{
    return s_metrics_path[0] ? s_metrics_path : NULL;
}

static void refresh_path_timestamp(const char *path, time_t now)
{
    if (!path || path[0] == '\0') {
        return;
    }

    struct utimbuf times = {
        .actime = now,
        .modtime = now,
    };
    if (utime(path, &times) != 0) {
        ESP_LOGW(TAG_RUN, "Failed to update timestamp for %s (errno=%d: %s)",
                 path, errno, strerror(errno));
    }
}

static bool make_full_path(const char *path, char *fullpath, size_t fullpath_len)
{
    if (!path || !fullpath || fullpath_len == 0) {
        return false;
    }

    if (path[0] == '/') {
        snprintf(fullpath, fullpath_len, "%s", path);
    } else {
        snprintf(fullpath, fullpath_len, "%s/%s", SDACS_SD_MOUNT_POINT, path);
    }
    return true;
}

static bool sd_append_raw_to_path(const char *path, const void *buf, size_t len)
{
    char fullpath[256];
    if (!make_full_path(path, fullpath, sizeof(fullpath))) {
        return false;
    }

    FILE *f = fopen(fullpath, "ab");
    if (!f) {
        ESP_LOGE(TAG_RUN, "Failed to open %s (errno=%d: %s)",
                 fullpath, errno, strerror(errno));
        return false;
    }

    size_t written = fwrite(buf, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG_RUN, "Short raw write to %s (%u/%u bytes)",
                 fullpath, (unsigned)written, (unsigned)len);
        return false;
    }
    return true;
}

static bool sd_write_raw(FILE *f, const void *buf, size_t len)
{
    size_t written = fwrite(buf, 1, len, f);
    if (written != len) {
        ESP_LOGE(TAG_RUN, "Short raw write (%u/%u bytes)",
                 (unsigned)written, (unsigned)len);
        return false;
    }
    return true;
}

static void log_file_stat(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG_RUN, "Missing file: %s", path);
        return;
    }
    ESP_LOGI(TAG_RUN, "File present: %s (%ld bytes)", path, (long)st.st_size);
}

static void log_dir_listing(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (!d) {
        ESP_LOGW(TAG_RUN, "Could not open directory: %s", dir_path);
        return;
    }

    ESP_LOGI(TAG_RUN, "Directory listing for %s:", dir_path);
    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        ESP_LOGI(TAG_RUN, "  %s", ent->d_name);
    }
    closedir(d);
}

static void log_file_crc32(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG_RUN, "Could not open for CRC32: %s", path);
        return;
    }

    uint8_t buf[256];
    uint32_t crc = 0;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc = esp_rom_crc32_le(crc, buf, n);
    }
    fclose(f);

    ESP_LOGI(TAG_RUN, "CRC32 %s = 0x%08" PRIX32, path, crc);
}

esp_err_t run_storage_init(run_storage_t *rs)
{
    if (!rs) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(rs, 0, sizeof(*rs));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDACS_SD_MOSI_GPIO,
        .miso_io_num = SDACS_SD_MISO_GPIO,
        .sclk_io_num = SDACS_SD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_RUN, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = SDACS_SD_CS_GPIO;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    vTaskDelay(pdMS_TO_TICKS(250));

    for (int attempt = 1; attempt <= 5; ++attempt) {
        ret = esp_vfs_fat_sdspi_mount(
            SDACS_SD_MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &rs->card
        );
        if (ret == ESP_OK) {
            break;
        }

        ESP_LOGW(TAG_RUN, "SD mount attempt %d/5 failed: %s", attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_RUN, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    ESP_LOGI(TAG_RUN, "SD card mounted at %s", SDACS_SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, rs->card);
    return ESP_OK;
}

esp_err_t run_storage_create_session(run_storage_t *rs, const char *node_id)
{
    if (!rs || !node_id) {
        return ESP_ERR_INVALID_ARG;
    }

    char ts[32];
    time_t now = time(NULL);

    if (time_sync_is_valid()) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(ts, sizeof(ts), "%Y%m%d", &timeinfo);
    } else {
        int64_t boot_ms = esp_timer_get_time() / 1000;
        snprintf(ts, sizeof(ts), "boot_%lld", (long long)boot_ms);
    }

    bool created = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        snprintf(rs->run_dir, sizeof(rs->run_dir),
                 "%s/%s_%s_%02d",
                 SDACS_SD_MOUNT_POINT, node_id, ts, attempt);

        if (mkdir(rs->run_dir, 0777) == 0) {
            created = true;
            break;
        }

        if (errno == EEXIST) {
            continue;
        }

        ESP_LOGW(TAG_RUN, "Failed to create run directory %s (errno=%d: %s)",
                 rs->run_dir, errno, strerror(errno));
    }

    if (!created) {
        ESP_LOGE(TAG_RUN, "Could not create a unique run directory under %s", SDACS_SD_MOUNT_POINT);
        return ESP_FAIL;
    }

    snprintf(rs->raw_path, sizeof(rs->raw_path), "%s/audio.raw", rs->run_dir);
    snprintf(rs->wav_path, sizeof(rs->wav_path), "%s/audio.wav", rs->run_dir);
    snprintf(rs->csv_path, sizeof(rs->csv_path), "%s/metrics.csv", rs->run_dir);
    snprintf(rs->cal_csv_path, sizeof(rs->cal_csv_path), "%s/calibration.csv", rs->run_dir);
    snprintf(rs->cal_offset_path, sizeof(rs->cal_offset_path), "%s/calibration_offset.txt", rs->run_dir);

    static const char *header =
        "timestamp,node_id,LAeq_dB,peak_dB,dbfs,rms,temp_C,humidity,fft_peak_Hz\n";

    FILE *csv = fopen(rs->csv_path, "w");
    if (!csv) return ESP_FAIL;
    fputs(header, csv);
    fclose(csv);

    FILE *cal_csv = fopen(rs->cal_csv_path, "w");
    if (!cal_csv) return ESP_FAIL;
    fputs(header, cal_csv);
    fclose(cal_csv);

    FILE *cal_txt = fopen(rs->cal_offset_path, "w");
    if (!cal_txt) return ESP_FAIL;
    fprintf(cal_txt, "node_id=%s\n", node_id);
    fprintf(cal_txt, "sample_rate_hz=%d\n", SDACS_SAMPLE_RATE_HZ);
    fprintf(cal_txt, "record_seconds=%d\n", SDACS_RECORD_SECONDS);
    fprintf(cal_txt, "i2s_bclk_gpio=%d\n", SDACS_I2S_BCLK_GPIO);
    fprintf(cal_txt, "i2s_ws_gpio=%d\n", SDACS_I2S_WS_GPIO);
    fprintf(cal_txt, "i2s_din_gpio=%d\n", SDACS_I2S_DIN_GPIO);
    fprintf(cal_txt, "cal_signal=TO_FILL_IN\n");
    fprintf(cal_txt, "reference_meter_db=TO_FILL_IN\n");
    fprintf(cal_txt, "offset_db=TO_FILL_IN\n");
    fclose(cal_txt);

    ESP_LOGI(TAG_RUN, "Created run folder: %s", rs->run_dir);
    return ESP_OK;
}

bool run_storage_begin_raw(run_storage_t *rs)
{
    if (!rs) {
        return false;
    }
    if (rs->raw_file) {
        return true;
    }

    char fullpath[256];
    if (!make_full_path(rs->raw_path, fullpath, sizeof(fullpath))) {
        return false;
    }

    rs->raw_file = fopen(fullpath, "wb");
    if (!rs->raw_file) {
        ESP_LOGE(TAG_RUN, "Failed to open %s (errno=%d: %s)",
                 fullpath, errno, strerror(errno));
        return false;
    }

    return true;
}

bool run_storage_append_raw(run_storage_t *rs, const int32_t *samples, size_t count)
{
    if (!rs || !samples || count == 0) {
        return false;
    }

    if (rs->raw_file) {
        return sd_write_raw(rs->raw_file, samples, count * sizeof(int32_t));
    }

    return sd_append_raw_to_path(rs->raw_path, samples, count * sizeof(int32_t));
}

bool run_storage_end_raw(run_storage_t *rs)
{
    bool ok = true;
    if (!rs || !rs->raw_file) {
        return true;
    }

    if (fflush(rs->raw_file) != 0) {
        ESP_LOGE(TAG_RUN, "Failed to flush raw file (errno=%d: %s)", errno, strerror(errno));
        ok = false;
    }
    if (fsync(fileno(rs->raw_file)) != 0) {
        ESP_LOGE(TAG_RUN, "Failed to sync raw file (errno=%d: %s)", errno, strerror(errno));
        ok = false;
    }
    if (fclose(rs->raw_file) != 0) {
        ESP_LOGE(TAG_RUN, "Failed to close raw file (errno=%d: %s)", errno, strerror(errno));
        ok = false;
    }
    rs->raw_file = NULL;
    return ok;
}

bool run_storage_append_metrics(run_storage_t *rs, const metrics_record_t *rec)
{
    if (!rs || !rec) {
        return false;
    }

    FILE *f = fopen(rs->csv_path, "a");
    if (!f) {
        ESP_LOGW(TAG_RUN, "Could not append metrics CSV");
        return false;
    }

    fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.6f,%.2f,%.2f,%.1f\n",
            rec->timestamp, rec->node_id, rec->laeq_db, rec->peak_db,
            rec->dbfs, rec->rms, rec->temp_c, rec->humidity, rec->fft_peak_hz);
    fclose(f);

    f = fopen(rs->cal_csv_path, "a");
    if (!f) {
        return false;
    }

    fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.6f,%.2f,%.2f,%.1f\n",
            rec->timestamp, rec->node_id, rec->laeq_db, rec->peak_db,
            rec->dbfs, rec->rms, rec->temp_c, rec->humidity, rec->fft_peak_hz);
    fclose(f);
    return true;
}

esp_err_t run_storage_convert_raw_to_wav(run_storage_t *rs, uint32_t sample_rate_hz)
{
    if (!rs) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *input = fopen(rs->raw_path, "rb");
    if (!input) {
        ESP_LOGE(TAG_RUN, "Unable to open raw audio for conversion: %s", rs->raw_path);
        return ESP_FAIL;
    }

    fseek(input, 0, SEEK_END);
    long raw_size = ftell(input);
    if (raw_size < 0) {
        fclose(input);
        return ESP_FAIL;
    }
    fseek(input, 0, SEEK_SET);

    FILE *output = fopen(rs->wav_path, "wb");
    if (!output) {
        fclose(input);
        ESP_LOGE(TAG_RUN, "Unable to create WAV file: %s", rs->wav_path);
        return ESP_FAIL;
    }

    if (!write_wav_header(output, sample_rate_hz, 24, 1, (uint32_t)raw_size)) {
        fclose(input);
        fclose(output);
        return ESP_FAIL;
    }

    uint8_t buffer[4096];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, read_bytes, output) != read_bytes) {
            fclose(input);
            fclose(output);
            return ESP_FAIL;
        }
    }

    fclose(input);
    fclose(output);
    return ESP_OK;
}

void run_storage_refresh_timestamps(run_storage_t *rs)
{
    if (!rs) {
        return;
    }
    if (!time_sync_is_valid()) {
        ESP_LOGW(TAG_RUN, "Skipping SD timestamp refresh because system time is not valid.");
        return;
    }

    time_t now = time(NULL);
    refresh_path_timestamp(rs->raw_path, now);
    refresh_path_timestamp(rs->csv_path, now);
    refresh_path_timestamp(rs->cal_csv_path, now);
    refresh_path_timestamp(rs->cal_offset_path, now);
    refresh_path_timestamp(rs->run_dir, now);
}

void run_storage_verify(run_storage_t *rs)
{
    if (!rs) {
        return;
    }

    log_file_stat(rs->raw_path);
    log_file_stat(rs->csv_path);
    log_file_stat(rs->cal_csv_path);
    log_file_stat(rs->cal_offset_path);
    log_dir_listing(rs->run_dir);
    log_file_crc32(rs->raw_path);
    log_file_crc32(rs->csv_path);
}
