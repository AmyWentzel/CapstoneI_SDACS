if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    cmake_minimum_required(VERSION 3.24)
    project(mic_test_component NONE)
    message(FATAL_ERROR
        "Do not run idf.py from the component folder.\n"
        "Run from project root: C:/ws/CapstoneI_SDACS/ESP-IDF/firmware/mic_test")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "network.c"
        "config_store.c"
        "mqtt_publish.c"
        "batteryLife.c"
        "sdCard.c"
        "metricsCSV.c"
        "audioAnalysis.c"
        "recordAudio.c"
        "tempHumidity.c"
    INCLUDE_DIRS
        "headers"
    REQUIRES
        esp_wifi
        esp_netif
        mqtt
        nvs_flash
    PRIV_REQUIRES
        driver
        esp_driver_i2s
        esp_driver_sdspi
        fatfs
        sdmmc
        esp_adc
)
