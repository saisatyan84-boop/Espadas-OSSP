/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <setupapi.h>
#include <bluetoothapis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void collect_bluetooth(cJSON *array);
void collect_displays(cJSON *array);
void collect_audio(cJSON *array);
void collect_drives(cJSON *array);
void collect_disk_io(cJSON *inventory);
static void property(HDEVINFO set,SP_DEVINFO_DATA *data,DWORD key,char *text,size_t cap) {
    WCHAR value[1024]={0}; DWORD type;
    if(SetupDiGetDeviceRegistryPropertyW(set,data,key,&type,(BYTE*)value,sizeof(value),NULL))
        utf8(value,text,(int)cap);
    else text[0]=0;
}
cJSON *collect_inventory(void) {
    cJSON *o=cJSON_CreateObject(), *a=cJSON_CreateArray(), *drives=cJSON_CreateArray();
    cJSON_AddItemToObject(o,"devices",a); cJSON_AddItemToObject(o,"drives",drives);
    json_text(o,"status","Native Win32 inventory; Bluetooth battery N/A; cached up to 10s");
    HDEVINFO set=SetupDiGetClassDevsW(NULL,NULL,NULL,DIGCF_ALLCLASSES|DIGCF_PRESENT);
    if(set!=INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA d; ZeroMemory(&d,sizeof(d)); d.cbSize=sizeof(d);
        for(DWORD i=0;SetupDiEnumDeviceInfo(set,i,&d);i++) {
            WCHAR wid[1024]; char id[2048],name[2048],cls[256],hardware[2048];
            if(!SetupDiGetDeviceInstanceIdW(set,&d,wid,1024,NULL)) continue;
            utf8(wid,id,sizeof(id));
            property(set,&d,SPDRP_CLASS,cls,sizeof(cls));
            property(set,&d,SPDRP_FRIENDLYNAME,name,sizeof(name));
            if(!name[0]) property(set,&d,SPDRP_DEVICEDESC,name,sizeof(name));
            property(set,&d,SPDRP_HARDWAREID,hardware,sizeof(hardware));
            const char *category=NULL;
            if(!_stricmp(cls,"AudioEndpoint") || !_stricmp(cls,"MEDIA")) category="Audio hardware";
            else if(!_stricmp(cls,"Bluetooth")) category="Bluetooth hardware";
            else if(!_stricmp(cls,"Net")) category="Network";
            else if(!_stricmp(cls,"DiskDrive") || !_stricmp(cls,"Volume")) category="Storage";
            else if(!_stricmp(cls,"Keyboard") || !_stricmp(cls,"Mouse") || !_stricmp(cls,"HIDClass")) category="Input";
            else if(!_stricmp(cls,"USB") || !strncmp(id,"USB\\",4)) category="USB";
            else if(!_stricmp(cls,"Camera") || !_stricmp(cls,"Image")) category="Camera";
            if(category && cJSON_GetArraySize(a)<2048)
                device(a,id,category,name[0]?name:cls,"Present",hardware);
        }
        SetupDiDestroyDeviceInfoList(set);
    }
    collect_bluetooth(a); collect_displays(a); collect_audio(a);
    collect_drives(drives); collect_disk_io(o);
    return o;
}
void collect_bluetooth(cJSON *a) {
    BLUETOOTH_DEVICE_SEARCH_PARAMS search; ZeroMemory(&search,sizeof(search));
    search.dwSize=sizeof(search);
    search.fReturnAuthenticated=TRUE; search.fReturnRemembered=TRUE;
    search.fReturnConnected=TRUE;
    /* Passive cache enumeration: no inquiry, GATT operations, or device connections. */
    search.fIssueInquiry=FALSE;
    BLUETOOTH_DEVICE_INFO d; ZeroMemory(&d,sizeof(d)); d.dwSize=sizeof(d);
    HBLUETOOTH_DEVICE_FIND find=BluetoothFindFirstDevice(&search,&d);
    if(!find) return;
    do {
        char name[1024],id[64],detail[128]; utf8(d.szName,name,sizeof(name));
        snprintf(id,sizeof(id),"BT:%012llX",(unsigned long long)d.Address.ullLong);
        snprintf(detail,sizeof(detail),"Classic Bluetooth; class 0x%06lx; battery N/A",(unsigned long)d.ulClassofDevice);
        device(a,id,"Bluetooth",name,d.fConnected?"Connected":d.fAuthenticated?"Paired":"Disconnected",detail);
        d.dwSize=sizeof(d);
    } while(BluetoothFindNextDevice(find,&d));
    BluetoothFindDeviceClose(find);
}
