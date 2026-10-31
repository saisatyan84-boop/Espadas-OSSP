/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
void json_text(cJSON *o, const char *key, const char *value) {
    cJSON_AddStringToObject(o, key, value ? value : "");
}
void json_number(cJSON *o, const char *key, double value) {
    if (isfinite(value) && value >= 0) cJSON_AddNumberToObject(o, key, value);
    else cJSON_AddNullToObject(o, key);
}
void utf8(const wchar_t *in, char *out, int cap) {
    if (cap < 1) return;
    out[0] = 0;
    if (in && !WideCharToMultiByte(CP_UTF8, 0, in, -1, out, cap, NULL, NULL))
        out[0] = 0;
    out[cap - 1] = 0;
}
cJSON *device(cJSON *a, const char *id, const char *category,
              const char *name, const char *state, const char *detail) {
    cJSON *o = cJSON_CreateObject();
    json_text(o,"id",id); json_text(o,"category",category);
    json_text(o,"name",name); json_text(o,"state",state);
    json_text(o,"detail",detail); cJSON_AddNullToObject(o,"battery");
    cJSON_AddItemToArray(a,o); return o;
}
int main(int argc, char **argv) {
    unsigned interval = 2000, frames = 0;
    for (int i=1; i<argc; ++i) {
        if (!strcmp(argv[i],"--stream")) continue;
        if ((!strcmp(argv[i],"--frames") || !strcmp(argv[i],"--interval-ms")) && i+1<argc) {
            int is_interval = !strcmp(argv[i],"--interval-ms");
            char *end; unsigned long v = strtoul(argv[++i], &end, 10);
            if (*end || v > 1000000) return 2;
            if (is_interval) { if (v<1000 || v>60000) return 2; interval=(unsigned)v; }
            else frames=(unsigned)v;
        } else { fputs("Usage: sysmon-host --stream [--interval-ms 1000..60000] [--frames N]\n",stderr); return 2; }
    }
    SetConsoleOutputCP(CP_UTF8);
    signal(SIGINT,stop); signal(SIGTERM,stop);
    ULONGLONG began=GetTickCount64(), inventory_at=0;
    cJSON *inventory=NULL;
    for (unsigned count=0; running && (!frames || count<frames); count++) {
        ULONGLONG tick=GetTickCount64();
        if (!inventory || tick-inventory_at>=10000) {
            cJSON_Delete(inventory); inventory=collect_inventory(); inventory_at=tick;
        }
        tick=GetTickCount64();
        double sample=(double)(tick-began)/1000.0;
        cJSON *frame=cJSON_CreateObject(), *host=cJSON_CreateObject();
        cJSON_AddNumberToObject(frame,"schema_version",1);
        json_text(frame,"source","windows"); json_number(frame,"sample_seconds",sample);
        cJSON_AddItemToObject(frame,"processes",collect_processes());
        cJSON_AddItemToObject(frame,"host",host);
        cJSON_AddItemToObject(host,"cpu",collect_cpu());
        cJSON_AddItemToObject(host,"memory",collect_memory());
        cJSON_AddItemToObject(host,"networks",collect_network(sample));
        cJSON_AddItemToObject(host,"inventory",cJSON_Duplicate(inventory,1));
        cJSON_AddItemToObject(host,"hardware",collect_sensors());
        char *line=cJSON_PrintUnformatted(frame);
        int failed=!line || puts(line)==EOF || fflush(stdout)==EOF;
        cJSON_free(line); cJSON_Delete(frame);
        if (failed || (frames && count+1>=frames)) break;
        ULONGLONG elapsed=GetTickCount64()-tick;
        if (elapsed<interval) Sleep(interval-(DWORD)elapsed);
    }
    cJSON_Delete(inventory); sensor_shutdown(); return 0;
}
