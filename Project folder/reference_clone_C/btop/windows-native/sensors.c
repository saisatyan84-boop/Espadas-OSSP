/* SPDX-License-Identifier: MIT
 * NVIDIA NVML C ABI. The DLL is supplied by the installed NVIDIA driver.
 * Optional functions are checked individually; missing readings stay null.
 */
#include "host.h"
#include <stdio.h>
#include <string.h>
typedef void *NvDevice;
typedef struct { unsigned gpu, memory; } NvUsage;
typedef struct { unsigned long long total, free, used; } NvMemory;
typedef int (__cdecl *NvInit)(void);
typedef int (__cdecl *NvCount)(unsigned*);
typedef int (__cdecl *NvGet)(unsigned,NvDevice*);
typedef int (__cdecl *NvName)(NvDevice,char*,unsigned);
typedef int (__cdecl *NvUtil)(NvDevice,NvUsage*);
typedef int (__cdecl *NvTemp)(NvDevice,unsigned,unsigned*);
typedef int (__cdecl *NvMem)(NvDevice,NvMemory*);
typedef int (__cdecl *NvPower)(NvDevice,unsigned*);
typedef int (__cdecl *NvClock)(NvDevice,unsigned,unsigned*);
static HMODULE module; static int initialized,attempted;
void collect_cpu_sensors(cJSON *array);
void cpu_sensors_shutdown(void);
static void add(cJSON *a,const char *hardware,const char *name,const char *unit,double value) {
    cJSON *o=cJSON_CreateObject(); char id[256];
    snprintf(id,sizeof(id),"%s/%s",hardware,name);
    json_text(o,"id",id); json_text(o,"hardware",hardware); json_text(o,"kind","GpuNvidia");
    json_text(o,"name",name); json_text(o,"unit",unit); json_number(o,"value",value); cJSON_AddItemToArray(a,o);
}
cJSON *collect_sensors(void) {
    if (!attempted) {
        attempted=1;
        /* Restrict lookup to Windows system directory, never current directory. */
        module=LoadLibraryExW(L"nvml.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) {
            NvInit init=(NvInit)GetProcAddress(module,"nvmlInit_v2");
            if (init && init()==0) initialized=1;
        }
    }
    cJSON *o=cJSON_CreateObject(), *a=cJSON_CreateArray();
    cJSON_AddItemToObject(o,"sensors",a);
    collect_cpu_sensors(a);
    json_text(o,"status",initialized?"Native C PDH + NVML; CPU/SSD temperatures N/A":"Native C PDH; NVIDIA NVML unavailable; temperatures N/A");
    if (!initialized) return o;
    NvCount count_fn=(NvCount)GetProcAddress(module,"nvmlDeviceGetCount_v2");
    NvGet get=(NvGet)GetProcAddress(module,"nvmlDeviceGetHandleByIndex_v2");
    NvName name_fn=(NvName)GetProcAddress(module,"nvmlDeviceGetName");
    NvUtil util=(NvUtil)GetProcAddress(module,"nvmlDeviceGetUtilizationRates");
    NvTemp temp=(NvTemp)GetProcAddress(module,"nvmlDeviceGetTemperature");
    NvMem mem=(NvMem)GetProcAddress(module,"nvmlDeviceGetMemoryInfo");
    NvPower power=(NvPower)GetProcAddress(module,"nvmlDeviceGetPowerUsage");
    NvClock clock=(NvClock)GetProcAddress(module,"nvmlDeviceGetClockInfo");
    unsigned count=0;
    if (!count_fn || !get || count_fn(&count)!=0) return o;
    for (unsigned i=0;i<count && i<32;i++) {
        NvDevice d; if(get(i,&d)!=0) continue;
        char raw[160]="NVIDIA GPU",name[200];
        if(name_fn) name_fn(d,raw,sizeof(raw));
        snprintf(name,sizeof(name),"%s [%u]",raw,i);
        NvUsage u; NvMemory m; unsigned value;
        add(a,name,"GPU Core","%",util && util(d,&u)==0?u.gpu:-1.0);
        add(a,name,"GPU Temperature","C",temp && temp(d,0,&value)==0?value:-1.0);
        if (mem && mem(d,&m)==0) {
            add(a,name,"GPU Memory Used","MiB",m.used/1048576.0);
            add(a,name,"GPU Memory Total","MiB",m.total/1048576.0);
        }
        add(a,name,"GPU Power","W",power && power(d,&value)==0?value/1000.0:-1.0);
        add(a,name,"GPU Clock","MHz",clock && clock(d,0,&value)==0?value:-1.0);
        add(a,name,"GPU Memory Clock","MHz",clock && clock(d,2,&value)==0?value:-1.0);
    }
    return o;
}
void sensor_shutdown(void) {
    cpu_sensors_shutdown();
    if(initialized) { NvInit end=(NvInit)GetProcAddress(module,"nvmlShutdown"); if(end) end(); }
    if(module) FreeLibrary(module);
}
