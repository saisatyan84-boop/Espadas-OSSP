// SPDX-License-Identifier: Apache-2.0
#ifdef __linux__
#include "host/child_process.hpp"
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>
extern char** environ;
namespace ipc {
ChildProcess::~ChildProcess() { stop(); }
bool ChildProcess::start(const std::vector<std::string>& args, std::string& error) {
    stop();
    if(args.empty()) { error="Empty executable"; return false; }
    int output[2], diagnostic[2];
    if(pipe2(output,O_CLOEXEC)<0) { error=strerror(errno); return false; }
    if(pipe2(diagnostic,O_CLOEXEC)<0) {
        error=strerror(errno); close(output[0]); close(output[1]); return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,output[1],STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions,diagnostic[1],STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr,0);
    std::vector<char*> argv;
    for(const auto& a:args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    int result=posix_spawnp(&pid_,argv[0],&actions,&attr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr);
    close(output[1]); close(diagnostic[1]);
    if(result) {
        close(output[0]); close(diagnostic[0]); pid_=-1;
        error="Cannot launch Windows producer: "+std::string(strerror(result)); return false;
    }
    out_=output[0]; err_=diagnostic[0];
    fcntl(out_,F_SETFL,fcntl(out_,F_GETFL)|O_NONBLOCK);
    fcntl(err_,F_SETFL,fcntl(err_,F_GETFL)|O_NONBLOCK);
    pending_.clear(); diagnostics_.clear();
    return true;
}
ReadResult ChildProcess::read() {
    ReadResult result;
    if(out_<0) { result.ended=true; return result; }
    char buffer[16384];
    // Budget each poll so an overactive producer cannot monopolize the TUI.
    for(size_t budget=0;budget<256*1024;) {
        ssize_t n=::read(out_,buffer,sizeof(buffer));
        if(n>0) {
            budget+=n; pending_.append(buffer,n);
            size_t newline;
            while((newline=pending_.find('\n'))!=std::string::npos) {
                if(newline>maxFrameBytes) { result.error="Telemetry frame too large"; return result; }
                std::string line=pending_.substr(0,newline);
                pending_.erase(0,newline+1);
                if(!line.empty() && line.back()=='\r') line.pop_back();
                if(!line.empty()) result.lines.push_back(std::move(line));
            }
            if(pending_.size()>maxFrameBytes) { result.error="Telemetry frame too large"; return result; }
        } else if(n==0) { result.ended=true; break; }
        else if(errno==EINTR) continue;
        else if(errno==EAGAIN || errno==EWOULDBLOCK) break;
        else { result.error="Windows stdout read failed"; break; }
    }
    for(int i=0;i<4;++i) {
        ssize_t n=::read(err_,buffer,sizeof(buffer));
        if(n<=0) break;
        diagnostics_.append(buffer,n);
        if(diagnostics_.size()>2048) diagnostics_.erase(0,diagnostics_.size()-2048);
    }
    if(result.ended) {
        result.error=!pending_.empty() ? "Incomplete Windows telemetry frame" :
            diagnostics_.empty() ? "Windows producer exited" : diagnostics_;
    }
    return result;
}
void ChildProcess::stop() {
    if(out_>=0) close(out_);
    if(err_>=0) close(err_);
    out_=err_=-1;
    if(pid_>0) {
        // Group signals also clean up children of a future producer.
        kill(-pid_,SIGTERM);
        for(int i=0;i<20;++i) {
            pid_t state=waitpid(pid_,nullptr,WNOHANG);
            if(state==pid_ || (state<0 && errno==ECHILD)) { pid_=-1; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if(pid_>0) {
            kill(-pid_,SIGKILL);
            while(waitpid(pid_,nullptr,0)<0 && errno==EINTR) {}
        }
    }
    pid_=-1; pending_.clear(); diagnostics_.clear();
}
}

#endif
