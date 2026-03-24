#pragma once

#include "run_storage.h"

void command_dispatcher_init(run_storage_t *storage, const char *node_id, const char *base_topic);
void command_dispatcher_handle(const char *topic, const char *payload, int len);
