#pragma once
#include <zima/cam/program.hpp>

namespace zima::cam {
enum class CollisionKind { RapidStock, CuttingFixture, NonCuttingStock, NonCuttingFixture, TargetGouge,
    CuttingClamping, NonCuttingClamping };
struct Collision {
    CollisionKind kind{};
    std::string block_id, tool_id, obstacle_id;
    double intersection_mm3{};
    bool operator==(const Collision&) const = default;
};
struct SimulationCounts {
    std::size_t swept_volumes{}, intersections{}, stock_cuts{}, bounds_rejections{};
};
enum class ClampingWarningKind { BelowJawTop, ContactLoss };
struct ClampingWarning {
    ClampingWarningKind kind{};
    std::string operation_id,block_id,tool_id,clamping_id;
    double removed_mm3{},lost_contact_mm2{},remaining_contact_mm2{};
    bool operator==(const ClampingWarning&) const = default;
};
struct ClampingContact {
    std::string clamping_id;
    double initial_mm2{},remaining_mm2{};
    bool operator==(const ClampingContact&) const = default;
};
} // namespace zima::cam
