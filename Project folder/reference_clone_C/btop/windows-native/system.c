/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>

static ULONGLONG file_ticks(FILETIME f) {
    ULARGE_INTEGER n; n.LowPart=f.dwLowDateTime; n.HighPart=f.dwHighDateTime; return n.QuadPart;
}
cJSON *collect_cpu(void) {
    static ULONGLONG previous_total, previous_idle;
    cJSON *o=cJSON_CreateObject();
    char name[256]="Windows processor"; DWORD size=sizeof(name);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",0,KEY_READ,&key)==ERROR_SUCCESS) {
        RegQueryValueExA(key,"ProcessorNameString",NULL,NULL,(BYTE*)name,&size);
        name[sizeof(name)-1]=0; RegCloseKey(key);
    }
    json_text(o,"name",name);
    DWORD n=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    cJSON_AddNumberToObject(o,"logical_processors",n);
    json_number(o,"uptime_seconds",GetTickCount64()/1000.0);
    FILETIME idle,kernel,user; double use=-1;
    if (GetActiveProcessorGroupCount()==1 && GetSystemTimes(&idle,&kernel,&user)) {
        ULONGLONG total=file_ticks(kernel)+file_ticks(user), id=file_ticks(idle);
        if (previous_total && total>previous_total && id>=previous_idle && id-previous_idle<=total-previous_total)
            use=100.0*(double)((total-previous_total)-(id-previous_idle))/(double)(total-previous_total);
        previous_total=total; previous_idle=id;
    }
    json_number(o,"usage",use); return o;
}
cJSON *collect_memory(void) {
    cJSON *o=cJSON_CreateObject();
    MEMORYSTATUSEX m; ZeroMemory(&m,sizeof(m)); m.dwLength=sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
        json_number(o,"total",(double)m.ullTotalPhys);
        json_number(o,"available",(double)m.ullAvailPhys);
    } else { json_number(o,"total",-1); json_number(o,"available",-1); }
    return o;
}
cJSON *collect_processes(void) {
    cJSON *a=cJSON_CreateArray();
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snap==INVALID_HANDLE_VALUE) return a;
    PROCESSENTRY32W p; ZeroMemory(&p,sizeof(p)); p.dwSize=sizeof(p);
    if (Process32FirstW(snap,&p)) do {
        if (!p.th32ProcessID) continue;
        cJSON *o=cJSON_CreateObject(); char name[1024]; utf8(p.szExeFile,name,sizeof(name));
        cJSON_AddNumberToObject(o,"pid",p.th32ProcessID);
        cJSON_AddNumberToObject(o,"parent_pid",p.th32ParentProcessID);
        json_text(o,"name",name);
        HANDLE proc=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,p.th32ProcessID);
        if (!proc) proc=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID);
        FILETIME created,exited,kernel,user;
        if (proc && GetProcessTimes(proc,&created,&exited,&kernel,&user)) {
            json_number(o,"cpu_seconds",(file_ticks(kernel)+file_ticks(user))/10000000.0);
            /* Creation time lets readers distinguish PID reuse. */
            json_number(o,"start_seconds",file_ticks(created)/10000000.0);
        } else cJSON_AddNullToObject(o,"cpu_seconds");
        PROCESS_MEMORY_COUNTERS m; ZeroMemory(&m,sizeof(m)); m.cb=sizeof(m);
        if (proc && GetProcessMemoryInfo(proc,&m,sizeof(m))) json_number(o,"ram_bytes",(double)m.WorkingSetSize);
        else cJSON_AddNullToObject(o,"ram_bytes");
        if (proc) CloseHandle(proc);
        cJSON_AddItemToArray(a,o);
    } while (Process32NextW(snap,&p));
    CloseHandle(snap); return a;
}
