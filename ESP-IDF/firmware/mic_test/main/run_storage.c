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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

#include "sdacs_config.h"
#include "time_sync.h"


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

static bool sd_append_raw(const char *path, const void *buf, size_t len)
{
    char fullpath[256];
    if (!make_full_path(path, fullpath, sizeof(fullpath))) {
        return false;
    }

    FILE *f = fopen(fullpath, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s (errno=%d: %s)",
                 fullpath, errno, strerror(errno));
        return false;
    }

    size_t written = fwrite(buf, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "Short raw write to %s (%u/%u bytes)",
                 fullpath, (unsigned)written, (unsigned)len);
        return false;
    }
    return true;
}

static bool sd_write_raw(FILE *f, const void *buf, size_t len)
{
    size_t written = fwrite(buf, 1, len, f);
    if (written != len) {
        ESP_LOGE(TAG, "Short raw write (%u/%u bytes)",
                 (unsigned)written, (unsigned)len);
        return false;
    }
    return true;
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

        ESP_LOGW(TAG, "SD mount attempt %d/5 failed: %s", attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(500));
    }

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

        ESP_LOGW(TAG, "Failed to create run directory %s (errno=%d: %s)",
                 rs->run_dir, errno, strerror(errno));
    }

    if (!created) {
        ESP_LOGE(TAG, "Could not create a unique run directory under %s", SDACS_SD_MOUNT_POINT);
        return ESP_FAIL;
    }

    // Build file paths inside run directory
    snprintf(rs->raw_path, sizeof(rs->raw_path),
             "%s/audio.raw", rs->run_dir);

    snprintf(rs->csv_path, sizeof(rs->csv_path),
             "%s/metrics.csv", rs->run_dir);

    snprintf(rs->cal_csv_path, sizeof(rs->cal_csv_path),
             "%s/calibration.csv", rs->run_dir);

    snprintf(rs->cal_offset_path, sizeof(rs->cal_offset_path),
             "%s/calibration_offset.txt", rs->run_dir);

    // Create CSV files with header
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

    ESP_LOGI(TAG, "Created run folder: %s", rs->run_dir);
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
        ESP_LOGE(TAG, "Failed to open %s (errno=%d: %s)",
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

    return sd_append_raw(rs->raw_path, samples, count * sizeof(int32_t));
}

bool run_storage_end_raw(run_storage_t *rs)
{
    bool ok = true;
    if (!rs || !rs->raw_file) {
        return true;
    }

    if (fflush(rs->raw_file) != 0) {
        ESP_LOGE(TAG, "Failed to flush raw file (errno=%d: %s)", errno, strerror(errno));
        ok = false;
    }
    if (fsync(fileno(rs->raw_file)) != 0) {
        ESP_LOGE(TAG, "Failed to sync raw file (errno=%d: %s)", errno, strerror(errno));
        ok = false;
    }
    if (fclose(rs->raw_file) != 0) {
        ESP_LOGE(TAG, "Failed to close raw file (errno=%d: %s)", errno, strerror(errno));
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
        ESP_LOGW(TAG, "Could not append metrics CSV");
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
