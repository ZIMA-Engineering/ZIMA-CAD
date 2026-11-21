#pragma once
#include <zima/document/placement_types.hpp>
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <string>
#include <vector>

namespace zima::document {
enum class FemConditionKind { Constraint, Force, Pressure };
enum class FemRegionKind { Point, Curve, Surface };

struct FemRegionReference {
    std::string owner_id, semantic_key, instance_path;
    bool operator==(const FemRegionReference&) const = default;
};

// Analysis definitions own no material geometry. The region references exact
// native points, curves or faces, including bounded Projection results. Empty
// Point regions denote the placed container origin; meshing must bind that
// actual position, never an arbitrary nearest mesh node.
struct FemCondition {
    std::string id, name;
    FemConditionKind kind{FemConditionKind::Constraint};
    FemRegionKind region{FemRegionKind::Point};
    Placement placement;
    std::vector<FemRegionReference> references;
    double force_N{1000};
    double pressure_MPa{1};
    std::array<bool,3> restrained{true,true,true};
    bool pressure_reversed{};
    bool visible{true};
    bool operator==(const FemCondition&) const = default;
};

void validate_fem_condition(const FemCondition&);
void to_json(nlohmann::json&, const FemCondition&);
void from_json(const nlohmann::json&, FemCondition&);
} // namespace zima::document
