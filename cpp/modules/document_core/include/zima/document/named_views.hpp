#pragma once
#include <array>
#include <string>
#include <vector>

namespace zima::document {
// Viewer camera: quaternion (w,x,y,z), zoom, pan in logical pixels, reference scale.
struct NamedView {
    std::string name;
    std::array<float,8> camera{1,0,0,0,1,0,0,1};
    bool operator==(const NamedView&) const = default;
};
void normalize_named_view(NamedView& view);
[[nodiscard]] std::vector<NamedView> parse_named_views(const std::string& serialized);
[[nodiscard]] std::string serialize_named_views(const std::vector<NamedView>& views);
}
