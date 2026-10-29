// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <optional>
struct HostNetwork {
    std::string id, name, kind, state, address;
    bool virtualAdapter=false;
    std::optional<double> rx, tx;
};
struct HostDrive { std::string name, kind; double total=0, free=0; };
struct HostDevice {
    std::string id, category, name, state, detail;
    std::optional<double> battery;
};
struct HostSensor {
    std::string id, hardware, kind, name, unit;
    std::optional<double> value;
};
struct HostTelemetry {
    bool available=false;
    std::string cpuName, inventoryStatus, sensorStatus;
    std::optional<double> cpuUsage, memoryTotal, memoryAvailable;
    std::optional<double> diskRead, diskWrite;
    double uptime=0, sample=0, inventorySample=0, sensorSample=0;
    int logicalProcessors=0;
    std::vector<HostNetwork> networks;
    std::vector<HostDrive> drives;
    std::vector<HostDevice> devices;
    std::vector<HostSensor> sensors;
};
struct HostProcess {
    int pid=0;
    std::string command;
    double memMB=0, cpuPct=0;
    bool cpuAvailable=false, memoryAvailable=false;
};
struct WindowsSnapshot {
    HostTelemetry host;
    std::vector<HostProcess> processes;
    std::string status="Connecting", detail="Waiting for Windows telemetry";
    std::chrono::steady_clock::time_point received{};
    bool connected=false;
};
struct HostOptions {
    bool linuxOnly=false;
    int windowsIntervalMs=2000;
    std::string powershell="powershell.exe";
    std::string agent;
    bool prototype=false;
};
