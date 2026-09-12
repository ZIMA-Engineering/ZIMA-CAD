#pragma once
#include "geometry_kernel.hpp"
#include <charconv>
#include <string_view>

namespace zima::kernel {
// Defined by the selected ZIMA source before calculation, independent of the
// order of selected bottoms, OCCT faces, or surviving matches.
inline std::string drill_point_key(std::string_view role,const FaceReference& source) {
    if((role!="side"&&role!="base"&&role!="base-circle")||!source.valid()||!source.instance_path.empty())
        throw std::invalid_argument("Invalid drill-point topology source");
    return "drill-point:"+std::string(role)+":from:"+std::to_string(source.owner_id.size())+":"+source.owner_id+":"+source.semantic_key;
}
inline std::optional<FaceReference> drill_point_source(std::string_view key) {
    constexpr std::string_view prefix="drill-point:";
    if(!key.starts_with(prefix))return {};
    key.remove_prefix(prefix.size());const auto separator=key.find(":from:");
    if(separator==std::string_view::npos)return {};
    const auto role=key.substr(0,separator);
    if(role!="side"&&role!="base"&&role!="base-circle")return {};
    key.remove_prefix(separator+6);const auto colon=key.find(':');
    if(colon==std::string_view::npos)return {};
    std::size_t length{};const auto parsed=std::from_chars(key.data(),key.data()+colon,length);
    if(parsed.ec!=std::errc{}||parsed.ptr!=key.data()+colon||length==0)return {};
    key.remove_prefix(colon+1);
    if(length>=key.size()||key[length]!=':'||length+1==key.size())return {};
    return FaceReference{std::string(key.substr(0,length)),std::string(key.substr(length+1)),{}};
}
} // namespace zima::kernel
