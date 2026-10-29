// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model.hpp"
#include "child_process.hpp"
#include <map>
struct ProcessCounter { std::string start; double cpu=0, sampled=0; };
class WindowsDecoder {
public:
    bool accept(const std::string& line, std::vector<HostProcess>& rows, std::string& error, HostTelemetry* host=nullptr);
    void reset() { previous_.clear(); lastSample_=-1; }
private:
    std::map<int,ProcessCounter> previous_;
    double lastSample_=-1;
};
class WindowsBridge {
public:
    explicit WindowsBridge(const HostOptions&);
    bool poll(WindowsSnapshot&);
    void reconnect();
private:
    void fail(WindowsSnapshot&, const std::string&);
    HostOptions config_;
    ipc::ChildProcess child_;
    WindowsDecoder decoder_;
    bool active_=false;
    int backoff_=1;
    std::string setupError_;
    std::vector<std::string> command_;
    std::chrono::steady_clock::time_point next_{}, lastFrame_{};
};
