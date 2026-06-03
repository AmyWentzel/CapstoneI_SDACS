#pragma once

#include "driver/gpio.h"

#if __has_include("sdacs_secrets.h")
#include "sdacs_secrets.h"
#endif

#ifndef SDACS_SECRET_WIFI_SSID
#define SDACS_SECRET_WIFI_SSID        "ElevatorNetwork24Router"
#endif

#ifndef SDACS_SECRET_WIFI_PASS
#define SDACS_SECRET_WIFI_PASS        "helloworld"
#endif

#ifndef SDACS_SECRET_MQTT_URI
#define SDACS_SECRET_MQTT_URI         ""
#endif

#ifndef SDACS_SECRET_MQTT_TOPIC
#define SDACS_SECRET_MQTT_TOPIC       ""
#endif

#ifndef SDACS_SECRET_ALWAYS_SYNC_MQTT
#define SDACS_SECRET_ALWAYS_SYNC_MQTT 0
#endif

#define SDACS_NODE_ID                    "node01"
#define SDACS_SD_MOUNT_POINT             "/sdcard"

#define SDACS_I2S_BCLK_GPIO              GPIO_NUM_11  // Metro S3 D11 (ICS-43432 SCLK - clock)
#define SDACS_I2S_WS_GPIO                GPIO_NUM_13  // Metro S3 D13 (unused for PDM)
#define SDACS_I2S_DIN_GPIO               GPIO_NUM_12  // Metro S3 D12 (ICS-43432 SD - serial data)

#define SDACS_SD_MOSI_GPIO               GPIO_NUM_42
#define SDACS_SD_MISO_GPIO               GPIO_NUM_21
#define SDACS_SD_SCLK_GPIO               GPIO_NUM_39
#define SDACS_SD_CS_GPIO                 GPIO_NUM_45

#define SDACS_SAMPLE_RATE_HZ             48000
#define SDACS_I2S_FRAMES_PER_READ        512
#define SDACS_RECORD_SECONDS             20
#define SDACS_AUDIO_CHUNK_SAMPLES        9600  // 48000 * 0.2 = 9600 samples for 200ms chunks
#define SDACS_FFT_SIZE                   1024
#define SDACS_CAL_OFFSET_DB              94.0f
#define SDACS_I2S_READ_TIMEOUT_MS        100
#define SDACS_RAW_CHUNK_SIZE             1024

#define SDACS_VALID_UNIX_TIME_EPOCH      1700000000
#define SDACS_WIFI_TIME_SYNC_WAIT_MS     15000
#define SDACS_CAPTURE_TASK_STACK_SIZE    8192
#define SDACS_CAPTURE_TASK_PRIORITY      5
#define SDACS_CAPTURE_TASK_CORE_ID       1

#define SDACS_TEMP_HUMIDITY_I2C_PORT     0
#define SDACS_TEMP_HUMIDITY_SDA_GPIO     47
#define SDACS_TEMP_HUMIDITY_SCL_GPIO     48
#define SDACS_TEMP_HUMIDITY_FREQ_HZ      100000
#define SDACS_TEMP_HUMIDITY_ADDR         0x44
#define SDACS_TEMP_HUMIDITY_PERIOD_MS    500

#define SDACS_PROVISION_WIFI_SSID        SDACS_SECRET_WIFI_SSID
#define SDACS_PROVISION_WIFI_PASS        SDACS_SECRET_WIFI_PASS
#define SDACS_PROVISION_MQTT_URI         SDACS_SECRET_MQTT_URI
#define SDACS_PROVISION_MQTT_TOPIC       SDACS_SECRET_MQTT_TOPIC
#define SDACS_PROVISION_ALWAYS_SYNC_MQTT SDACS_SECRET_ALWAYS_SYNC_MQTT
