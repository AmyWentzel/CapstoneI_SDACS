#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#if __has_include("sdacs_secrets.h")
#include "sdacs_secrets.h"
#endif

#ifndef SDACS_SECRET_WIFI_SSID
#define SDACS_SECRET_WIFI_SSID        ""
#endif

#ifndef SDACS_SECRET_WIFI_PASS
#define SDACS_SECRET_WIFI_PASS        ""
#endif

#ifndef SDACS_SECRET_MQTT_URI
#define SDACS_SECRET_MQTT_URI         ""
#endif

#ifndef SDACS_SECRET_MQTT_TOPIC
#define SDACS_SECRET_MQTT_TOPIC       ""
#endif

#ifndef SDACS_SECRET_NODE_ID
#define SDACS_SECRET_NODE_ID          "node01"
#endif

#ifndef SDACS_SECRET_ALWAYS_SYNC_MQTT
#define SDACS_SECRET_ALWAYS_SYNC_MQTT 0
#endif

#define SDACS_NODE_ID                    SDACS_SECRET_NODE_ID
#define SDACS_FW_VERSION                 "ota-enable-v2"
#define SDACS_SD_SPI_HOST                SPI2_HOST
#define SDACS_SD_SPI_MAX_FREQ_KHZ        5000
#define SDACS_SD_MOUNT_RETRY_COUNT       5
#define SDACS_SD_MOUNT_RETRY_DELAY_MS    750
#define SDACS_SD_MOUNT_POINT             "/sdcard"
#define SDACS_SD_FORMAT_IF_MOUNT_FAILED  0
#define SDACS_SD_ALLOW_FORMAT_IF_MOUNT_FAILED 0
/*
 * SDACS_ENABLE_SD_STORAGE = 0 disables SD mount, raw file writing, WAV
 * conversion, and SD metrics CSV during capture. This mode is intended for
 * MEMS mic/I2S validation; Node-RED/MQTT logging becomes the primary dataset
 * path. Set SDACS_ENABLE_SD_STORAGE back to 1 later if local SD recording is
 * needed.
 */
#define SDACS_ENABLE_SD_STORAGE             0
#define SDACS_CAPTURE_MQTT_ONLY_WHEN_NO_SD  1
/*
 * MQTT feature JSON is larger because of mic diagnostics and storage/audio
 * status fields. Keep these stacks large enough for MQTT client calls and JSON
 * formatting; they can be reduced later after stack high-water marks are
 * measured on hardware.
 */
#define SDACS_MQTT_PUBLISH_TASK_STACK_SIZE  12288
#define SDACS_MQTT_PUBLISH_TASK_PRIORITY    5
#define SDACS_MQTT_PUBLISH_TASK_CORE        0
#define SDACS_MQTT_FEATURE_JSON_MAX_LEN     12288
#define SDACS_MQTT_STATUS_JSON_MAX_LEN      2048

#define SDACS_I2S_BCLK_GPIO              GPIO_NUM_11
#define SDACS_I2S_WS_GPIO                GPIO_NUM_13
#define SDACS_I2S_DIN_GPIO               GPIO_NUM_12

#define SDACS_MIC_MODEL                  "ICS-43432"
#define SDACS_MIC_VALID_BITS             24
#define SDACS_MIC_I2S_SLOT_BITS          32
#define SDACS_MIC_SENSITIVITY_DBFS_94DB_SPL (-26.0f)
#define SDACS_ENABLE_RAW_SAMPLE_DIAGNOSTICS 1
#define SDACS_I2S_CONVERSION_SHIFT8      0
#define SDACS_I2S_CONVERSION_LOW24       1
/*
 * Production MEMS conversion.
 *
 * The PCB ties the ICS-43432 LR/CONFIG pin to GND, so the mic is expected on
 * the left I2S slot. stereo_raw mode reads both 32-bit slots and firmware
 * manually selects the left word.
 *
 * SHIFT8 is the production path for the next validation test:
 *   sign_extend_24(raw_word >> 8)
 *
 * LOW24 is kept only as a diagnostic because low24 made quiet and 1 kHz
 * captures look similarly loud and did not recover the 1 kHz FFT peak.
 */
#define SDACS_I2S_SAMPLE_CONVERSION_MODE SDACS_I2S_CONVERSION_SHIFT8
#if SDACS_I2S_SAMPLE_CONVERSION_MODE == SDACS_I2S_CONVERSION_LOW24
#define SDACS_I2S_SAMPLE_CONVERSION_LABEL "low24"
#else
#define SDACS_I2S_SAMPLE_CONVERSION_LABEL "shift8"
#endif
/*
 * ICS-43432 is a mono microphone using stereo I2S frame timing. The board ties
 * LR/CONFIG to GND, so keep the hardware-selected slot on left.
 */
#define SDACS_I2S_SLOT_MODE_STEREO_FRAME 1
/* 0 = left slot, 1 = right slot */
#define SDACS_I2S_USE_RIGHT_SLOT         0
#if SDACS_I2S_USE_RIGHT_SLOT
#define SDACS_I2S_SELECTED_SLOT_LABEL    "right"
#define SDACS_I2S_SLOT_MASK_LABEL        "right"
#else
#define SDACS_I2S_SELECTED_SLOT_LABEL    "left"
#define SDACS_I2S_SLOT_MASK_LABEL        "left"
#endif

#define SDACS_I2S_RX_MODE_SELECTED_SLOT  0
#define SDACS_I2S_RX_MODE_STEREO_RAW     1
#define SDACS_I2S_RX_MODE                SDACS_I2S_RX_MODE_STEREO_RAW
#if SDACS_I2S_RX_MODE == SDACS_I2S_RX_MODE_STEREO_RAW
#define SDACS_I2S_RX_MODE_LABEL          "stereo_raw"
#define SDACS_I2S_DRIVER_SLOT_MASK_LABEL "both"
#else
#define SDACS_I2S_RX_MODE_LABEL          "selected_slot"
#define SDACS_I2S_DRIVER_SLOT_MASK_LABEL SDACS_I2S_SLOT_MASK_LABEL
#endif
#define SDACS_I2S_VERBOSE_TIMEOUT_LOGS   0
#define SDACS_I2S_DMA_DESC_NUM           8
#define SDACS_I2S_DMA_FRAME_NUM          256

#define SDACS_LED_BATT_1_GPIO            GPIO_NUM_14
#define SDACS_LED_BATT_2_GPIO            GPIO_NUM_15
#define SDACS_LED_BATT_3_GPIO            GPIO_NUM_16
#define SDACS_LED_BATT_4_GPIO            GPIO_NUM_17
#define SDACS_LED_BATT_5_GPIO            GPIO_NUM_18
#define SDACS_LED_BATT_6_GPIO            GPIO_NUM_1

#define SDACS_SD_MOSI_GPIO               GPIO_NUM_42
#define SDACS_SD_MISO_GPIO               GPIO_NUM_21
#define SDACS_SD_SCLK_GPIO               GPIO_NUM_39
#define SDACS_SD_CS_GPIO                 GPIO_NUM_45

#define SDACS_SAMPLE_RATE_HZ             48000
#define SDACS_I2S_FRAMES_PER_READ        256
#define SDACS_RECORD_SECONDS             20
#define SDACS_AUDIO_CHUNK_SAMPLES        2048
#define SDACS_FFT_SIZE                   1024
#define SDACS_FFT_ACOUSTIC_PEAK_MIN_HZ   200.0f
#define SDACS_FFT_ACOUSTIC_PEAK_MAX_HZ   12000.0f
#define SDACS_BAND_SUB_MIN_HZ            20.0f
#define SDACS_BAND_SUB_MAX_HZ            40.0f
#define SDACS_BAND_BASS_MIN_HZ           40.0f
#define SDACS_BAND_BASS_MAX_HZ           160.0f
#define SDACS_BAND_LOW_MID_MIN_HZ        160.0f
#define SDACS_BAND_LOW_MID_MAX_HZ        500.0f
#define SDACS_BAND_MID_MIN_HZ            500.0f
#define SDACS_BAND_MID_MAX_HZ            2000.0f
#define SDACS_BAND_PRESENCE_MIN_HZ       2000.0f
#define SDACS_BAND_PRESENCE_MAX_HZ       5000.0f
#define SDACS_BAND_HIGH_MIN_HZ           5000.0f
#define SDACS_BAND_HIGH_MAX_HZ           12000.0f
#define SDACS_TONE_1KHZ_CENTER_HZ        1000.0f
#define SDACS_TONE_1KHZ_BAND_HALF_WIDTH_HZ 125.0f
#define SDACS_TONE_1KHZ_RATIO_THRESHOLD  0.13f
#define SDACS_TONE_1KHZ_PEAK_TOLERANCE_HZ 150.0f
#define SDACS_CAL_OFFSET_DB              120.0f
#define SDACS_I2S_READ_TIMEOUT_MS        20
#define SDACS_WAV_CHUNK_SIZE             1024
#define SDACS_I2S_PREFLIGHT_ENABLED      1
#define SDACS_I2S_PREFLIGHT_MS           1000
#define SDACS_I2S_PREFLIGHT_RECOVERY_RETRIES 1
#define SDACS_I2S_PREFLIGHT_WARMUP_MS    200
#define SDACS_CAPTURE_MIN_EFFECTIVE_SR_RATIO 0.90f
#define SDACS_I2S_MAX_TIMEOUTS_PER_WINDOW 10

#define SDACS_VALID_UNIX_TIME_EPOCH      1700000000
#define SDACS_WIFI_TIME_SYNC_WAIT_MS     15000
#define SDACS_HEARTBEAT_INTERVAL_MS      15000
#define SDACS_HEARTBEAT_TASK_STACK_SIZE  6144
#define SDACS_HEARTBEAT_TASK_PRIORITY    4
#define SDACS_CAPTURE_TASK_STACK_SIZE    8192
#define SDACS_CAPTURE_TASK_PRIORITY      5
#define SDACS_CAPTURE_TASK_CORE_ID       1

#define SDACS_TEMP_HUMIDITY_I2C_PORT     0
#define SDACS_TEMP_HUMIDITY_SDA_GPIO     47
#define SDACS_TEMP_HUMIDITY_SCL_GPIO     48
#define SDACS_TEMP_HUMIDITY_FREQ_HZ      100000
#define SDACS_TEMP_HUMIDITY_ADDR         0x44
#define SDACS_TEMP_HUMIDITY_PERIOD_MS    2000
#define SDACS_SENSOR_MONITOR_LOG_WINDOW_MS 5000

#define SDACS_FUEL_GAUGE_ENABLED         1
#define SDACS_FUEL_GAUGE_I2C_PORT        SDACS_TEMP_HUMIDITY_I2C_PORT
#define SDACS_FUEL_GAUGE_ADDR            0x36
#define SDACS_FUEL_GAUGE_PERIOD_MS       2000

#define SDACS_PROVISION_WIFI_SSID        SDACS_SECRET_WIFI_SSID
#define SDACS_PROVISION_WIFI_PASS        SDACS_SECRET_WIFI_PASS
#define SDACS_PROVISION_MQTT_URI         SDACS_SECRET_MQTT_URI
#define SDACS_PROVISION_MQTT_TOPIC       SDACS_SECRET_MQTT_TOPIC
#define SDACS_PROVISION_ALWAYS_SYNC_MQTT SDACS_SECRET_ALWAYS_SYNC_MQTT

#define SDACS_BLE_LOCATOR_ENABLED        1
#define SDACS_BOOT_DISABLE_BLE_LOCATOR   0
#define SDACS_BLE_LOCATOR_DURATION_MS    15000
