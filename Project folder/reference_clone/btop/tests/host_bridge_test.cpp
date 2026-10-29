// SPDX-License-Identifier: Apache-2.0
#include "host/windows_bridge.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
#include <thread>
#include <fstream>
using Json=nlohmann::json;
static Json frame(double t,double cpu,std::string start="A") {
    return {{"schema_version",1},{"source","windows"},{"sample_seconds",t},
        {"processes",Json::array({{{"pid",42},{"name","test.exe"},{"ram_bytes",1048576},
            {"cpu_seconds",cpu},{"start_id",start},{"sample_seconds",t}}})}};
}
int main(int argc,char** argv) {
    if(argc==2) {
        WindowsDecoder live; WindowsSnapshot snapshot; std::string line,error; int count=0;
        std::ifstream file(argv[1]);
        while(std::getline(file,line)) {
            assert(live.accept(line,snapshot.processes,error,&snapshot.host));
            assert(snapshot.host.available && snapshot.host.memoryTotal && *snapshot.host.memoryTotal>0);
            ++count;
        }
        assert(count>=3);
        std::cout<<"PASS: "<<count<<" actual Windows host frames validated by the C++ decoder\n";
        return 0;
    }
    WindowsDecoder decoder;
    std::vector<HostProcess> rows;
    std::string error;
    assert(decoder.accept(frame(1,2).dump(),rows,error));
    assert(rows.size()==1 && !rows[0].cpuAvailable && rows[0].memMB==1);
    assert(decoder.accept(frame(3,3).dump(),rows,error));
    assert(rows[0].cpuAvailable && rows[0].cpuPct==50);
    assert(decoder.accept(frame(4,1,"B").dump(),rows,error));
    assert(!rows[0].cpuAvailable); // PID reused.
    auto missing=frame(5,2,"B");
    missing["processes"][0]["ram_bytes"]=nullptr;
    missing["processes"][0]["cpu_seconds"]=nullptr;
    assert(decoder.accept(missing.dump(),rows,error));
    assert(!rows[0].memoryAvailable && !rows[0].cpuAvailable);
    for(int kind=0;kind<11;++kind) {
        auto bad=frame(6,4);
        switch(kind) {
            case 0: bad["schema_version"]=2; break;
            case 1: bad["source"]="linux"; break;
            case 2: bad["processes"][0]["pid"]=-1; break;
            case 3: bad["processes"][0]["pid"]=1.5; break;
            case 4: bad["processes"][0]["ram_bytes"]=-1; break;
            case 5: bad["processes"][0]["name"]=std::string(1025,'x'); break;
            case 6: bad["processes"].push_back(bad["processes"][0]); break;
            case 7: bad["sample_seconds"]=1; break;
            case 8: bad["processes"]=Json::object(); break;
            case 9: bad["processes"][0]["sample_seconds"]=7; break;
            case 10: bad["processes"][0]["cpu_seconds"]="invalid"; break;
        }
        assert(!decoder.accept(bad.dump(),rows,error));
        assert(rows.size()==1 && rows[0].pid==42 && !rows[0].memoryAvailable);
    }
    assert(!decoder.accept("not json",rows,error));
    assert(!decoder.accept(std::string(ipc::maxFrameBytes+1,'x'),rows,error));
    assert(!decoder.accept(std::string(40,'[')+std::string(40,']'),rows,error));
    assert(decoder.accept(frame(6,5,"B").dump(),rows,error));
    assert(!rows[0].cpuAvailable); // Inaccessible sample broke the CPU baseline.
    decoder.reset();
    assert(decoder.accept(frame(1,1).dump(),rows,error));
    assert(!rows[0].cpuAvailable); // Producer restart resets its clock.
    auto empty=frame(2,2); empty["processes"]=Json::array();
    assert(decoder.accept(empty.dump(),rows,error) && rows.empty());
    // Optional host extension is validated before committing either host or process data.
    auto hostFrame=frame(3,3);
    hostFrame["host"]={
        {"cpu",{{"usage",12.5},{"name","CPU"},{"logical_processors",16},{"uptime_seconds",100}}},
        {"memory",{{"total",1000},{"available",400}}},
        {"networks",Json::array()},
        {"inventory",{{"sample_seconds",2},{"status","ready"},{"drives",Json::array()},
            {"devices",Json::array({{{"id","BT"},{"category","Bluetooth"},{"name","Test buds"},{"state","Connected"},{"battery",0},{"detail","test fixture"}}})}}},
        {"hardware",{{"sample_seconds",2},{"status","ready"},{"sensors",Json::array({
            {{"id","temperature"},{"hardware","CPU"},{"kind","Cpu"},{"name","Package"},{"unit","C"},{"value",nullptr}}})}}}
    };
    HostTelemetry host;
    assert(decoder.accept(hostFrame.dump(),rows,error,&host));
    assert(*host.memoryTotal-*host.memoryAvailable==600); // Host physical usage, no subsystem addition.
    assert(host.devices[0].battery && *host.devices[0].battery==0); // Empty battery differs from unavailable.
    assert(!host.sensors[0].value);
    for(int kind=0;kind<5;++kind) {
        auto bad=hostFrame; bad["sample_seconds"]=4; bad["processes"][0]["sample_seconds"]=4;
        switch(kind) {
            case 0: bad["host"]["memory"]["available"]=1001; break;
            case 1: bad["host"]["inventory"]["devices"][0]["battery"]=101; break;
            case 2: bad["host"]["cpu"]["usage"]=101; break;
            case 3: bad["host"]["hardware"]["sample_seconds"]=5; break;
            case 4: bad["host"]["inventory"]["devices"][0]["name"]=std::string(2049,'x'); break;
        }
        assert(!decoder.accept(bad.dump(),rows,error,&host));
        assert(host.sample==3 && host.devices[0].battery==0);
    }
    ipc::ChildProcess child;
    assert(!child.start({"/does/not/exist"},error));
    assert(child.start({"/usr/bin/python3","-c",
        "import sys,time;sys.stdout.write('abc');sys.stdout.flush();time.sleep(.1);sys.stdout.write('def\\r\\nsecond\\n');sys.stdout.flush()"},error));
    std::vector<std::string> lines;
    bool ended=false;
    for(int i=0;i<200 && !ended;++i) {
        auto r=child.read();
        lines.insert(lines.end(),r.lines.begin(),r.lines.end()); ended=r.ended;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ended && lines==std::vector<std::string>({"abcdef","second"}));
    child.stop();
    assert(child.start({"/usr/bin/python3","-c","print('x'*5000000,flush=True)"},error));
    bool rejected=false;
    for(int i=0;i<200 && !rejected;++i) {
        auto r=child.read(); rejected=r.error=="Telemetry frame too large";
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(rejected); child.stop();
    assert(child.start({"/usr/bin/python3","-c","import time;time.sleep(60)"},error));
    auto before=std::chrono::steady_clock::now(); child.stop();
    assert(std::chrono::steady_clock::now()-before<std::chrono::seconds(1));
    std::cout<<"PASS: CPU deltas, PID reuse, missing metrics, atomic schema validation, restarts, framing, size limits, shutdown\n";
}
