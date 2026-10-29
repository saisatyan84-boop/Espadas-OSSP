// SPDX-License-Identifier: Apache-2.0
#include "host/ui.hpp"
#include "btop_draw.hpp"
#include "btop_shared.hpp"
#include "btop_config.hpp"
#include "btop_theme.hpp"
#include "btop_tools.hpp"
#include <algorithm>
#include <set>
#include <cwchar>

namespace HostUi {
using namespace Tools;
namespace {
std::string clean(std::string s) { for(auto& c:s) if(static_cast<unsigned char>(c)<32 || c==127) c='?'; return s; }
std::string clip(const std::string& text,int width) {
    std::string out; std::mbstate_t state{}; size_t at=0; int columns=0;
    while(at<text.size()) {
        wchar_t wc; size_t n=std::mbrtowc(&wc,text.data()+at,text.size()-at,&state);
        if(n==static_cast<size_t>(-1) || n==static_cast<size_t>(-2) || n==0) { n=1; wc=L'?'; state={}; }
        int cells=wcwidth(wc);
        if(cells<0) { if(columns+1>width) break; out+='?'; ++columns; }
        else { if(columns+cells>width) break; out.append(text,at,n); columns+=cells; }
        at+=n;
    }
    return out;
}
std::string bytes(double n) {
    if(n>=1073741824.) return fmt::format("{:.1f} GiB",n/1073741824.);
    return floating_humanizer(static_cast<uint64_t>(std::max(0.,n)));
}
std::string metric(std::optional<double> n,const std::string& unit) { return n?fmt::format("{:.1f}{}",*n,unit):"N/A"; }
struct Panel {
    int x,y,w,h; std::string color,out;
    Panel(int x,int y,int w,int h,std::string color,std::string title):x(x),y(y),w(w),h(h),color(color),
        out(Draw::createBox(x,y,w,h,Theme::c(color),true,clip(title,std::max(0,w-8)))) {}
    void line(int row,int col,const std::string& text,std::string fg="main_fg",int limit=0) {
        if(row<1 || row>=h-1 || col<1 || col>=w-1) return;
        int width=std::min(w-col-1,limit?limit:w);
        out+=Mv::to(y+row,x+col)+Theme::c(fg)+clip(clean(text),width);
    }
    void footer(const std::string& text) {
        out+=Mv::to(y+h-1,x+2)+Theme::c(color)+Symbols::title_left_down+Theme::c("title")+
            clip(text,w-8)+Theme::c(color)+Symbols::title_right_down;
    }
    void graph(int row,int col,int width,int height,const History& values,const std::string& gradient,long long max=0,bool invert=false) {
        width=std::min(width,w-col-1); height=std::min(height,h-row-1);
        if(width<2 || height<1 || values.empty()) return;
        Draw::Graph graph(width,height,gradient,values,Config::getB("tty_mode")?"tty":Config::getS("graph_symbol"),invert,false,max);
        out+=Mv::to(y+row,x+col)+graph();
    }
    void meter(int row,int col,int width,double percent) {
        if(row>=h-1 || width<2) return;
        Draw::Meter meter(std::min(width,w-col-1),"cpu");
        out+=Mv::to(y+row,x+col)+meter(static_cast<int>(std::clamp(percent,0.,100.)));
    }
};
bool sensorFresh(const WindowsSnapshot& s) { return s.connected && s.host.sensorSample>0 && s.host.sample-s.host.sensorSample<15; }
bool inventoryFresh(const WindowsSnapshot& s) { return s.connected && s.host.inventorySample>0 && s.host.sample-s.host.inventorySample<20; }
std::optional<double> sensor(const WindowsSnapshot& s,const std::string& kind,const std::string& unit,const std::string& name="",const std::string& hardware="") {
    if(!sensorFresh(s)) return {};
    for(const auto& v:s.host.sensors) if(v.kind.starts_with(kind) && v.unit==unit &&
        (name.empty() || v.name==name) && (hardware.empty() || v.hardware==hardware) && v.value) return v.value;
    return {};
}
}
std::string dashboard(const WindowsSnapshot& s,const History& cpu,const History& memory,
        std::map<std::string,History>& rx,std::map<std::string,History>& tx,int& ni) {
    std::string out;
    const auto& h=s.host;
    const bool live=s.connected && h.available;
    if(Cpu::shown) {
        Panel p(Cpu::x,Cpu::y,Cpu::width,Cpu::height,"cpu_box","whole PC / Windows CPU + hardware");
        const int side=p.w>=100?std::min(60,p.w/2):p.w-4, col=p.w-side-2;
        if(col>4) {
            p.line(1,2,"CPU history / all Windows + WSL workloads","title",col-3);
            if(live) p.graph(3,2,col-4,p.h-5,cpu,"cpu");
        }
        const int start=col>4?col:2;
        p.line(1,start,h.cpuName.empty()?"Waiting for Windows host":h.cpuName,"title");
        p.line(2,start,"CPU "+metric(live?h.cpuUsage:std::nullopt,"%")+"   "+std::to_string(h.logicalProcessors)+" logical CPUs");
        if(live && h.cpuUsage) p.meter(3,start,side-3,*h.cpuUsage);
        p.line(4,start,"Temp "+metric(sensor(s,"Cpu","C")," C")+"   Clock "+metric(sensor(s,"Cpu","MHz","Cores (Average)")," MHz"));
        p.line(5,start,"Power "+metric(sensor(s,"Cpu","W","Package")," W")+"   Host uptime "+std::to_string(static_cast<long long>(h.uptime/3600))+"h");
        std::set<std::string> gpus;
        for(const auto& v:h.sensors) if(v.kind.starts_with("Gpu")) gpus.insert(v.hardware);
        int row=7;
        for(const auto& gpu:gpus) {
            p.line(row++,start,gpu,"title");
            p.line(row++,start,"GPU "+metric(sensor(s,"Gpu","%","GPU Core",gpu),"%")+"   Temp "+metric(sensor(s,"Gpu","C","GPU Core",gpu)," C"));
            p.line(row++,start,"VRAM "+metric(sensor(s,"Gpu","MiB","GPU Memory Used",gpu),"")+" / "+metric(sensor(s,"Gpu","MiB","GPU Memory Total",gpu)," MiB"));
            p.line(row++,start,"Clock "+metric(sensor(s,"Gpu","MHz","GPU Core",gpu)," MHz")+"   Power "+metric(sensor(s,"Gpu","W","GPU Package",gpu)," W"));
        }
        if(gpus.empty()) p.line(7,start,"GPU / sensors: "+h.sensorStatus,"inactive_fg");
        if(!live) p.line(p.h-2,2,s.detail,"inactive_fg");
        p.footer("Host: "+s.status+" | v WSL | d devices | S sensors | P processes | q quit");
        out+=p.out;
    }
    if(Mem::shown) {
        Panel p(Mem::x,Mem::y,Mem::width,Mem::height,"mem_box","whole PC memory + Windows drives");
        const int half=p.w>=65?p.w/2:p.w-3;
        const bool valid=live && h.memoryTotal && h.memoryAvailable;
        p.line(1,2,valid?"Physical RAM  "+bytes(*h.memoryTotal):"Physical RAM  N/A","title",half-3);
        p.line(2,2,valid?"Used       "+bytes(*h.memoryTotal-*h.memoryAvailable):"Used       N/A","main_fg",half-3);
        if(valid) p.meter(3,2,half-5,100*(*h.memoryTotal-*h.memoryAvailable)/ *h.memoryTotal);
        p.line(4,2,valid?"Available  "+bytes(*h.memoryAvailable):"Available  N/A","main_fg",half-3);
        p.line(5,2,"Includes memory backing WSL","inactive_fg",half-3);
        if(live) p.graph(7,2,half-5,p.h-10,memory,"used");
        // Linux view is a subset with its own allocation limit, never summed into host RAM.
        auto& wsl=Mem::collect(true);
        p.line(p.h-3,2,"WSL used "+bytes(wsl.stats.at("used")),"inactive_fg",half-3);
        p.line(p.h-2,2,"WSL limit "+bytes(Mem::get_totalMem()),"inactive_fg",half-3);
        if(p.w>=65) {
            int row=1;
            for(const auto& drive:h.drives) {
                p.line(row++,half,"Drive "+drive.name+"  "+(inventoryFresh(s)?drive.kind:"stale"),"title");
                p.line(row++,half,"Used "+bytes(drive.total-drive.free)+" / "+bytes(drive.total));
                if(inventoryFresh(s) && drive.total>0) p.meter(row++,half,p.w-half-3,100*(drive.total-drive.free)/drive.total);
                p.line(row++,half,"Free "+bytes(drive.free));
                ++row;
            }
            if(h.drives.empty()) p.line(1,half,"Drives: N/A","inactive_fg");
            p.line(p.h-5,half,"All physical disks","title");
            p.line(p.h-4,half,"SSD temp "+metric(sensor(s,"Storage","C")," C"));
            p.line(p.h-3,half,"Read  "+(inventoryFresh(s)&&h.diskRead?bytes(*h.diskRead)+"/s":"N/A"));
            p.line(p.h-2,half,"Write "+(inventoryFresh(s)&&h.diskWrite?bytes(*h.diskWrite)+"/s":"N/A"));
        }
        p.footer(live?"Host RAM includes WSL | v subsystem view":"Host: "+s.status+" | v WSL view");
        out+=p.out;
    }
    if(Net::shown) {
        Panel p(Net::x,Net::y,Net::width,Net::height,"net_box","Windows network / adapter traffic");
        if(!h.networks.empty()) {
            ni=(ni%static_cast<int>(h.networks.size())+static_cast<int>(h.networks.size()))%static_cast<int>(h.networks.size());
            const auto& n=h.networks[ni];
            p.line(1,2,n.name+" | "+n.state+(n.virtualAdapter?" | virtual":""),"title");
            p.line(2,2,n.kind+" | "+n.address,"inactive_fg");
            p.line(3,2,"Download "+(live&&n.rx?bytes(*n.rx)+"/s":"N/A")+"   Upload "+(live&&n.tx?bytes(*n.tx)+"/s":"N/A"));
            int height=std::max(1,(p.h-7)/2);
            auto& down=rx[n.id]; auto& up=tx[n.id];
            long long scale=10240;
            if(!down.empty()) scale=std::max(scale,*std::max_element(down.begin(),down.end()));
            if(!up.empty()) scale=std::max(scale,*std::max_element(up.begin(),up.end()));
            if(live) { p.graph(5,2,p.w-4,height,down,"download",scale); p.graph(5+height,2,p.w-4,height,up,"upload",scale,true); }
            p.footer(live?"[ / ] adapter "+std::to_string(ni+1)+"/"+std::to_string(h.networks.size())+" | v WSL network":"Host: "+s.status+" | v WSL network");
        } else { p.line(2,2,"Windows network: unavailable","inactive_fg"); p.footer("v WSL network"); }
        out+=p.out;
    }
    return out+Fx::reset;
}

std::string details(const WindowsSnapshot& s,int page,bool showPaired,int& offset,int x,int y,int w,int h) {
    Panel p(x,y,w,h,"proc_box",page==1?"connected devices / Windows":"hardware sensors / Windows");
    const bool fresh=page==1?inventoryFresh(s):sensorFresh(s);
    p.line(1,2,"Host: "+s.status+" | "+(fresh?"Live":"Pending / stale")+" | "+(page==1?s.host.inventoryStatus:s.host.sensorStatus),"inactive_fg");
    using Row=std::pair<std::string,bool>;
    std::vector<Row> rows;
    if(page==1) {
        int connected=0,paired=0;
        for(const auto& d:s.host.devices) if(d.category=="Bluetooth") { ++paired; if(d.state=="Connected") ++connected; }
        rows.emplace_back("BLUETOOTH  "+std::to_string(connected)+" connected / "+std::to_string(paired)+" known",true);
        if(!connected && !showPaired) rows.emplace_back("No active Bluetooth connections | b: show paired devices",false);
        if(!paired && showPaired) rows.emplace_back("No paired Bluetooth devices reported",false);
        auto add=[&](const HostDevice& d) {
            auto battery=fresh && d.state=="Connected"?metric(d.battery,"%"):"N/A";
            rows.emplace_back(ljust(clean(d.name),std::max(8,w-30),true,true)+" "+rjust(d.state,12)+" "+rjust(battery,7),false);
            rows.emplace_back("  "+clean(d.detail),false);
        };
        for(const auto& d:s.host.devices) if(d.category=="Bluetooth" && (showPaired || d.state=="Connected")) add(d);
        std::string category;
        for(const auto& d:s.host.devices) {
            if(d.category=="Bluetooth") continue;
            if(category!=d.category) { category=d.category; rows.emplace_back(category+"  /  state                     battery",true); }
            add(d);
        }
    } else {
        rows.emplace_back("Unavailable sensors show N/A. Sensing requires hardware/driver support.",false);
        std::string hardware;
        for(const auto& v:s.host.sensors) {
            if(v.hardware!=hardware) { hardware=v.hardware; rows.emplace_back(hardware+" / "+v.kind,true); }
            rows.emplace_back(ljust(clean(v.name),std::max(8,w-22),true,true)+rjust(metric(fresh?v.value:std::nullopt," "+v.unit),16),false);
        }
        if(s.host.sensors.empty()) rows.emplace_back("No sensor readings available yet",false);
    }
    int visible=std::max(1,h-4);
    offset=std::clamp(offset,0,std::max(0,static_cast<int>(rows.size())-visible));
    for(int i=0;i<visible && offset+i<static_cast<int>(rows.size());++i)
        p.line(i+2,2,rows[offset+i].first,rows[offset+i].second?"title":"main_fg");
    p.footer("d devices | S sensors | P processes | "+std::string(page==1?"b paired | ":"")+"scroll "+std::to_string(offset+1)+"/"+std::to_string(rows.size()));
    return p.out+Fx::reset;
}
}
