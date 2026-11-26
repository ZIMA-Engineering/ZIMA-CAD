#pragma once
#include <zima/cam/facing.hpp>
#include <functional>

namespace zima::cam {
enum class ProfileMillingKind { Pocket, Contour };
enum class ContourSide { Inside, Outside };
struct ProfileMilling {
    std::string id,name,tool_id,sketch_id;
    ProfileMillingKind kind{ProfileMillingKind::Pocket};
    ContourSide side{ContourSide::Inside};
    bool clockwise{};
    double top_mm{},bottom_mm{-1},step_down_mm{1},step_over_mm{1};
    double clearance_mm{5},radial_allowance_mm{},axial_allowance_mm{},path_tolerance_mm{.01};
    bool operator==(const ProfileMilling&) const = default;
};
void validate(const ProfileMilling&);
// Exact authored regions in work coordinates; no viewer strokes or triangles.
// All generated cutting and transfer motions still require compile/simulate.
[[nodiscard]] Program profile_program(const ProfileMilling&,const Tool&,
    const kernel::ProjectionRequest&,const Bounds& stock,const Bounds& target,
    const std::vector<Bounds>& fixtures={},const std::function<void()>& checkpoint={});
} // namespace zima::cam
