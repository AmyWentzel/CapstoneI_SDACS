/*
 * SDACS module: Public MQTT command-dispatch interface
 *
 * Purpose:
 *   Connects received command payloads to the SDACS subsystem state machine.
 *
 * Design note:
 *   Initialized once after node identity and MQTT base topics are known.
 *
 * This comment documents engineering intent for the final SDACS implementation;
 * functional behavior is defined by the code and validated configuration below.
 */

#pragma once

#include "run_storage.h"

void command_dispatcher_init(run_storage_t *storage, const char *node_id, const char *base_topic);
void command_dispatcher_handle(const char *topic, const char *payload, int len);
