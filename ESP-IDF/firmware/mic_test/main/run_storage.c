#include "run_storage.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>
#include <utime.h>

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

#include "sdacs_config.h"
#include "time_sync.h"

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

static const char *TAG = "run_storage";

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
        ESP_LOGW(TAG, "Failed to update timestamp for %s (errno=%d: %s)",
                 path, errno, strerror(errno));
    }
}

static bool append_file(const char *fullpath, const void *buf, size_t len)
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
    return written == len;
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
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
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

    ret = esp_vfs_fat_sdspi_mount(
        SDACS_SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &rs->card
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free(host.slot);
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", SDACS_SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, rs->card);
    return ESP_OK;
}

esp_err_t run_storage_create_session(run_storage_t *rs, const char *node_id)
{
    char ts[32];
    time_t now;
    bool dir_created = false;
    static const char *header = "timestamp,node_id,LAeq_dB,peak_dB,dbfs,rms,temp_C,humidity,fft_peak_Hz,fft_low_ratio,fft_mid_ratio,fft_high_ratio,fft_total_energy\n";

    if (!rs || !node_id) {
        return ESP_ERR_INVALID_ARG;
    }

    now = time(NULL);
    if (time_sync_is_valid()) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &timeinfo);
    } else {
        int64_t boot_ms = esp_timer_get_time() / 1000;
        snprintf(ts, sizeof(ts), "boot-%lld", (long long)boot_ms);
    }

    for (int suffix = 0; suffix < 100; ++suffix) {
        int n = 0;
        if (suffix == 0) {
            n = snprintf(rs->run_dir, sizeof(rs->run_dir), "%s/capture_%s_%s", SDACS_SD_MOUNT_POINT, ts, node_id);
        } else {
            n = snprintf(rs->run_dir, sizeof(rs->run_dir), "%s/capture_%s_%s_%02d", SDACS_SD_MOUNT_POINT, ts, node_id, suffix);
        }

        if (n < 0 || n >= (int)sizeof(rs->run_dir)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (mkdir(rs->run_dir, 0775) == 0) {
            dir_created = true;
            break;
        }
        if (errno != EEXIST) {
            ESP_LOGE(TAG, "Failed to create run directory: %s (errno=%d: %s)",
                     rs->run_dir, errno, strerror(errno));
            return ESP_FAIL;
        }
    }

    if (!dir_created) {
        ESP_LOGE(TAG, "Could not find unique run directory name for base '%s'", ts);
        return ESP_FAIL;
    }

    if (snprintf(rs->raw_path, sizeof(rs->raw_path), "%s/audio.raw", rs->run_dir) >= (int)sizeof(rs->raw_path) ||
        snprintf(rs->wav_path, sizeof(rs->wav_path), "%s/audio_%s_%s.wav", rs->run_dir, ts, node_id) >= (int)sizeof(rs->wav_path) ||
        snprintf(rs->csv_path, sizeof(rs->csv_path), "%s/metrics_%s_%s.csv", rs->run_dir, ts, node_id) >= (int)sizeof(rs->csv_path) ||
        snprintf(rs->cal_offset_path, sizeof(rs->cal_offset_path), "%s/calibration_offset.txt", rs->run_dir) >= (int)sizeof(rs->cal_offset_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *csv = fopen(rs->csv_path, "w");
    if (!csv) {
        return ESP_FAIL;
    }
    fputs(header, csv);
    fclose(csv);

    FILE *cal_txt = fopen(rs->cal_offset_path, "w");
    if (!cal_txt) {
        return ESP_FAIL;
    }
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

    ESP_LOGI(TAG, "Created capture folder: %s", rs->run_dir);
    ESP_LOGI(TAG, "Metrics CSV: %s", rs->csv_path);
    return ESP_OK;
}

bool run_storage_append_raw(run_storage_t *rs, const int32_t *samples, size_t count)
{
    if (!rs || !samples || count == 0) {
        return false;
    }

    return append_file(rs->raw_path, samples, count * sizeof(int32_t));
}

bool run_storage_append_metrics(run_storage_t *rs, const metrics_record_t *rec)
{
    if (!rs || !rec) {
        return false;
    }

    FILE *f = fopen(rs->csv_path, "a");
    if (!f) {
        ESP_LOGW(TAG, "Could not append metrics CSV");
        return false;
    }

    fprintf(f, "%s,%s,%.2f,%.2f,%.2f,%.6f,%.2f,%.2f,%.1f,%.6f,%.6f,%.6f,%.6e\n",
            rec->timestamp, rec->node_id, rec->laeq_db, rec->peak_db,
            rec->dbfs, rec->rms, rec->temp_c, rec->humidity, rec->fft_peak_hz,
            rec->fft_low_ratio, rec->fft_mid_ratio, rec->fft_high_ratio,
            rec->fft_total_energy);
    fclose(f);

    ESP_LOGI(TAG, "SD metrics row: low=%.6f mid=%.6f high=%.6f total=%.6e",
             rec->fft_low_ratio, rec->fft_mid_ratio, rec->fft_high_ratio,
             rec->fft_total_energy);
    return true;
}

esp_err_t run_storage_convert_raw_to_wav(run_storage_t *rs, uint32_t sample_rate_hz)
{
    if (!rs) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *raw_file = fopen(rs->raw_path, "rb");
    if (!raw_file) {
        ESP_LOGE(TAG, "Failed to open raw file %s", rs->raw_path);
        return ESP_FAIL;
    }

    fseek(raw_file, 0, SEEK_END);
    long raw_size_l = ftell(raw_file);
    fseek(raw_file, 0, SEEK_SET);
    if (raw_size_l <= 0) {
        fclose(raw_file);
        ESP_LOGE(TAG, "Raw file is empty");
        return ESP_FAIL;
    }

    FILE *wav_file = fopen(rs->wav_path, "wb");
    if (!wav_file) {
        fclose(raw_file);
        ESP_LOGE(TAG, "Failed to create WAV file %s", rs->wav_path);
        return ESP_FAIL;
    }

    size_t num_samples = (size_t)raw_size_l / sizeof(int32_t);
    wav_header_t header = {0};
    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    memcpy(header.data, "data", 4);
    header.file_size = 36 + (uint32_t)(num_samples * 3);
    header.fmt_size = 16;
    header.format = 1;
    header.channels = 1;
    header.sample_rate = sample_rate_hz;
    header.byte_rate = sample_rate_hz * 3;
    header.block_align = 3;
    header.bits_per_sample = 24;
    header.data_size = (uint32_t)(num_samples * 3);
    fwrite(&header, sizeof(header), 1, wav_file);

    int32_t chunk_buf[SDACS_WAV_CHUNK_SIZE];
    size_t samples_left = num_samples;
    while (samples_left > 0) {
        size_t to_read = (samples_left > SDACS_WAV_CHUNK_SIZE) ? SDACS_WAV_CHUNK_SIZE : samples_left;
        size_t rd = fread(chunk_buf, sizeof(int32_t), to_read, raw_file);
        if (rd == 0) {
            break;
        }
        for (size_t i = 0; i < rd; ++i) {
            uint8_t bytes[3] = {
                (uint8_t)(chunk_buf[i] & 0xFF),
                (uint8_t)((chunk_buf[i] >> 8) & 0xFF),
                (uint8_t)((chunk_buf[i] >> 16) & 0xFF),
            };
            fwrite(bytes, 1, sizeof(bytes), wav_file);
        }
        samples_left -= rd;
    }

    fclose(raw_file);
    fflush(wav_file);
    fsync(fileno(wav_file));
    fclose(wav_file);
    ESP_LOGI(TAG, "WAV file created: %s", rs->wav_path);
    return ESP_OK;
}

void run_storage_refresh_timestamps(run_storage_t *rs)
{
    if (!rs) {
        return;
    }
    if (!time_sync_is_valid()) {
        ESP_LOGW(TAG, "Skipping SD timestamp refresh because system time is not valid.");
        return;
    }

    time_t now = time(NULL);
    refresh_path_timestamp(rs->raw_path, now);
    refresh_path_timestamp(rs->csv_path, now);
    refresh_path_timestamp(rs->cal_offset_path, now);
    refresh_path_timestamp(rs->wav_path, now);
    refresh_path_timestamp(rs->run_dir, now);
}

void run_storage_verify(run_storage_t *rs)
{
    if (!rs) {
        return;
    }

    log_file_stat(rs->raw_path);
    log_file_stat(rs->csv_path);
    log_file_stat(rs->cal_offset_path);
    log_file_stat(rs->wav_path);
    log_dir_listing(rs->run_dir);
    log_file_crc32(rs->raw_path);
    log_file_crc32(rs->csv_path);
    log_file_crc32(rs->wav_path);
}

esp_err_t run_storage_get_file_size(const char *path, size_t *out_size)
{
    struct stat st;

    if (!path || path[0] == '\0' || !out_size) {
        return ESP_ERR_INVALID_ARG;
    }

    if (stat(path, &st) != 0) {
        *out_size = 0;
        return ESP_FAIL;
    }

    if (st.st_size <= 0) {
        *out_size = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    *out_size = (size_t)st.st_size;
    return ESP_OK;
}

esp_err_t run_storage_verify_capture(run_storage_t *rs, char *reason, size_t reason_sz,
                                     size_t *raw_bytes, size_t *wav_bytes, size_t *csv_bytes)
{
    esp_err_t err = ESP_OK;

    if (!rs) {
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz, "storage not initialized");
        }
        return ESP_ERR_INVALID_ARG;
    }

    if (reason && reason_sz > 0) {
        reason[0] = '\0';
    }

    err = run_storage_get_file_size(rs->raw_path, raw_bytes);
    if (err != ESP_OK) {
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz, "raw file missing or empty");
        }
        ESP_LOGE(TAG, "Capture verification failed: %s", reason ? reason : "raw file");
        return err;
    }

    err = run_storage_get_file_size(rs->wav_path, wav_bytes);
    if (err != ESP_OK) {
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz, "wav file missing or empty");
        }
        ESP_LOGE(TAG, "Capture verification failed: %s", reason ? reason : "wav file");
        return err;
    }

    err = run_storage_get_file_size(rs->csv_path, csv_bytes);
    if (err != ESP_OK) {
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz, "metrics csv missing or empty");
        }
        ESP_LOGE(TAG, "Capture verification failed: %s", reason ? reason : "metrics csv");
        return err;
    }

    ESP_LOGI(TAG, "Capture verification passed: raw=%u wav=%u csv=%u bytes",
             (unsigned)(raw_bytes ? *raw_bytes : 0U),
             (unsigned)(wav_bytes ? *wav_bytes : 0U),
             (unsigned)(csv_bytes ? *csv_bytes : 0U));
    return ESP_OK;
}
