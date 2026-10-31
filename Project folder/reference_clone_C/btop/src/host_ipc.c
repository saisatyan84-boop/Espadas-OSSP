/* SPDX-License-Identifier: MIT */
#include "host_ipc.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/wait.h>
#include <limits.h>
#define FRAME_MAX (4*1024*1024)
typedef struct {int pid; double cpu,sample,start;} Track;
static Track tracks[MAX_PROCESSES];
static int track_count;
static pid_t child=-1;
static int fd=-1,enabled,dropping;
static char path[PATH_MAX],*buffer;
static size_t used;
static double last_frame,retry_at;
static double seconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec+t.tv_nsec/1e9;
}
static cJSON *item(cJSON *o,const char *key) {return cJSON_GetObjectItemCaseSensitive(o,key);}
static double number(cJSON *o,const char *key,double fallback,double max) {
    cJSON *v=item(o,key);
    return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=0&&v->valuedouble<=max?v->valuedouble:fallback;
}
static void text(cJSON *o,const char *key,char *out,size_t size) {
    cJSON *v=item(o,key); const char *p=cJSON_IsString(v)?v->valuestring:"";
    size_t i=0;
    for(;p[i]&&i+1<size;i++) out[i]=(unsigned char)p[i]<32 || (unsigned char)p[i]==127?'?':p[i];
    out[i]=0;
}
#define TEXT(o,key,dest) text(o,key,dest,sizeof(dest))
static void parse(const char *line) {
    const char *end=NULL;
    cJSON *r=cJSON_ParseWithOpts(line,&end,1);
    if(!r) return;
    cJSON *source=item(r,"source"),*host=item(r,"host");
    double sample=number(r,"sample_seconds",-1,1e12);
    if(number(r,"schema_version",0,100)!=1 || !cJSON_IsString(source) ||
       strcmp(source->valuestring,"windows") || !cJSON_IsObject(host) || sample<0) {cJSON_Delete(r);return;}
    cJSON *cpu=item(host,"cpu"),*memory=item(host,"memory"),*processes=item(r,"processes");
    if(!cJSON_IsObject(cpu)||!cJSON_IsObject(memory)||!cJSON_IsArray(processes)) {cJSON_Delete(r);return;}
    double total=number(memory,"total",-1,1e17),avail=number(memory,"available",-1,1e17);
    if(total<=0 || avail<0 || avail>total) {cJSON_Delete(r);return;}
    HostData *next=calloc(1,sizeof(*next));if(!next) {cJSON_Delete(r);return;}
    next->sample_seconds=sample;next->available=true;
    TEXT(cpu,"name",next->cpu_name);
    next->cpu_usage=number(cpu,"usage",NAN,100);
    next->logical_processors=(int)number(cpu,"logical_processors",1,65536);
    next->uptime_seconds=number(cpu,"uptime_seconds",0,1e12);
    next->mem_total_bytes=total;next->mem_avail_bytes=avail;
    next->selected_network=g_host.selected_network;
    next->disk_read_rate=next->disk_write_rate=NAN;
    cJSON *v; cJSON_ArrayForEach(v,item(host,"networks")) {
        if(!cJSON_IsObject(v)||next->network_count>=MAX_NETWORKS) continue;
        NetworkInfo *n=&next->networks[next->network_count++];
        TEXT(v,"id",n->id);TEXT(v,"name",n->name);TEXT(v,"kind",n->kind);
        TEXT(v,"state",n->state);TEXT(v,"address",n->address);
        n->rx_rate=number(v,"rx",NAN,1e17);n->tx_rate=number(v,"tx",NAN,1e17);
    }
    if(next->selected_network>=next->network_count) next->selected_network=0;
    if(!g_host.network_count) for(int i=0;i<next->network_count;i++) {
        if(!strcmp(next->networks[i].state,"Up")) {next->selected_network=i;break;}
    }
    cJSON *inventory=item(host,"inventory");
    TEXT(inventory,"status",next->inventory_status);
    next->disk_read_rate=number(inventory,"disk_read",NAN,1e17);
    next->disk_write_rate=number(inventory,"disk_write",NAN,1e17);
    cJSON_ArrayForEach(v,item(inventory,"drives")) {
        if(!cJSON_IsObject(v)||next->drive_count>=MAX_DRIVES) continue;
        double t=number(v,"total",-1,1e20),f=number(v,"free",-1,1e20);
        if(t<0||f<0||f>t) continue;
        DriveInfo *d=&next->drives[next->drive_count++];
        TEXT(v,"name",d->name);TEXT(v,"kind",d->kind);
        d->total_bytes=t;d->free_bytes=f;
    }
    cJSON_ArrayForEach(v,item(inventory,"devices")) {
        if(!cJSON_IsObject(v)||next->device_count>=MAX_DEVICES) continue;
        DeviceInfo *d=&next->devices[next->device_count++];
        TEXT(v,"id",d->id);TEXT(v,"category",d->category);TEXT(v,"name",d->name);
        TEXT(v,"state",d->state);TEXT(v,"detail",d->detail);
        d->battery=number(v,"battery",-1,100);
    }
    cJSON *hardware=item(host,"hardware");TEXT(hardware,"status",next->sensor_status);
    cJSON_ArrayForEach(v,item(hardware,"sensors")) {
        if(!cJSON_IsObject(v)||next->sensor_count>=MAX_SENSORS) continue;
        SensorInfo *s=&next->sensors[next->sensor_count++];
        TEXT(v,"id",s->id);TEXT(v,"hardware",s->hardware);TEXT(v,"kind",s->kind);
        TEXT(v,"name",s->name);TEXT(v,"unit",s->unit);
        cJSON *value=item(v,"value");s->has_value=cJSON_IsNumber(value)&&isfinite(value->valuedouble);
        if(s->has_value) s->value=value->valuedouble;
    }
    Track fresh[MAX_PROCESSES];int fresh_count=0;
    cJSON_ArrayForEach(v,processes) {
        if(!cJSON_IsObject(v)||next->process_count>=MAX_PROCESSES) continue;
        double pid=number(v,"pid",-1,INT_MAX);
        if(pid<1 || floor(pid)!=pid) continue;
        ProcessInfo *p=&next->processes[next->process_count++];
        p->pid=(int)pid;TEXT(v,"name",p->name);strcpy(p->user,"Windows");
        double ram=number(v,"ram_bytes",-1,1e17);
        p->mem_available=ram>=0;if(p->mem_available) p->ram_mb=ram/1048576.0;
        double cpu_sec=number(v,"cpu_seconds",-1,1e15),start=number(v,"start_seconds",0,1e15);
        if(cpu_sec>=0) {
            for(int j=0;j<track_count;j++) if(tracks[j].pid==p->pid && tracks[j].start==start) {
                double dt=sample-tracks[j].sample,delta=cpu_sec-tracks[j].cpu;
                if(dt>0 && delta>=0) {
                    p->cpu_pct=fmin(100,100*delta/dt/(next->logical_processors?next->logical_processors:1));
                    p->cpu_available=true;
                }
                break;
            }
            fresh[fresh_count++]=(Track){p->pid,cpu_sec,sample,start};
        }
    }
    track_count=fresh_count;memcpy(tracks,fresh,sizeof(Track)*fresh_count);
    next->cpu_hist_count=g_host.cpu_hist_count;
    memcpy(next->cpu_history,g_host.cpu_history,sizeof(next->cpu_history));
    if(isfinite(next->cpu_usage)) {
        if(next->cpu_hist_count==CPU_HISTORY_LEN) {
            memmove(next->cpu_history,next->cpu_history+1,sizeof(double)*(CPU_HISTORY_LEN-1));next->cpu_hist_count--;
        }
        next->cpu_history[next->cpu_hist_count++]=next->cpu_usage;
    }
    g_host=*next;free(next);cJSON_Delete(r);
    g_state.agent_connected=true;last_frame=seconds();
}
static void reap(void) {
    if(child>0 && waitpid(child,NULL,WNOHANG)==child) child=-1;
}
static void launch(void) {
    int pipes[2];if(pipe(pipes)<0) return;
    fcntl(pipes[0],F_SETFD,FD_CLOEXEC);fcntl(pipes[1],F_SETFD,FD_CLOEXEC);
    child=fork();
    if(child==0) {
        setpgid(0,0);close(pipes[0]);
        dup2(pipes[1],STDOUT_FILENO);close(pipes[1]);
        int null=open("/dev/null",O_RDWR);
        if(null>=0) {dup2(null,STDIN_FILENO);dup2(null,STDERR_FILENO);close(null);}
        char interval[32];snprintf(interval,sizeof(interval),"%d",g_state.refresh_ms?g_state.refresh_ms:2000);
        execl(path,path,"--stream","--interval-ms",interval,(char*)NULL);_exit(127);
    }
    close(pipes[1]);
    if(child<0) {close(pipes[0]);return;}
    setpgid(child,child);fd=pipes[0];fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);
    used=0;dropping=0;track_count=0;last_frame=seconds();g_state.agent_running=true;
}
bool host_ipc_start(const char *override) {
    const char *mode=getenv("WINTOP_WINDOWS");
    enabled=!(mode && !strcmp(mode,"0"));
    if(!enabled) {g_state.whole_pc_mode=false;return false;}
    const char *env=getenv("WINTOP_AGENT");
    if(override||env) snprintf(path,sizeof(path),"%s",override?override:env);
    else {
        char exe[PATH_MAX];ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1);
        if(n<0) return false;
        exe[n]=0;char *slash=strrchr(exe,'/');if(!slash) return false;*slash=0;
        slash=strrchr(exe,'/');
        if(slash && !strcmp(slash+1,"bin")) *slash=0;
        if(snprintf(path,sizeof(path),"%s/windows-native/publish/sysmon-host.exe",exe)>=(int)sizeof(path)) return false;
    }
    buffer=malloc(FRAME_MAX+1);if(!buffer) {enabled=0;return false;}
    launch();return fd>=0;
}
void host_ipc_poll(void) {
    if(!enabled) return;
    double now=seconds();reap();
    if(fd<0) {if(child<0 && now>=retry_at) {launch();retry_at=now+3;} return;}
    char data[32768];
    for(int iteration=0;iteration<16;iteration++) {
        ssize_t n=read(fd,data,sizeof(data));
        if(n>0) {
            for(ssize_t i=0;i<n;i++) {
                char c=data[i];
                if(c=='\n') {
                    if(!dropping && used) {buffer[used]=0;parse(buffer);}
                    used=0;dropping=0;
                } else if(!dropping) {
                    if(c==0 || used==FRAME_MAX) {dropping=1;used=0;}
                    else buffer[used++]=c;
                }
            }
        } else if(n<0 && errno==EINTR) continue;
        else if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK)) break;
        else {
            close(fd);fd=-1;g_state.agent_connected=g_state.agent_running=false;
            g_host.available=false;retry_at=now+3;break;
        }
    }
    double stale_after=fmax(10,g_state.refresh_ms/1000.0*3);
    if(fd>=0 && now-last_frame>stale_after) {
        g_state.agent_connected=false;g_host.available=false;
        if(child>0) kill(-child,SIGTERM);
        close(fd);fd=-1;retry_at=now+3;
    }
}
void host_ipc_stop(void) {
    enabled=0;
    if(fd>=0) {close(fd);fd=-1;}
    if(child>0) {
        kill(-child,SIGTERM);
        for(int i=0;i<10 && child>0;i++) {reap();if(child>0) usleep(20000);}
        if(child>0) {kill(-child,SIGKILL);waitpid(child,NULL,0);child=-1;}
    }
    free(buffer);buffer=NULL;g_state.agent_running=g_state.agent_connected=false;
}
