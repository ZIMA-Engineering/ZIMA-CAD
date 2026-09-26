#pragma once
#include <zima/document/part_document.hpp>

namespace zima::document {
// Presentation capability, independent of the placement solver's contract.
inline bool has_origin_display_controls(FeatureKind kind) {
    switch (kind) {
    case FeatureKind::Feature: case FeatureKind::Extrusion: case FeatureKind::Revolution:
    case FeatureKind::Sweep2D: case FeatureKind::Sweep3D: case FeatureKind::HelicalSweep:
    case FeatureKind::Hole: case FeatureKind::Thread: case FeatureKind::ImportedStep:
    case FeatureKind::TwistedSheet: case FeatureKind::SheetTransition:
    case FeatureKind::Flat: case FeatureKind::Bend: case FeatureKind::Holes:
        return true;
    default: return false;
    }
}
inline bool same_except_origin_display(HistoryContainer first, const HistoryContainer& second) {
    first.origin_point_visible=second.origin_point_visible;
    first.origin_text_visible=second.origin_text_visible;
    first.feature.show_point=second.feature.show_point;
    first.feature.show_text=second.feature.show_text;
    return first==second;
}
inline zima::kernel::ViewerPoint container_origin_marker(const HistoryContainer& value, bool editing=false) {
    const auto& p=value.placement;
    zima::kernel::ViewerPoint point{{p.x,p.y,p.z},
        {value.id,"container:origin-marker",{}},
        editing||value.origin_text_visible ? value.name : std::string{},
        editing||value.origin_point_visible};
    point.display_owner_id=value.id;
    return point;
}
}
