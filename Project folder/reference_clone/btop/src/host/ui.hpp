// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "model.hpp"
#include <deque>
#include <map>
namespace HostUi {
using History=std::deque<long long>;
std::string dashboard(const WindowsSnapshot&,const History& cpu,const History& memory,
    std::map<std::string,History>& rx,std::map<std::string,History>& tx,int& interfaceIndex);
std::string details(const WindowsSnapshot&,int page,bool paired,int& offset,int x,int y,int w,int h);
}
