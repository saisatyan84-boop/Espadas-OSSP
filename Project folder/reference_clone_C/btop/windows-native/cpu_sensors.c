/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
static PDH_HQUERY query;
static PDH_HCOUNTER frequency, cores;
static int attempted, primed;
static void cpu_sensor(cJSON *a,const char *name,const char *unit,double value) {
    cJSON *o=cJSON_CreateObject();char id[256];
    snprintf(id,sizeof(id),"cpu/%s",name);
    json_text(o,"id",id);json_text(o,"hardware","Windows CPU");
    json_text(o,"kind","Cpu");json_text(o,"name",name);json_text(o,"unit",unit);
    json_number(o,"value",value);cJSON_AddItemToArray(a,o);
}
void collect_cpu_sensors(cJSON *a) {
    cpu_sensor(a,"CPU Package Temperature","C",-1);
    cpu_sensor(a,"CPU Package Power","W",-1);
    if(!attempted) {
        attempted=1;
        if(PdhOpenQueryW(NULL,0,&query)==ERROR_SUCCESS) {
            if(PdhAddEnglishCounterW(query,L"\\Processor Information(_Total)\\Processor Frequency",0,&frequency)!=ERROR_SUCCESS)
                frequency=NULL;
            if(PdhAddEnglishCounterW(query,L"\\Processor Information(*)\\% Processor Time",0,&cores)!=ERROR_SUCCESS)
                cores=NULL;
        }
    }
    double mhz=-1;
    if(query && PdhCollectQueryData(query)==ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE v;
        if(primed && frequency && PdhGetFormattedCounterValue(frequency,PDH_FMT_DOUBLE,NULL,&v)==ERROR_SUCCESS &&
           (v.CStatus==PDH_CSTATUS_VALID_DATA || v.CStatus==PDH_CSTATUS_NEW_DATA)) mhz=v.doubleValue;
        if(primed && cores) {
            DWORD bytes=0,count=0;
            if(PdhGetFormattedCounterArrayW(cores,PDH_FMT_DOUBLE,&bytes,&count,NULL)==(PDH_STATUS)PDH_MORE_DATA && bytes<4*1024*1024) {
                PDH_FMT_COUNTERVALUE_ITEM_W *items=malloc(bytes);
                if(items && PdhGetFormattedCounterArrayW(cores,PDH_FMT_DOUBLE,&bytes,&count,items)==ERROR_SUCCESS) {
                    for(DWORD i=0;i<count && i<256;i++) {
                        if(wcsstr(items[i].szName,L"_Total")) continue;
                        char name[160],instance[96];utf8(items[i].szName,instance,sizeof(instance));
                        snprintf(name,sizeof(name),"CPU %s Load",instance);
                        double value=-1;
                        if(items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA || items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA)
                            value=items[i].FmtValue.doubleValue;
                        cpu_sensor(a,name,"%",value);
                    }
                }
                free(items);
            }
        }
        primed=1;
    }
    cpu_sensor(a,"Windows Reported Frequency","MHz",mhz);
}
void cpu_sensors_shutdown(void) {
    if(query) {PdhCloseQuery(query);query=NULL;}
}
