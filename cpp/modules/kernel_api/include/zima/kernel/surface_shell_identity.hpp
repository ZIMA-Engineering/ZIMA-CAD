#pragma once
#include <zima/kernel/solid_state_ancestry.hpp>
namespace zima::kernel {
// The new surface object is a child of the exact input topology, including
// edges created by treatments. Geometry reuse never aliases parent identity.
inline std::string surface_shell_child_key(const std::string& owner,const std::string& key) {
    auto encoded=solid_state_child_key(owner,key);
    encoded.replace(0,std::string_view("solid-state").size(),"surface-shell");
    return encoded;
}
inline std::optional<std::pair<std::string,std::string>> surface_shell_parent(std::string_view key) {
    if(!key.starts_with("surface-shell:parent:"))return {};
    return solid_state_parent("solid-state"+std::string(key.substr(std::string_view("surface-shell").size())));
}
}
