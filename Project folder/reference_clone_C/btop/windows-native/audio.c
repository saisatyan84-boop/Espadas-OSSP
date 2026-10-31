/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <mmdeviceapi.h>
#include <propsys.h>
#include <stdio.h>
/* COM interface identifiers from the Windows SDK. */
static const GUID clsid_enumerator={0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID iid_enumerator={0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const PROPERTYKEY friendly={{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
void collect_audio(cJSON *a) {
    HRESULT initialized=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(FAILED(initialized)) return;
    IMMDeviceEnumerator *en=NULL;
    if(SUCCEEDED(CoCreateInstance(&clsid_enumerator,NULL,CLSCTX_INPROC_SERVER,&iid_enumerator,(void**)&en))) {
        for(int flow=eRender;flow<=eCapture;flow++) {
            IMMDevice *def=NULL; LPWSTR default_id=NULL;
            if(SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en,(EDataFlow)flow,eConsole,&def))) {
                IMMDevice_GetId(def,&default_id); IMMDevice_Release(def);
            }
            IMMDeviceCollection *list=NULL;
            if(SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(en,(EDataFlow)flow,DEVICE_STATE_ACTIVE,&list))) {
                UINT count=0; IMMDeviceCollection_GetCount(list,&count);
                for(UINT i=0;i<count;i++) {
                    IMMDevice *d=NULL;
                    if(FAILED(IMMDeviceCollection_Item(list,i,&d))) continue;
                    LPWSTR id=NULL; IMMDevice_GetId(d,&id);
                    char text[2048]="Audio endpoint",uid[2048]="",detail[96];
                    IPropertyStore *store=NULL;
                    if(SUCCEEDED(IMMDevice_OpenPropertyStore(d,STGM_READ,&store))) {
                        PROPVARIANT value; PropVariantInit(&value);
                        if(SUCCEEDED(IPropertyStore_GetValue(store,&friendly,&value)) && value.vt==VT_LPWSTR)
                            utf8(value.pwszVal,text,sizeof(text));
                        PropVariantClear(&value);IPropertyStore_Release(store);
                    }
                    utf8(id,uid,sizeof(uid));
                    int is_default=id && default_id && !wcscmp(id,default_id);
                    snprintf(detail,sizeof(detail),"%s%s",flow==eRender?"Output":"Microphone",is_default?" | Default console endpoint":"");
                    device(a,uid,"Audio",text,"Active",detail);
                    CoTaskMemFree(id);IMMDevice_Release(d);
                }
                IMMDeviceCollection_Release(list);
            }
            CoTaskMemFree(default_id);
        }
        IMMDeviceEnumerator_Release(en);
    }
    CoUninitialize();
}
