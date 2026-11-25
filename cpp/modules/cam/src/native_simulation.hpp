#pragma once
#include "native_sweep.hpp"
#include <zima/cam/simulation.hpp>
#include <functional>

namespace zima::cam::native {
struct Obstacle {
    std::string id;
    TopoDS_Shape shape;
};
struct Clamping {
    std::string id;
    // Exact bounded planar contact faces, including their authored holes.
    TopoDS_Shape contact;
    Point contact_origin,outward_normal;
    Point upper_origin,upper_normal; // Positive normal points above the jaw.
};
struct SimulationResult {
    TopoDS_Shape remaining;
    std::vector<Collision> collisions;
    std::vector<ClampingWarning> warnings;
    std::vector<ClampingContact> contacts;
    double removed_mm3{};
    // A desired design outside the starting stock cannot be obtained by removal.
    double missing_target_mm3{};
    SimulationCounts counts;
};
// All shapes and motions are in the same work frame. Source shapes are read-only.
// Inspection/stepping consumes the compiled motions without calling this function.
// Progress may throw to cancel; partial results are never published.
[[nodiscard]] SimulationResult simulate(const CompiledProgram&,const std::vector<Tool>&,
    const TopoDS_Shape& stock,const TopoDS_Shape& target,const std::vector<Obstacle>& fixtures,
    double tolerance_mm=1e-7,const std::function<void(std::size_t,std::size_t)>& progress={},
    const std::vector<Clamping>& clampings={});
} // namespace zima::cam::native
