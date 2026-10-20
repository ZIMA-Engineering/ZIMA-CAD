#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace zima::sketcher {
struct SketchExternalReference;
inline constexpr std::string_view external_center_prefix{"sketch_external_center:"};
// A derived, read-only point identity. The referenced native curve owns its
// cached exact geometry; no duplicate center or external sidecar is required.
[[nodiscard]] inline std::string external_center_point_id(std::string_view reference) {
    return std::string(external_center_prefix)+std::string(reference);
}
[[nodiscard]] inline std::optional<std::string_view> external_center_source(std::string_view point) {
    if(!point.starts_with(external_center_prefix)||point.size()==external_center_prefix.size())return {};
    return point.substr(external_center_prefix.size());
}
// Circle/arc or ellipse/elliptical-arc center, certified against the complete
// exact rational curve. A broken link may still expose its last valid cache.
[[nodiscard]] std::optional<std::array<double,2>> external_reference_center(const SketchExternalReference&);
}
