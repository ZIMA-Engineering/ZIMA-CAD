#pragma once
#include <zima/document/feature_parameters.hpp>
#include <nlohmann/json_fwd.hpp>

namespace zima::document {
void validate_feature_parameters(const FeatureParameters&);
[[nodiscard]] nlohmann::json serialize_feature_parameters(const FeatureParameters&);
[[nodiscard]] FeatureParameters load_feature_parameters(const nlohmann::json&);
}
