#pragma once
#include <zima/document/feature_parameters.hpp>

namespace zima::document {
inline void validate_feature_rotation_span(double first,double second) {
    if(first+second>360.0+1e-9)
        throw std::runtime_error("Combined rotation angle must not exceed 360 degrees.");
}

// The edited side wins. Retain inactive settings, source references and side
// identities. Reference-limited angles are checked after exact resolution;
// never shorten an UpTo operation before its selected target.
inline void normalize_feature_rotations(FeatureParameters& p,std::size_t edited) {
    if(p.type!=FeatureType::Modeling)return;
    if(p.symmetric) {
        auto& side=p.sides[0];
        if(side.operation!=FeatureSideOperation::Revolution)return;
        if(side.rotation_extent==FeatureRotationExtent::Full) {
            p.symmetric=false;p.sides[1].operation=FeatureSideOperation::None;
        } else if(side.rotation_extent==FeatureRotationExtent::Angle && side.angle_degrees>180)
            side.angle_degrees=180;
        return;
    }
    auto& first=p.sides.at(edited);auto& other=p.sides.at(1-edited);
    if(first.operation!=FeatureSideOperation::Revolution || other.operation!=FeatureSideOperation::Revolution)return;
    if(first.rotation_extent==FeatureRotationExtent::Full ||
        (first.rotation_extent==FeatureRotationExtent::Angle && first.angle_degrees>=360)) {
        other.operation=FeatureSideOperation::None;return;
    }
    if(other.rotation_extent==FeatureRotationExtent::Full) {
        if(first.rotation_extent==FeatureRotationExtent::UpTo) {
            other.operation=FeatureSideOperation::None;return;
        }
        other.rotation_extent=FeatureRotationExtent::Angle;
        other.angle_degrees=360-first.angle_degrees;
    } else if(first.rotation_extent==FeatureRotationExtent::Angle &&
              other.rotation_extent==FeatureRotationExtent::Angle && first.angle_degrees+other.angle_degrees>360) {
        other.angle_degrees=360-first.angle_degrees;
    }
}
} // namespace zima::document
