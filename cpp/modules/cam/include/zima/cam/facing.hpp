#pragma once
#include <zima/cam/program.hpp>

namespace zima::cam {
struct Bounds {
    Point minimum,maximum;
    bool operator==(const Bounds&) const = default;
};
struct Facing {
    std::string id,name,tool_id;
    double step_down_mm{1},step_over_mm{1},clearance_mm{5},allowance_mm{};
    bool operator==(const Facing&) const = default;
};
// A deterministic raster above the target's highest point. This generates a
// candidate, not a machining certificate: compile with the selected machine
// and simulate against the actual stock, target and fixtures before acceptance.
[[nodiscard]] Program face_program(const Facing&,const Tool&,const Bounds& stock,
    const Bounds& target,const std::vector<Bounds>& fixtures={});
} // namespace zima::cam
