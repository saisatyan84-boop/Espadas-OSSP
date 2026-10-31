/* SPDX-License-Identifier: MIT */
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *line=NULL;size_t cap=0;int count=0;
    while(getline(&line,&cap,stdin)>0) {
        cJSON *root=cJSON_Parse(line);assert(root);
        cJSON *source=cJSON_GetObjectItem(root,"source");assert(cJSON_IsString(source)&&!strcmp(source->valuestring,"windows"));
        cJSON *host=cJSON_GetObjectItem(root,"host");assert(cJSON_IsObject(host));
        cJSON *mem=cJSON_GetObjectItem(host,"memory");
        cJSON *total=cJSON_GetObjectItem(mem,"total"),*avail=cJSON_GetObjectItem(mem,"available");
        assert(cJSON_IsNumber(total)&&cJSON_IsNumber(avail)&&total->valuedouble>0&&avail->valuedouble<=total->valuedouble);
        cJSON *procs=cJSON_GetObjectItem(root,"processes");assert(cJSON_IsArray(procs)&&cJSON_GetArraySize(procs)>0);
        if(count>0) assert(cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(host,"cpu"),"usage")));
        cJSON_Delete(root);count++;
    }
    free(line);assert(count>=2);printf("Verified %d live native C Windows frames\n",count);return 0;
}
