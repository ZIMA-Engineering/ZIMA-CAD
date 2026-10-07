#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace zima::kernel {
// Decode the explicit persisted parent of an exact Boolean face fragment.
// Multiple parents cannot be reduced to an arbitrary reference here.
inline std::optional<std::pair<std::string,std::string>> boolean_face_parent(std::string_view key) {
    if(!key.starts_with("boolean:"))return {};
    const auto marker=key.find(':',std::string_view("boolean:").size());
    if(marker==std::string_view::npos||!key.substr(marker).starts_with(":split-face:from:"))return {};
    key.remove_prefix(marker+std::string_view(":split-face:from:").size());
    const auto read=[](std::string_view& input,std::string_view& value) {
        const auto colon=input.find(':');if(colon==std::string_view::npos)return false;
        std::size_t length{};
        const auto [end,error]=std::from_chars(input.data(),input.data()+colon,length);
        if(error!=std::errc{}||end!=input.data()+colon)return false;
        input.remove_prefix(colon+1);if(length>input.size())return false;
        value=input.substr(0,length);input.remove_prefix(length);return true;
    };
    std::string_view parent,owner,source,path;
    if(!read(key,parent)||!key.starts_with(":boundary:")||
       !read(parent,owner)||!read(parent,source)||!read(parent,path)||
       !parent.empty()||owner.empty()||source.empty())return {};
    return std::pair{std::string(owner),std::string(source)};
}
} // namespace zima::kernel
