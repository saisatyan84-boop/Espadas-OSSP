// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <vector>
#include <sys/types.h>
namespace ipc {
constexpr size_t maxFrameBytes = 4 * 1024 * 1024;
struct ReadResult {
    std::vector<std::string> lines;
    std::string error;
    bool ended = false;
};
class ChildProcess {
public:
    ~ChildProcess();
    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    bool start(const std::vector<std::string>& args, std::string& error);
    ReadResult read();
    void stop();
private:
    pid_t pid_ = -1;
    int out_ = -1, err_ = -1;
    std::string pending_, diagnostics_;
};
}
