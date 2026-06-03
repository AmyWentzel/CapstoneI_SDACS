#include "metricsCSV.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "network.h"
#include "sdCard.h"

static const char *TAG = "metricsCSV";

bool metricsCSV_init(void)
{
    const char *path = sdCard_get_metrics_path();
    if (path == NULL) {
        ESP_LOGE(TAG, "Metrics CSV path is not available");
        return false;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Unable to create metrics CSV: %s", path);
        return false;
    }

    fprintf(f, "measurement,LAeq_dB,peak_dB,dbfs,rms,temp_C,humidity,fft_peak_Hz\n");
    fclose(f);
    return true;
}

bool metricsCSV_append(const metrics_record_t *rec)
{
    if (rec == NULL) {
        return false;
    }

    const char *path = sdCard_get_metrics_path();
    if (path == NULL) {
        return false;
    }

    FILE *f = fopen(path, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Unable to open metrics CSV for append: %s", path);
        return false;
    }

    fprintf(f,
            "%" PRIu32 ",%.2f,%.2f,%.2f,%.6f,%.2f,%.2f,%.2f\n",
            rec->measurement,
            rec->laeq_db,
            rec->peak_db,
            rec->dbfs,
            rec->rms,
            rec->temp_c,
            rec->humidity,
            rec->fft_peak_hz);
    fclose(f);
    return true;
}

const char *metricsCSV_get_path(void)
{
    return sdCard_get_metrics_path();
}
