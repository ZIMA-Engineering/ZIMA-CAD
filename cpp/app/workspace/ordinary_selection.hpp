#pragma once
#include <zima/document/part_document.hpp>
#include <zima/viewer/picking.hpp>
#include <map>

namespace zima::app::workspace_detail {
// Source aliases remain available through ordinary cycling, but must not
// preempt the actual converted Flats/Bends in Sheet Metal.
inline std::map<std::string,int> converted_sheet_source_owners(
        const document::PartDocument& document) {
    std::map<std::string,int> result;
    for (const auto& feature : document.history) {
        if (feature.suppressed ||
                feature.feature_kind != document::FeatureKind::SheetFromBody)
            continue;
        for (const auto& binding : feature.sheet_from_body.child_bindings) {
            if (binding.detached) continue;
            for (const auto& face : binding.faces)
                if (face.reference.valid()) {
                    const auto* source=document.find_container(face.reference.owner_id);
                    // Original solid precedes its Fillet when both hidden
                    // source surfaces are hit. This uses authored metadata;
                    // ordinary selection never calculates geometry.
                    result.emplace(face.reference.owner_id,source&&
                        source->feature_kind==document::FeatureKind::Fillet?2:1);
                }
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
