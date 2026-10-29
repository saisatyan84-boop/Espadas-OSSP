// SPDX-License-Identifier: Apache-2.0
// Windows telemetry is independent of Proc::proc_info and Linux PID actions.
#include "btop_host.hpp"
#include "btop_config.hpp"
#include "btop_draw.hpp"
#include "btop_input.hpp"
#include "btop_shared.hpp"
#include "btop_theme.hpp"
#include "btop_tools.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#ifdef __linux__
#include "host/windows_bridge.hpp"
#include "host/ui.hpp"
#endif

namespace Host {
using namespace Tools;
namespace {
int px=0, py=0, pw=0, ph=0, offset=0;
bool focused=false, compact=false, host_view=true;
bool show_paired=false;
int page=0, network_index=0; // processes, devices, sensors
int sort=0; // CPU, working set, PID. Independent from the Linux table.
#ifdef __linux__
WindowsSnapshot snapshot;
std::unique_ptr<WindowsBridge> bridge;
std::map<int,std::deque<long long>> histories;
std::chrono::steady_clock::time_point last_sample{};
int producer_interval=0;
std::deque<long long> cpu_history, memory_history;
std::map<std::string,std::deque<long long>> rx_history, tx_history;
void push(std::deque<long long>& h,double n) { h.push_back(static_cast<long long>(n)); while(h.size()>240) h.pop_front(); }
std::string detail_page() {
    return HostUi::details(snapshot,page,show_paired,offset,px,Proc::y,pw,compact?ph:Proc::height+ph);
}
#endif
std::string safe(std::string text) {
    for(char& c:text) if(static_cast<unsigned char>(c)<32 || c==127) c='?';
    return text;
}
}

bool is_wsl() {
    static const bool value=[] {
        std::string release; std::ifstream("/proc/sys/kernel/osrelease") >> release;
        return release.find("microsoft")!=std::string::npos || release.find("Microsoft")!=std::string::npos;
    }();
    return value;
}
bool enabled() {
#ifdef __linux__
    static const bool value=[] {
        const char* setting=std::getenv("WINTOP_WINDOWS");
        return setting ? std::string_view(setting)!="0" : is_wsl();
    }();
    return value;
#else
    return false;
#endif
}
void reset_layout() {
    px=py=pw=ph=0;

}
void layout(int x,int y,int width,int& linux_height) {
    if(!enabled()) return;
    px=x; pw=width;
    // Keep btop's original process minimum so detailed view/tree remain valid.
    compact=linux_height<Proc::min_height+8;
    if(compact) { py=y; ph=linux_height; }
    else {
        int whole=linux_height;
        linux_height=std::max(Proc::min_height,whole/2);
        py=y+linux_height; ph=whole-linux_height;
    }
}
bool show_linux() { return !enabled() || (page==0 && (!compact || !focused)); }
void collect() {
#ifdef __linux__
    if(!enabled()) return;
    const int interval=std::max(1000,Config::getI("update_ms"));
    if(!bridge || interval!=producer_interval) {
        HostOptions options;
        options.windowsIntervalMs=producer_interval=interval;
        if(const char* exe=std::getenv("WINTOP_POWERSHELL")) { options.powershell=exe; options.prototype=true; }
        if(const char* exe=std::getenv("WINTOP_AGENT")) options.agent=exe;
        bridge=std::make_unique<WindowsBridge>(options);
        histories.clear();
    }
    bridge->poll(snapshot);
    if(snapshot.connected && snapshot.received!=last_sample) {
        const auto& h=snapshot.host;
        if(h.cpuUsage) push(cpu_history,*h.cpuUsage); else cpu_history.clear();
        if(h.memoryTotal && h.memoryAvailable) push(memory_history,100*(*h.memoryTotal-*h.memoryAvailable)/ *h.memoryTotal);
        else memory_history.clear();
        std::set<std::string> interfaces;
        for(const auto& n:h.networks) {
            interfaces.insert(n.id);
            if(n.rx) push(rx_history[n.id],*n.rx); else rx_history[n.id].clear();
            if(n.tx) push(tx_history[n.id],*n.tx); else tx_history[n.id].clear();
        }
        std::erase_if(rx_history,[&](const auto& pair){return !interfaces.contains(pair.first);});
        std::erase_if(tx_history,[&](const auto& pair){return !interfaces.contains(pair.first);});
        std::set<int> live;
        for(const auto& process:snapshot.processes) {
            live.insert(process.pid);
            auto& history=histories[process.pid];
            // The decoder marks new/reused PIDs and inaccessible CPU as N/A.
            if(!process.cpuAvailable) history.clear();
            else {
                history.push_back(std::clamp(static_cast<long long>(process.cpuPct),0LL,100LL));
                while(history.size()>30) history.pop_front();
            }
        }
        for(auto it=histories.begin();it!=histories.end();) {
            if(!live.contains(it->first)) it=histories.erase(it); else ++it;
        }
        last_sample=snapshot.received;
    }
#endif
}
void shutdown() {
#ifdef __linux__
    bridge.reset();
#endif
}

bool key(std::string_view key) {
    if(!enabled()) return false;
    atomic_wait(Runner::active);
    if(Input::last_was_mouse && host_view) {
        const int col=Input::mouse_pos[0],row=Input::mouse_pos[1];
        auto inside=[&](bool shown,int x,int y,int w,int h) { return shown && col>=x && col<x+w && row>=y && row<y+h; };
        // Native controls underneath the host-rendered panels are not visible/clickable.
        if(inside(Cpu::shown,Cpu::x,Cpu::y,Cpu::width,Cpu::height) ||
           inside(Mem::shown,Mem::x,Mem::y,Mem::width,Mem::height) ||
           inside(Net::shown,Net::x,Net::y,Net::width,Net::height)) return true;
    }
    if(!Input::last_was_mouse) {
        if(page==1 && key=="b") { show_paired=!show_paired; offset=0; Runner::run("all",true,true); return true; }
        if(key=="v") { host_view=!host_view; Runner::run("all",true,true); return true; }
        if(key=="[" || key=="]") { network_index+=key=="]"?1:-1; Runner::run("all",true,true); return true; }
        if(Proc::shown && (key=="d" || key=="S" || key=="P")) {
            page=key=="d"?1:key=="S"?2:0; offset=0; focused=true;
            Runner::run("all",true,true); return true;
        }
    }
    if(!Proc::shown) return false;
    const bool mouse=Input::last_was_mouse;
    // Shared with the runner, following btop's existing input synchronization.
    atomic_wait(Runner::active);
    bool host_mouse=false;
    if(mouse && ph>0) {
        const auto col=Input::mouse_pos[0], row=Input::mouse_pos[1];
        host_mouse=col>=px && col<px+pw && row>=(page?Proc::y:py) && row<py+ph && (page || !compact || focused);
        if(!host_mouse && !key.starts_with("mouse_")) {
            // An actual click on a native btop control keeps its original meaning.
            focused=false;
            return false;
        }
        if(host_mouse && !key.starts_with("mouse_")) key="mouse_click";
        if(key=="mouse_click" && host_mouse) focused=true;
        else if(key=="mouse_click" && row>=Proc::y && row<Proc::y+Proc::height &&
                col>=Proc::x && col<Proc::x+Proc::width) focused=false;
    }
    if(key=="tab" || key=="shift_tab") {
        if(page) { page=0; offset=0; focused=false; } else focused=!focused;
        Runner::run("all",true,true);
        return true;
    }
    if(!focused && !host_mouse) return false;
    // Global controls still work. Everything else is confined to the host
    // panel: never pass its keyboard input to kill(), nice or Linux details.
    if(key=="q" || key=="escape" || key=="f1" || key=="?" || key=="H" ||
       key=="f2" || key=="o" || key=="ctrl_r" || key=="+" || key=="-" ||
       (key.size()==1 && key[0]>='0' && key[0]<='9')) return false;
    if(mouse && !host_mouse) return true;
    if(key=="down" || key=="j" || key=="mouse_scroll_down") ++offset;
    else if(key=="up" || key=="k" || key=="mouse_scroll_up") offset=std::max(0,offset-1);
    else if(key=="page_down") offset+=std::max(1,ph-4);
    else if(key=="page_up") offset=std::max(0,offset-std::max(1,ph-4));
    else if(key=="home") offset=0;
    else if(key=="end") offset=100000;
    else if(key=="c") { sort=0; offset=0; }
    else if(key=="m") { sort=1; offset=0; }
    else if(key=="p") { sort=2; offset=0; }
    else if(key=="left") { sort=(sort+2)%3; offset=0; }
    else if(key=="right") { sort=(sort+1)%3; offset=0; }
#ifdef __linux__
    else if(key=="r" && bridge) bridge->reconnect();
#endif
    Runner::run("proc",true,true);
    return true;
}

std::string draw() {
#ifdef __linux__
    if(!enabled() || ph<5 || pw<30) return "";
    if(page) return detail_page();
    if(compact && !focused) return "";
    const auto color=Theme::c(focused?"hi_fg":"proc_box");
    std::string title=focused?"windows processes *":"windows processes";
    auto out=Draw::createBox(px,py,pw,ph,color,true,title);
    const auto status="Host: "+snapshot.status;
    out+=Mv::to(py,px+pw-static_cast<int>(status.size())-3)+color+Symbols::title_left+
        Theme::c(snapshot.connected?"title":"inactive_fg")+status+color+Symbols::title_right;
    auto rows=snapshot.processes;
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b) {
        if(sort==0) {
            if(a.cpuAvailable!=b.cpuAvailable) return a.cpuAvailable;
            if(a.cpuPct!=b.cpuPct) return a.cpuPct>b.cpuPct;
        }
        if(sort==1 && a.memMB!=b.memMB) return a.memMB>b.memMB;
        return a.pid<b.pid;
    });
    const int count=static_cast<int>(rows.size()), visible=ph-4;
    offset=std::clamp(offset,0,std::max(0,count-visible));
    auto label=sort==0?"CPU":sort==1?"RAM":"PID";
    std::string summary=std::to_string(count)+" processes | sort "+label+" | 100% = 1 core";
    if(!snapshot.connected) summary=safe(snapshot.detail);
    out+=Mv::to(py+1,px+2)+Theme::c("main_fg")+uresize(summary,pw-4,true);
    const int name_width=pw-34;
    out+=Mv::to(py+2,px+2)+Theme::c("title")+Fx::b+
        rjust("Pid:",7)+" "+ljust("Program:",name_width)+" "+rjust("RAM",7)+" "+
        ljust("trend",5)+" "+rjust("Cpu%",6)+Fx::ub;
    // The TTY palette has process colors, but no proc/proc_color fading gradients.
    // Use the same availability decision for both row and metric colors.
    const bool proc_gradient=Config::getB("proc_gradient") && !Config::getB("lowcolor") &&
        Theme::gradients.contains("proc") && Theme::gradients.contains("proc_color");
    for(int i=0;i<visible && offset+i<count;++i) {
        const auto& p=rows[offset+i];
        auto fg=proc_gradient
            ? Theme::g("proc").at(std::clamp(i*80/std::max(1,visible),0,100)) : Theme::c("main_fg");
        auto metric_color=fg;
        if(Config::getB("proc_colors") && Theme::gradients.contains("process")) {
            int load=std::clamp(static_cast<int>(p.cpuPct),0,100);
            if(proc_gradient) {
                int shade=load+100-i*80/std::max(1,visible);
                metric_color=shade<100 ? Theme::g("proc_color").at(std::max(0,shade))
                    : Theme::g("process").at(std::min(100,shade-100));
            } else metric_color=Theme::g("process").at(load);
        }
        auto ram=p.memoryAvailable?floating_humanizer(static_cast<uint64_t>(p.memMB*1024*1024),true):"N/A";
        auto cpu=p.cpuAvailable?fmt::format("{:.1f}",p.cpuPct):"N/A";
        std::string graph(5,' ');
        if(p.cpuAvailable && snapshot.connected) {
            auto symbol=Config::getB("tty_mode")?"tty":Config::getS("graph_symbol");
            Draw::Graph spark(5,1,"cpu",histories[p.pid],symbol);
            graph=spark();
        }
        out+=Mv::to(py+3+i,px+2)+fg+rjust(std::to_string(p.pid),7)+" "+metric_color+
            ljust(safe(p.command),name_width,true,true)+" "+rjust(ram,7)+" "+graph+
            metric_color+" "+rjust(cpu,6);
    }
    const std::string footer=focused?"Tab WSL | c/m/p sort | d devices | S sensors":"Tab Windows | d devices | S sensors";
    out+=Mv::to(py+ph-1,px+2)+color+Symbols::title_left_down+
        Theme::c("title")+uresize(footer,pw-8,true)+color+Symbols::title_right_down;
    return out+Fx::reset;
#else
    return "";
#endif
}
std::string scope_labels() {
    if(!enabled()) return "";
    if(host_view) return dashboard();
    std::string out;
    auto badge=[&](int x,int y,int width,const std::string& color,const std::string& label) {
        if(width<static_cast<int>(label.size())+8) return;
        out+=Mv::to(y,x+3)+Theme::c(color)+Symbols::title_left_down+
            Theme::c("title")+label+Theme::c(color)+Symbols::title_right_down;
    };
    auto scope=is_wsl()?"WSL":"Linux";
    std::string cpu_label=std::string(scope)+" CPU | Tab: WSL/Windows";
#ifdef __linux__
    cpu_label=std::string(scope)+" CPU | Windows: "+ljust(snapshot.status,11)+" | v: whole PC";
#endif
    if(Cpu::shown) badge(Cpu::x,Config::getB("cpu_bottom")?Cpu::y:Cpu::y+Cpu::height-1,Cpu::width,"cpu_box",cpu_label);
    if(Mem::shown) badge(Mem::x,Mem::y+Mem::height-1,Mem::width,"mem_box",std::string(scope)+" memory / disks");
    if(Net::shown) badge(Net::x,Net::y+Net::height-1,Net::width,"net_box",std::string(scope)+" network");
    return out+Fx::reset;
}
std::string dashboard() {
#ifdef __linux__
    return HostUi::dashboard(snapshot,cpu_history,memory_history,rx_history,tx_history,network_index);
#else
    return "";
#endif
}
}
