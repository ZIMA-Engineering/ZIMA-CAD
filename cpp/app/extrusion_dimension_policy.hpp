#pragma once

#include <zima/document/part_document.hpp>

#include <cmath>
#include <optional>

namespace zima::app {

// Mirrors the established Container Properties dimension contract: derive
// displayed dimensions from the loaded feature definition, omit inactive and
// zero values, and let Symmetric use the forward value/condition on both sides.
// Up-to and Through-all retain numeric fallback values in the document, but
// those values are not active dimensions.
[[nodiscard]] inline std::optional<double> extrusion_length_dimension_value(
    const zima::document::ExtrusionParameters& parameters, bool reverse) {
    if (parameters.extent != zima::document::ExtrusionExtent::Blind) {
        return std::nullopt;
    }
    double value = parameters.length_forward;
    auto condition = parameters.end_condition_forward;
    if (reverse) {
        if (parameters.extent_mode ==
                zima::document::ProfileExtentMode::OneSide) {
            return std::nullopt;
        }
        if (parameters.extent_mode ==
                zima::document::ProfileExtentMode::TwoSides) {
            value = parameters.length_reverse;
            condition = parameters.end_condition_reverse;
        }
    }
    if (condition != zima::document::EndCondition::Length ||
        std::abs(value) <= 1.0e-9) {
        return std::nullopt;
    }
    return value;
}

}  // namespace zima::app
