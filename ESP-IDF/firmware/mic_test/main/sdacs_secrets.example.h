/*
 * SDACS module: Template for untracked deployment secrets
 *
 * Purpose:
 *   Documents the compile-time Wi-Fi, MQTT, node identity, and synchronization defines expected by the firmware.
 *
 * Design note:
 *   The real sdacs_secrets.h is intentionally ignored by Git.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

// Copy this file to sdacs_secrets.h and fill in local values.
// main/sdacs_secrets.h is ignored by git.

#define SDACS_SECRET_WIFI_SSID        ""
#define SDACS_SECRET_WIFI_PASS        ""
#define SDACS_SECRET_MQTT_URI         ""
#define SDACS_SECRET_MQTT_TOPIC       ""
#define SDACS_SECRET_ALWAYS_SYNC_MQTT 0
