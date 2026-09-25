#pragma once

#include <zima/document/profile_parameters.hpp>
#include <stdexcept>
#include <array>

namespace zima::document {

enum class FeatureType { Point, Axis, Plane, Sketch, Modeling };

enum class FeatureSideOperation { None, Extrusion, Revolution };
enum class FeatureRotationExtent { Angle, Full, UpTo };

struct FeatureSideParameters {
    FeatureSideOperation operation{FeatureSideOperation::None};
    // Retain the settings of both operation kinds when switching a side.
    double length{50};
    EndCondition extrusion_extent{EndCondition::Length};
    double angle_degrees{90};
    FeatureRotationExtent rotation_extent{FeatureRotationExtent::Angle};
    std::vector<ExtrusionParameters::EndTarget> targets;
    bool operator==(const FeatureSideParameters&) const = default;
};

// The editing definition is independent of calculated OCCT operands. Start/End
// ancestry uses the owning container/feature and Sketch IDs, not operation kind.
struct FeatureParameters {
    FeatureType type{FeatureType::Modeling};
    std::string sketch_id;
    std::string axis_segment_id;
    ProfileSource profile_source{ProfileSource::Internal};
    double profile_plane_offset{};
    ProfileResultType result_type{ProfileResultType::Solid};
    double thin_thickness{1};
    ThinMode thin_mode{ThinMode::OneSide};
    bool symmetric{};
    bool origin_centerline{};
    bool centroid_centerline{};
    // UI Side 1 is End; Side 2 is Start. Order never changes with geometry.
    std::array<FeatureSideParameters,2> sides{{
        {FeatureSideOperation::Extrusion}, {FeatureSideOperation::None}}};
    [[nodiscard]] FeatureSideParameters effective_side(std::size_t side) const {
        if(side>=sides.size())throw std::out_of_range("Invalid Feature side");
        auto value=sides[symmetric?0:side];
        if(type==FeatureType::Axis) {
            value.operation=FeatureSideOperation::Extrusion;
            value.extrusion_extent=EndCondition::Length;
            value.targets.clear();
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
