#pragma once

#include "driver/gpio.h"

#define SDACS_NODE_ID                    "node01"
#define SDACS_SD_MOUNT_POINT             "/sdcard"

#define SDACS_I2S_BCLK_GPIO              GPIO_NUM_14
#define SDACS_I2S_WS_GPIO                GPIO_NUM_15
#define SDACS_I2S_DIN_GPIO               GPIO_NUM_16

#define SDACS_SD_MOSI_GPIO               GPIO_NUM_42
#define SDACS_SD_MISO_GPIO               GPIO_NUM_21
#define SDACS_SD_SCLK_GPIO               GPIO_NUM_39
#define SDACS_SD_CS_GPIO                 GPIO_NUM_45

#define SDACS_SAMPLE_RATE_HZ             48000
#define SDACS_I2S_FRAMES_PER_READ        512
#define SDACS_RECORD_SECONDS             20
#define SDACS_AUDIO_CHUNK_SAMPLES        2048
#define SDACS_FFT_SIZE                   1024
#define SDACS_CAL_OFFSET_DB              94.0f
#define SDACS_I2S_READ_TIMEOUT_MS        100
#define SDACS_WAV_CHUNK_SIZE             1024

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
#define SDACS_TEMP_HUMIDITY_PERIOD_MS    2000

#define SDACS_PROVISION_WIFI_SSID        "Cheerios & Shreddies"
#define SDACS_PROVISION_WIFI_PASS        "JesusisLord"
#define SDACS_PROVISION_MQTT_URI         "mqtt://172.20.10.5:1883"
#define SDACS_PROVISION_MQTT_TOPIC       "sdacs/node/node01/features"
#define SDACS_PROVISION_ALWAYS_SYNC_MQTT 1
