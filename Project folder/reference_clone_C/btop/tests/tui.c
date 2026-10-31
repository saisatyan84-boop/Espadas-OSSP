/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <pty.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int master;
static char output[200000];static size_t used;
static double milliseconds(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec*1000.0+t.tv_nsec/1e6;
}
static void drain(int ms) {
    struct pollfd p={master,POLLIN,0};
    double end=milliseconds()+ms;
    while(milliseconds()<end) {
        if(poll(&p,1,50)>0 && used+1024<sizeof(output)) {
            ssize_t n=read(master,output+used,1024);
            if(n>0) {used+=(size_t)n;output[used]=0;}
        }
    }
}
int main(void) {
    struct winsize size={40,160,0,0};
    pid_t child=forkpty(&master,NULL,NULL,&size);assert(child>=0);
    if(!child) {
        setenv("TERM","xterm-256color",1);setenv("WINTOP_WINDOWS","0",1);
        execl("./bin/wintop","./bin/wintop",(char*)NULL);_exit(127);
    }
    drain(1500);assert(strstr(output,"WINTOP"));
    assert(write(master,"P",1)==1);drain(800);
    assert(strstr(output,"WINDOWS PROCESSES")&&strstr(output,"WSL / LINUX PROCESSES"));
    assert(write(master,"SduvPcmpr",9)==9);drain(700);
    size.ws_row=20;size.ws_col=60;assert(ioctl(master,TIOCSWINSZ,&size)==0);
    kill(child,SIGWINCH);drain(500);
    assert(strstr(output,"enlarge terminal"));
    assert(write(master,"q",1)==1);
    int status=0,done=0;
    for(int i=0;i<50;i++) { if(waitpid(child,&status,WNOHANG)==child) {done=1;break;} usleep(100000); }
    if(!done) kill(child,SIGKILL);
    assert(done&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    close(master);puts("C TUI controls, resize and shutdown passed");return 0;
}
