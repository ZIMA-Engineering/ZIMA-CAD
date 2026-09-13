#pragma once
#include <zima/command_host/host.hpp>
namespace zima::command_host {
Json derived_copy_details(const workspace::Workspace&,const std::string& document,const std::string& object,bool pattern);
}
