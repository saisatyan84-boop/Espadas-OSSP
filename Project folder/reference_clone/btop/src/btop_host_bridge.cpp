// SPDX-License-Identifier: Apache-2.0
#ifdef __linux__
#include "host/windows_bridge.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <set>
#include <stdexcept>
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
static double number(const Json& value, double max) {
    if(!value.is_number()) throw std::runtime_error("Expected numeric telemetry");
    double n=value.get<double>();
    if(!std::isfinite(n) || n<0 || n>max) throw std::runtime_error("Telemetry value out of range");
    return n;
}
static std::string label(const Json& j) {
    if(!j.is_string() || j.get_ref<const std::string&>().size()>2048) throw std::runtime_error("Invalid host label");
    return j.get<std::string>();
}
static std::optional<double> optionalNumber(const Json& j,double max) {
    return j.is_null()?std::nullopt:std::optional<double>(number(j,max));
}
static const Json& list(const Json& j,size_t max) {
    if(!j.is_array() || j.size()>max) throw std::runtime_error("Invalid host list");
    return j;
}
static HostTelemetry decodeHost(const Json& frame,double sampled) {
    HostTelemetry h;
    if(!frame.contains("host")) return h; // Explicit legacy process-only prototype.
    const auto& j=frame.at("host");
    const auto& cpu=j.at("cpu");
    h.cpuName=label(cpu.at("name")); h.cpuUsage=optionalNumber(cpu.at("usage"),100);
    if(!cpu.at("logical_processors").is_number_integer() || cpu.at("logical_processors")<1)
        throw std::runtime_error("Invalid host processor count");
    h.logicalProcessors=static_cast<int>(number(cpu.at("logical_processors"),65536));
    h.uptime=number(cpu.at("uptime_seconds"),1e12);
    const auto& mem=j.at("memory");
    if(!mem.is_null()) {
        h.memoryTotal=number(mem.at("total"),1e16); h.memoryAvailable=number(mem.at("available"),*h.memoryTotal);
        if(*h.memoryTotal==0) throw std::runtime_error("Invalid host memory total");
    }
    for(const auto& n:list(j.at("networks"),1024)) {
        h.networks.push_back({label(n.at("id")),label(n.at("name")),label(n.at("kind")),label(n.at("state")),label(n.at("address")),
            n.at("virtual").get<bool>(),optionalNumber(n.at("rx"),1e15),optionalNumber(n.at("tx"),1e15)});
    }
    const auto& inventory=j.at("inventory");
    h.inventorySample=number(inventory.at("sample_seconds"),sampled);
    h.inventoryStatus=label(inventory.at("status"));
    if(inventory.contains("disk_read")) h.diskRead=optionalNumber(inventory.at("disk_read"),1e15);
    if(inventory.contains("disk_write")) h.diskWrite=optionalNumber(inventory.at("disk_write"),1e15);
    for(const auto& d:list(inventory.at("drives"),1024)) {
        double total=number(d.at("total"),1e16), free=number(d.at("free"),total);
        h.drives.push_back({label(d.at("name")),label(d.at("kind")),total,free});
    }
    for(const auto& d:list(inventory.at("devices"),8192)) {
        h.devices.push_back({label(d.at("id")),label(d.at("category")),label(d.at("name")),label(d.at("state")),label(d.at("detail")),optionalNumber(d.at("battery"),100)});
    }
    const auto& hardware=j.at("hardware");
    h.sensorSample=number(hardware.at("sample_seconds"),sampled); h.sensorStatus=label(hardware.at("status"));
    for(const auto& s:list(hardware.at("sensors"),8192)) {
        std::optional<double> value;
        if(!s.at("value").is_null()) {
            if(!s.at("value").is_number()) throw std::runtime_error("Invalid sensor value");
            double n=s.at("value").get<double>();
            if(!std::isfinite(n) || n< -1e9 || n>1e15) throw std::runtime_error("Invalid sensor range");
            value=n;
        }
        h.sensors.push_back({label(s.at("id")),label(s.at("hardware")),label(s.at("kind")),label(s.at("name")),label(s.at("unit")),value});
    }
    h.sample=sampled; h.available=true;
    return h;
}
bool WindowsDecoder::accept(const std::string& line, std::vector<HostProcess>& rows, std::string& error, HostTelemetry* host) {
    try {
        if(line.size()>ipc::maxFrameBytes) throw std::runtime_error("Telemetry frame too large");
        auto j=Json::parse(line,[](int depth,Json::parse_event_t,Json&) {
            if(depth>12) throw std::runtime_error("Telemetry nesting limit exceeded");
            return true;
        });
        if(!j.is_object() || !j.at("schema_version").is_number_integer() ||
           j.at("schema_version")!=1 || j.at("source")!="windows")
            throw std::runtime_error("Unsupported telemetry schema/source");
        const double sampled=number(j.at("sample_seconds"),1e12);
        if(sampled<=lastSample_) throw std::runtime_error("Non-increasing sample time");
        auto telemetry=decodeHost(j,sampled);
        const auto& list=j.at("processes");
        if(!list.is_array() || list.size()>100000) throw std::runtime_error("Invalid process list");
        std::vector<HostProcess> fresh;
        std::map<int,ProcessCounter> counters;
        std::set<int> seen;
        for(const auto& row:list) {
            HostProcess p;
            if(!row.at("pid").is_number_integer()) throw std::runtime_error("Invalid process PID");
            p.pid=static_cast<int>(number(row.at("pid"),2147483647));
            if(!seen.insert(p.pid).second) throw std::runtime_error("Duplicate process PID");
            p.command=row.at("name").get<std::string>();
            if(p.command.empty() || p.command.size()>1024) throw std::runtime_error("Invalid process name");
            // Null is explicitly unavailable, not a zero measurement.
            p.memoryAvailable=!row.at("ram_bytes").is_null();
            if(p.memoryAvailable) p.memMB=number(row.at("ram_bytes"),1e16)/(1024*1024);
            double processSample=number(row.at("sample_seconds"),sampled);
            if(!row.at("cpu_seconds").is_null() && !row.at("start_id").is_null()) {
                auto start=row.at("start_id").get<std::string>();
                if(start.empty() || start.size()>64) throw std::runtime_error("Invalid process identity");
                double cpu=number(row.at("cpu_seconds"),1e12);
                auto prev=previous_.find(p.pid);
                if(prev!=previous_.end() && prev->second.start==start &&
                   processSample>prev->second.sampled && cpu>=prev->second.cpu) {
                    p.cpuPct=100*(cpu-prev->second.cpu)/(processSample-prev->second.sampled);
                    if(!std::isfinite(p.cpuPct) || p.cpuPct>1e7) throw std::runtime_error("Invalid CPU delta");
                    p.cpuAvailable=true;
                }
                counters[p.pid]={start,cpu,processSample};
            }
            fresh.push_back(std::move(p));
        }
        // Commit only a fully validated frame.
        rows=std::move(fresh); previous_=std::move(counters); lastSample_=sampled;
        if(host) *host=std::move(telemetry);
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}
static std::string encodedScript(const std::string& script) {
    // The bundled script is ASCII; encode UTF-16LE for Windows PowerShell.
    std::string bytes;
    for(unsigned char c:script) {
        if(c>127) throw std::runtime_error("Prototype script must be ASCII");
        bytes.push_back(c); bytes.push_back('\0');
    }
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(size_t i=0;i<bytes.size();i+=3) {
        unsigned n=static_cast<unsigned char>(bytes[i])<<16;
        if(i+1<bytes.size()) n|=static_cast<unsigned char>(bytes[i+1])<<8;
        if(i+2<bytes.size()) n|=static_cast<unsigned char>(bytes[i+2]);
        out+=alphabet[(n>>18)&63]; out+=alphabet[(n>>12)&63];
        out+=i+1<bytes.size()?alphabet[(n>>6)&63]:'=';
        out+=i+2<bytes.size()?alphabet[n&63]:'=';
    }
    return out;
}
WindowsBridge::WindowsBridge(const HostOptions& config):config_(config) {
    if(config_.linuxOnly) return;
    try {
        auto bin=std::filesystem::canonical("/proc/self/exe").parent_path();
        if(!config_.prototype) {
            auto agent=config_.agent.empty()?(bin.parent_path()/"windows-agent/publish/sysmon-host.exe").string():config_.agent;
            if(!std::filesystem::exists(agent)) throw std::runtime_error("Windows agent missing; run scripts/build-host.ps1 on Windows (v: WSL view)");
            command_={agent,"--stream","--interval-ms",std::to_string(config_.windowsIntervalMs)};
            return;
        }
        auto path=bin/"scripts/windows-processes.ps1";
        if(!std::filesystem::exists(path)) path=bin.parent_path()/"scripts/windows-processes.ps1";
        std::ifstream file(path);
        if(!file) throw std::runtime_error("Missing bundled scripts/windows-processes.ps1");
        std::string script((std::istreambuf_iterator<char>(file)),{});
        if(script.size()>65536) throw std::runtime_error("Prototype script is too large");
        script="& {\n"+script+"\n} -IntervalMs "+std::to_string(config_.windowsIntervalMs);
        command_={config_.powershell,"-NoProfile","-NonInteractive","-EncodedCommand",encodedScript(script)};
    } catch(const std::exception& e) { setupError_=e.what(); }
}
void WindowsBridge::fail(WindowsSnapshot& s,const std::string& why) {
    child_.stop(); active_=false;
    s.connected=false; s.status=s.received==Clock::time_point{}?"Unavailable":"Stale";
    s.detail=why.substr(0,256)+"; retry in "+std::to_string(backoff_)+"s";
    next_=Clock::now()+std::chrono::seconds(backoff_);
    backoff_=std::min(30,backoff_*2);
}
void WindowsBridge::reconnect() {
    // Manual refresh does not restart a healthy producer.
    if(!active_) next_=Clock::time_point{};
}
bool WindowsBridge::poll(WindowsSnapshot& s) {
    if(config_.linuxOnly || !setupError_.empty()) {
        std::string detail=config_.linuxOnly?"Disabled by --linux-only":setupError_;
        bool changed=s.detail!=detail;
        s.status="Unavailable"; s.detail=detail; s.connected=false;
        return changed;
    }
    auto now=Clock::now();
    bool changed=false;
    if(!active_) {
        if(now<next_) return false;
        std::string error;
        if(!child_.start(command_,error)) { fail(s,error); return true; }
        decoder_.reset(); active_=true; lastFrame_=now;
        s.connected=false; s.status=s.received==Clock::time_point{}?"Connecting":"Stale";
        s.detail="Waiting for Windows telemetry"; changed=true;
    }
    auto received=child_.read();
    for(const auto& line:received.lines) {
        std::string error;
        if(!decoder_.accept(line,s.processes,error,&s.host)) { fail(s,error); return true; }
        s.connected=true; s.status="Connected"; s.detail=s.host.available?"Native Windows agent":"PowerShell process-only prototype";
        s.received=lastFrame_=Clock::now(); backoff_=1; changed=true;
    }
    if(received.ended || !received.error.empty()) { fail(s,received.error); return true; }
    if(Clock::now()-lastFrame_>std::chrono::milliseconds(std::max(15000,config_.windowsIntervalMs*3))) {
        fail(s,"Windows telemetry timed out"); return true;
    }
    return changed;
}

#endif
