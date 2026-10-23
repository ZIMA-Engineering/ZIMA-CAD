#pragma once

#include <zima/document/profile_parameters.hpp>
#include <stdexcept>
#include <array>
#include <optional>

namespace zima::document {

enum class FeatureType { Point, Axis, Plane, Sketch, Modeling };

enum class FeatureSideOperation { None, Extrusion, Revolution, Twist };
enum class FeatureRotationExtent { Angle, Full, UpTo };

struct FeatureUpToPatchChoice {
    kernel::FaceReference source;
    kernel::FaceReference adjacent;
    bool positive{};
    bool operator==(const FeatureUpToPatchChoice&) const = default;
};
struct FeatureUpToParameters {
    double length_correction{};
    double angle_correction_degrees{};
    // The pre-existing end-cap identity stays attached to this authored target
    // when a single limit becomes a chain or the chain's rows are reordered.
    std::optional<kernel::FaceReference> primary_cap_source;
    // Directed adjacency relationships, keyed by authored references rather
    // than volatile row numbers. Reordering never transfers a retained side
    // to another source/adjacent pair.
    std::vector<FeatureUpToPatchChoice> retained_patches;
    [[nodiscard]] bool neutral() const;
    [[nodiscard]] std::optional<bool> retained_side(const kernel::FaceReference& source,
        const kernel::FaceReference& adjacent) const;
    void set_retained_side(const kernel::FaceReference& source,
        const kernel::FaceReference& adjacent,bool positive);
    bool operator==(const FeatureUpToParameters&) const = default;
};

struct FeatureSideParameters {
    FeatureSideOperation operation{FeatureSideOperation::None};
    // Retain the settings of both operation kinds when switching a side.
    double length{50};
    EndCondition extrusion_extent{EndCondition::Length};
    double angle_degrees{90};
    FeatureRotationExtent rotation_extent{FeatureRotationExtent::Angle};
    std::vector<ExtrusionParameters::EndTarget> targets;
    FeatureUpToParameters up_to;
    // Positive removes material away from the neutral Sketch plane.
    double draft_angle_degrees{};
    bool axis_enabled{true};
    bool twist_reverse{};
    bool twist_smooth{true};
    bool operator==(const FeatureSideParameters&) const = default;
};

// Drop only relationships whose sources are no longer immediate neighbours.
// Existing pairs retain their authored choice even if their display order flips.
void prune_feature_up_to_patch_choices(FeatureSideParameters& side);
struct FeatureParameters;
bool resolve_feature_up_to_choices(FeatureParameters& parameters,const std::string& container_id,
    const std::string& feature_id,const std::vector<kernel::ResolvedOrderedUpToLimit>& resolutions);

// The editing definition is independent of calculated OCCT operands. Start/End
// ancestry uses the owning container/feature and Sketch IDs, not operation kind.
struct FeatureParameters {
    // Last generated display name; empty (or different from the container name)
    // means a user-authored name. This has no geometric/reference meaning.
    std::string automatic_name;
    FeatureType type{FeatureType::Modeling};
    std::string sketch_id;
    std::string axis_segment_id;
    ProfileSource profile_source{ProfileSource::Internal};
    double profile_plane_offset{};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1};
    ThinMode thin_mode{ThinMode::OneSide};
    bool symmetric{};
    // Reverse physical directions without moving either logical side definition.
    bool reversed_sides{};
    bool origin_centerline{};
    bool centroid_centerline{};
    bool show_point{};
    bool show_text{};
    // UI Side 1 is End; Side 2 is Start. Order never changes with geometry.
    std::array<FeatureSideParameters,2> sides{{
        {FeatureSideOperation::Extrusion}, {FeatureSideOperation::None}}};
    [[nodiscard]] bool side_direction_reversed(std::size_t side) const {
        if(side>=sides.size())throw std::out_of_range("Invalid Feature side");
        return reversed_sides!=(side==1);
    }
    [[nodiscard]] double side_direction_sign(std::size_t side) const {
        return side_direction_reversed(side)?-1.:1.;
    }
    [[nodiscard]] FeatureSideParameters effective_side(std::size_t side) const {
        if(side>=sides.size())throw std::out_of_range("Invalid Feature side");
        auto value=sides[symmetric?0:side];
        if(type==FeatureType::Axis) {
            value.operation=value.axis_enabled?FeatureSideOperation::Extrusion:FeatureSideOperation::None;
            if(value.extrusion_extent==EndCondition::ThroughAll)value.extrusion_extent=EndCondition::Length;
            value.draft_angle_degrees=0;
            if(value.extrusion_extent!=EndCondition::UpTo)value.targets.clear();
        } else if(type!=FeatureType::Modeling) value.operation=FeatureSideOperation::None;
        return value;
    }
    [[nodiscard]] bool sketch_only() const {
        return type!=FeatureType::Modeling ||
            (effective_side(0).operation==FeatureSideOperation::None &&
             effective_side(1).operation==FeatureSideOperation::None);
    }
    [[nodiscard]] bool shows_sketch() const {
        return type==FeatureType::Sketch || (type==FeatureType::Modeling&&sketch_only());
    }
    [[nodiscard]] bool uses_sketch() const {
        return type==FeatureType::Sketch || type==FeatureType::Modeling;
    }
    bool operator==(const FeatureParameters&) const = default;
};

} // namespace zima::document
