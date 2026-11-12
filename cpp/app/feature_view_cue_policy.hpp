#pragma once
#include <zima/document/feature_parameters.hpp>

namespace zima::app::workspace_detail {

inline bool shows_feature_direction_label(
        const document::FeatureParameters& parameters) {
    return parameters.type == document::FeatureType::Axis ||
        (parameters.type == document::FeatureType::Modeling &&
            !parameters.planar_surface());
}

} // namespace zima::app::workspace_detail
