#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace zima::kernel {
inline std::string solid_state_child_key(const std::string& owner,const std::string& key) {
    return "solid-state:parent:"+std::to_string(owner.size())+":"+owner+
        ":"+std::to_string(key.size())+":"+key;
}
inline std::optional<std::pair<std::string,std::string>> solid_state_parent(std::string_view key) {
    constexpr std::string_view prefix="solid-state:parent:";
    if(!key.starts_with(prefix))return {};
    key.remove_prefix(prefix.size());
    const auto read=[&](std::string& value) {
        const auto separator=key.find(':');if(separator==std::string_view::npos)return false;
        std::size_t length{};
        const auto [end,error]=std::from_chars(key.data(),key.data()+separator,length);
        if(error!=std::errc{} || end!=key.data()+separator || length==0)return false;
        key.remove_prefix(separator+1);if(length>key.size())return false;
        value=key.substr(0,length);key.remove_prefix(length);return true;
    };
    std::pair<std::string,std::string> result;
    if(!read(result.first) || key.empty() || key.front()!=':')return {};
    key.remove_prefix(1);
    if(!read(result.second) || !key.empty())return {};
    return result;
}
// The returned view uses the input or caller-owned storage. Only state keys
// require decoding; ordinary datum classification performs no allocation.
inline std::string_view solid_state_source_key(std::string_view key,std::string& storage) {
    while(auto parent=solid_state_parent(key)) {
        storage=std::move(parent->second);key=storage;
    }
    return key;
}
} // namespace zima::kernel
