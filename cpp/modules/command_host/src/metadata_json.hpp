#pragma once
#include <zima/commands/dispatcher.hpp>
#include <algorithm>
#include <map>
#include <stdexcept>
namespace zima::command_host::metadata_json {
using Json=commands::Json;
inline void fields(const Json& object,std::initializer_list<const char*> allowed) {
    if(!object.is_object())throw std::invalid_argument("Metadata entries must be JSON objects with supported fields.");
    for(auto it=object.begin();it!=object.end();++it)if(std::ranges::find(allowed,it.key())==allowed.end())
        throw std::invalid_argument("Metadata entries must be JSON objects with supported fields.");
}
inline std::map<std::string,std::string> strings(const Json& object) {
    if(!object.is_object())throw std::invalid_argument("Metadata values and labels must be objects of strings.");
    std::map<std::string,std::string> result;
    for(auto it=object.begin();it!=object.end();++it) {
        if(!it.value().is_string())throw std::invalid_argument("Metadata values and labels must be objects of strings.");
        result[it.key()]=it.value().get<std::string>();
    }
    return result;
}
}
