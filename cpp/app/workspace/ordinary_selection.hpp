#pragma once
#include <zima/document/part_document.hpp>
#include <zima/viewer/picking.hpp>
#include <set>

namespace zima::app::workspace_detail {
// Source aliases remain available through ordinary cycling, but must not
// preempt the actual converted Flats/Bends in Sheet Metal.
inline std::set<std::string> converted_sheet_source_owners(
        const document::PartDocument& document) {
    std::set<std::string> result;
    for (const auto& feature : document.history) {
        if (feature.suppressed ||
                feature.feature_kind != document::FeatureKind::SheetFromBody)
            continue;
        for (const auto& binding : feature.sheet_from_body.child_bindings) {
            if (binding.detached) continue;
            for (const auto& face : binding.faces)
                if (face.reference.valid()) result.insert(face.reference.owner_id);
        }
    }
    return result;
}

inline std::vector<viewer::CandidateKind> ordinary_selection_kinds(
    viewer::SelectionFilter filter, bool part) {
    using Kind = viewer::CandidateKind;
    using Filter = viewer::SelectionFilter;
    switch (filter) {
        case Filter::Faces: return {Kind::Face};
        case Filter::Planes: return {Kind::Plane};
        case Filter::Curves: return {Kind::Edge};
        case Filter::Origins:
        case Filter::Points: return {Kind::Vertex};
        case Filter::Axes: return {Kind::Axis};
        default: return part
            ? std::vector{Kind::Dimension, Kind::Symbol, Kind::Container}
            : std::vector{Kind::Dimension, Kind::Symbol, Kind::Container, Kind::Occurrence};
    }
}
}
