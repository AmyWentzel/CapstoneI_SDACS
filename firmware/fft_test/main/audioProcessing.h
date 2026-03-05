#ifndef audioProcessing_h
#define audioProcessing_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

#include "esp_system.h"
#include "driver/spi_master.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "soc/uart_struct.h"

#include "esp_dsp.h"

/* ================= USER CONFIG ================= */

// I2S pin mapping (safe pins for ESP32-S3)
#define I2S_BCLK_GPIO   GPIO_NUM_6
#define I2S_WS_GPIO     GPIO_NUM_5
#define I2S_DIN_GPIO    GPIO_NUM_4

// Audio sample rate
#define SAMPLE_RATE_HZ  48000

// I2S read size
#define I2S_FRAMES_PER_READ  512

// Recording duration
#define RECORD_DURATION_SEC  20

// How often to print stats during recording
#define PRINT_PERIOD_MS  250

/* ============================================== */

static const char *TAG = "MIC_TEST";

static i2s_chan_handle_t rx_chan = NULL;

// Recording buffer: 20 seconds at 48kHz = 960,000 samples
// WARNING: This uses ~3.75 MB of RAM. If ESP32-S3 runs out of memory,
// reduce RECORD_DURATION_SEC or use external storage (SPIFFS/SD card).
static int32_t audio_buffer[SAMPLE_RATE_HZ * RECORD_DURATION_SEC];
static size_t buffer_index = 0;
static SemaphoreHandle_t recording_done_sem = NULL;

// FFT parameters
#define FFT_SIZE 1024
static float fft_input[FFT_SIZE];
static float fft_output[FFT_SIZE];
static float magnitude[FFT_SIZE / 2];