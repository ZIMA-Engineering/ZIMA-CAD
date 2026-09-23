#pragma once
#include "transition_pattern.hpp"
#include <zima/kernel/sheet_material.hpp>
namespace zima::research::transition {
struct SheetOptions {double thickness{1},inside_radius{1},k_factor{.5};};
struct SheetPanel {std::vector<Vec3> outer,developed;Vec3 inward;std::vector<std::string> edge_roles;};
struct SheetBend {
    std::size_t boundary_index{};
    Vec3 center,along,start_radial,turn_tangent;
    double angle{},outer_radius{},neutral_radius{},thickness{};
    std::vector<std::array<Vec3,4>> sections; // Outer first/last, inner last/first.
    std::vector<std::array<Vec3,4>> developed_sections;
    kernel::SheetMaterialDefinition material;
};
struct SheetResult {
    std::vector<SheetPanel> panels;
    std::vector<SheetBend> bends;
    Pattern axes;
    double thickness{};
};
// Explicit calculation only. Throws on invalid or collapsed finite-radius geometry.
[[nodiscard]] SheetResult manufacture(const HalfModel&,const SheetOptions&);
[[nodiscard]] kernel::FeatureGroupRequest sheet_request(const SheetResult&,bool unfolded=false);
[[nodiscard]] kernel::HistoryOperation sheet_operation(const SheetResult&,const std::string& owner);
}
