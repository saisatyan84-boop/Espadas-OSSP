// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <string_view>
namespace Host {
bool enabled();
bool is_wsl();
void reset_layout();
void layout(int x,int y,int width,int& linux_height);
void collect();
void shutdown();
bool show_linux();
bool key(std::string_view key);
std::string draw();
std::string scope_labels();
std::string dashboard();
}
