/* SPDX-License-Identifier: MIT */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
static const char *connector(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY type) {
    switch(type) {
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:return "HDMI";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:return "DisplayPort (USB-C cannot be inferred)";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:return "Internal eDP";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:return "Internal";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:return "Miracast";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:return "DVI";
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15:return "VGA";
        default:return "Unknown connection";
    }
}
void collect_displays(cJSON *a) {
    for(int attempt=0;attempt<3;attempt++) {
        UINT32 paths=0,modes=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)!=ERROR_SUCCESS) return;
        DISPLAYCONFIG_PATH_INFO *p=calloc(paths?paths:1,sizeof(*p));
        DISPLAYCONFIG_MODE_INFO *m=calloc(modes?modes:1,sizeof(*m));
        if(!p || !m) { free(p);free(m);return; }
        LONG result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,p,&modes,m,NULL);
        if(result==ERROR_INSUFFICIENT_BUFFER) {free(p);free(m);continue;}
        if(result==ERROR_SUCCESS) for(UINT32 i=0;i<paths;i++) {
            DISPLAYCONFIG_TARGET_DEVICE_NAME n; ZeroMemory(&n,sizeof(n));
            n.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME; n.header.size=sizeof(n);
            n.header.adapterId=p[i].targetInfo.adapterId; n.header.id=p[i].targetInfo.id;
            char name[1024]="Monitor",id[1024],detail[256];
            if(DisplayConfigGetDeviceInfo(&n.header)==ERROR_SUCCESS) {
                utf8(n.monitorFriendlyDeviceName,name,sizeof(name));
                utf8(n.monitorDevicePath,id,sizeof(id));
            } else snprintf(id,sizeof(id),"display:%ld:%u",(long)p[i].targetInfo.adapterId.LowPart,p[i].targetInfo.id);
            UINT32 w=0,h=0,index=p[i].sourceInfo.modeInfoIdx;
            if(index<modes && m[index].infoType==DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
                w=m[index].sourceMode.width;h=m[index].sourceMode.height;
            }
            double hz=p[i].targetInfo.refreshRate.Denominator?
                (double)p[i].targetInfo.refreshRate.Numerator/p[i].targetInfo.refreshRate.Denominator:0;
            snprintf(detail,sizeof(detail),"%ux%u %.2f Hz | %s",w,h,hz,connector(p[i].targetInfo.outputTechnology));
            device(a,id,"Display",name[0]?name:"Monitor","Active",detail);
        }
        free(p);free(m);return;
    }
}
