/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
void collect_drives(cJSON *a) {
    WCHAR roots[1024];
    DWORD length=GetLogicalDriveStringsW(1024,roots);
    if(!length || length>=1024) return;
    for(WCHAR *p=roots;*p;p+=wcslen(p)+1) {
        UINT kind=GetDriveTypeW(p);
        if(kind!=DRIVE_FIXED && kind!=DRIVE_REMOVABLE) continue;
        ULARGE_INTEGER free,total;
        if(!GetDiskFreeSpaceExW(p,NULL,&total,&free)) continue;
        char name[64];utf8(p,name,sizeof(name));
        cJSON *o=cJSON_CreateObject();
        json_text(o,"name",name);json_text(o,"kind",kind==DRIVE_FIXED?"Fixed":"Removable");
        json_number(o,"total",(double)total.QuadPart);json_number(o,"free",(double)free.QuadPart);
        cJSON_AddItemToArray(a,o);
    }
}
void collect_disk_io(cJSON *o) {
    static PDH_HQUERY query;static PDH_HCOUNTER read_counter,write_counter;
    static int attempted,primed;
    if(!attempted) {
        attempted=1;
        if(PdhOpenQueryW(NULL,0,&query)==ERROR_SUCCESS) {
            if(PdhAddEnglishCounterW(query,L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",0,&read_counter)!=ERROR_SUCCESS ||
               PdhAddEnglishCounterW(query,L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",0,&write_counter)!=ERROR_SUCCESS) {
                PdhCloseQuery(query);query=NULL;
            }
        }
    }
    double read=-1,write=-1;
    if(query && PdhCollectQueryData(query)==ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE v;
        if(primed && PdhGetFormattedCounterValue(read_counter,PDH_FMT_DOUBLE,NULL,&v)==ERROR_SUCCESS &&
           (v.CStatus==PDH_CSTATUS_VALID_DATA || v.CStatus==PDH_CSTATUS_NEW_DATA)) read=v.doubleValue;
        if(primed && PdhGetFormattedCounterValue(write_counter,PDH_FMT_DOUBLE,NULL,&v)==ERROR_SUCCESS &&
           (v.CStatus==PDH_CSTATUS_VALID_DATA || v.CStatus==PDH_CSTATUS_NEW_DATA)) write=v.doubleValue;
        primed=1;
    }
    json_number(o,"disk_read",read);json_number(o,"disk_write",write);
}
