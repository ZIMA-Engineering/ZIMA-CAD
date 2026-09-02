#pragma once

#include <zima/document/part_document.hpp>

#include <cmath>
#include <optional>

namespace zima::app {

[[nodiscard]] inline bool extrusion_has_numeric_axial_extent(
    const zima::document::ExtrusionParameters& parameters) {
    if (parameters.extent != zima::document::ExtrusionExtent::Blind ||
        parameters.end_condition_forward !=
            zima::document::EndCondition::Length) {
        return false;
    }
    return parameters.extent_mode !=
               zima::document::ProfileExtentMode::TwoSides ||
        parameters.end_condition_reverse ==
            zima::document::EndCondition::Length;
}

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

// Profile-plane offset and extrusion length lie on the same operation axis.
// In Up-to/Through-all mode that axis has no user-defined numeric endpoint,
// so exposing the stored offset alongside the target-driven result is both
// visually ambiguous and inconsistent with the no-length-dimension contract.
[[nodiscard]] inline std::optional<double>
extrusion_profile_offset_dimension_value(
    const zima::document::ExtrusionParameters& parameters) {
    if (!extrusion_has_numeric_axial_extent(parameters) ||
        std::abs(parameters.profile_plane_offset) <= 1.0e-9) {
        return std::nullopt;
    }
    return parameters.profile_plane_offset;
}

}  // namespace zima::app
