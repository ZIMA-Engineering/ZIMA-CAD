#pragma once
#include <zima/document/feature_parameters.hpp>

namespace zima::document {
inline bool feature_uses_extended_up_to(const FeatureParameters& parameters) {
    if(parameters.type!=FeatureType::Modeling)return false;
    for(std::size_t side=0;side<(parameters.symmetric?1u:2u);++side) {
        const auto& value=parameters.effective_side(side);
        if(value.operation==FeatureSideOperation::Extrusion&&value.extrusion_extent==EndCondition::UpTo&&
                (value.targets.size()>1||value.up_to.length_correction!=0))return true;
        if(value.operation==FeatureSideOperation::Revolution&&value.rotation_extent==FeatureRotationExtent::UpTo&&
                (value.targets.size()>1||value.up_to.angle_correction_degrees!=0))return true;
    }
    return false;
}
} // namespace zima::document
