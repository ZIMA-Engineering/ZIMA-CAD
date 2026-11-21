#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/kernel/geometry_kernel.hpp>
#include <nlohmann/json_fwd.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace zima::sketcher { class Sketch; }
namespace zima::document {
enum class FemConditionKind { Constraint, Force, Pressure };
enum class FemRegionKind { Point, Curve, Surface };

struct FemRegionReference {
    std::string owner_id, semantic_key, instance_path;
    bool operator==(const FemRegionReference&) const = default;
};

struct FemProjectedSketch {
    // Ordinary Sketch data owned by this condition. Its local XY geometry
    // remains editable; projection is calculated from the resolved placement.
    std::string serialized;
    // Seed points use the material source frame and follow target row order.
    // They select the bounded loaded patch on each target face.
    std::vector<kernel::Vec3> surface_seeds;
    std::vector<std::string> surface_region_keys;
    bool operator==(const FemProjectedSketch&) const = default;
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
    // When present, references identify the native target faces rather than
    // the already authored region geometry.
    std::optional<FemProjectedSketch> projected_sketch;
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
[[nodiscard]] sketcher::Sketch fem_condition_sketch(const FemCondition&);
[[nodiscard]] kernel::ProjectionRequest fem_condition_projection_request(const FemCondition&);
} // namespace zima::document
