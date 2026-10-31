/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <ws2tcpip.h>
#include <stdio.h>

typedef struct { NET_LUID id; ULONG64 rx,tx; double sample; } Track;
static Track previous[256]; static unsigned count;
cJSON *collect_network(double sample) {
    cJSON *a=cJSON_CreateArray(); PMIB_IF_TABLE2 table=NULL;
    if (GetIfTable2(&table)!=NO_ERROR) return a;
    ULONG len=16384; IP_ADAPTER_ADDRESSES *addresses=malloc(len);
    ULONG result=addresses?GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,NULL,addresses,&len):ERROR_OUTOFMEMORY;
    if (result==ERROR_BUFFER_OVERFLOW) {
        free(addresses); addresses=malloc(len);
        result=addresses?GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,NULL,addresses,&len):ERROR_OUTOFMEMORY;
    }
    for (ULONG i=0;i<table->NumEntries;i++) {
        MIB_IF_ROW2 *r=&table->Table[i];
        if (r->Type==IF_TYPE_SOFTWARE_LOOPBACK || !r->InterfaceAndOperStatusFlags.HardwareInterface) continue;
        cJSON *o=cJSON_CreateObject(); char name[1024],id[40],ip[128]="";
        utf8(r->Alias,name,sizeof(name)); snprintf(id,sizeof(id),"%llu",(unsigned long long)r->InterfaceLuid.Value);
        json_text(o,"id",id); json_text(o,"name",name);
        json_text(o,"kind",r->Type==IF_TYPE_IEEE80211?"Wi-Fi":r->Type==IF_TYPE_ETHERNET_CSMACD?"Ethernet":"Network");
        json_text(o,"state",r->OperStatus==IfOperStatusUp?"Up":"Down");
        cJSON_AddBoolToObject(o,"virtual",!r->InterfaceAndOperStatusFlags.HardwareInterface);
        if (result==NO_ERROR) for (IP_ADAPTER_ADDRESSES *ad=addresses;ad;ad=ad->Next) {
            if (ad->Luid.Value!=r->InterfaceLuid.Value) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS *u=ad->FirstUnicastAddress;u;u=u->Next) {
                if (u->Address.lpSockaddr->sa_family==AF_INET) {
                    struct sockaddr_in *v=(struct sockaddr_in*)u->Address.lpSockaddr;
                    InetNtopA(AF_INET,&v->sin_addr,ip,sizeof(ip)); break;
                }
            }
        }
        json_text(o,"address",ip);
        unsigned j; for(j=0;j<count;j++) if(previous[j].id.Value==r->InterfaceLuid.Value) break;
        double rx=-1,tx=-1;
        if (j<count && sample>previous[j].sample && r->InOctets>=previous[j].rx && r->OutOctets>=previous[j].tx) {
            double dt=sample-previous[j].sample;
            rx=(r->InOctets-previous[j].rx)/dt; tx=(r->OutOctets-previous[j].tx)/dt;
        }
        json_number(o,"rx",rx); json_number(o,"tx",tx);
        if (j<256) {
            if (j==count) count++;
            previous[j]=(Track){r->InterfaceLuid,r->InOctets,r->OutOctets,sample};
        }
        cJSON_AddItemToArray(a,o);
    }
    free(addresses); FreeMibTable(table); return a;
}
