#pragma once
#include <zima/cam/program.hpp>

namespace zima::cam {
enum class CollisionKind { RapidStock, CuttingFixture, NonCuttingStock, NonCuttingFixture, TargetGouge };
struct Collision {
    CollisionKind kind{};
    std::string block_id, tool_id, obstacle_id;
    double intersection_mm3{};
    bool operator==(const Collision&) const = default;
};
struct SimulationCounts {
    std::size_t swept_volumes{}, intersections{}, stock_cuts{}, bounds_rejections{};
};
} // namespace zima::cam
