/* SPDX-License-Identifier: MIT */
#ifndef HOST_H
#define HOST_H
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0602
#define COBJMACROS
#include <winsock2.h>
#include <windows.h>
#include "cJSON.h"
#include <stdlib.h>
void json_text(cJSON *o, const char *key, const char *value);
void json_number(cJSON *o, const char *key, double value);
void utf8(const wchar_t *in, char *out, int cap);
cJSON *device(cJSON *array, const char *id, const char *category,
              const char *name, const char *state, const char *detail);
cJSON *collect_cpu(void);
cJSON *collect_memory(void);
cJSON *collect_processes(void);
cJSON *collect_network(double sample);
cJSON *collect_inventory(void);
cJSON *collect_sensors(void);
void sensor_shutdown(void);
#endif
