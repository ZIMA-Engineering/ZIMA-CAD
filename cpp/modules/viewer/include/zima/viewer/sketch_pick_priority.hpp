#pragma once
#include <zima/viewer/picking.hpp>
namespace zima::viewer {
// Ordinary editing: authored geometry wins over coincident helper/reference
// geometry. Keep all candidates available for explicit right-button cycling.
inline int sketch_edit_pick_priority(const ViewerCandidate& candidate) {
    using K=CandidateKind;
    // A corner-radius handle is an explicit drag grip. At approximately 1:1
    // small radii can put it inside the ordinary vertex hit tolerance; the
    // grip must win that overlap while RMB still exposes the native point.
    if(candidate.kind==K::SketchPoint&&
            candidate.semantic_key.starts_with("corner_radius_handle:"))return -1;
    if(candidate.kind==K::SketchPoint)return 0;
    // Visible dimension values retain their established direct edit contract.
    if(candidate.kind==K::Dimension)return 1;
    if(candidate.kind==K::SketchSegment||candidate.kind==K::SketchCurve||
       candidate.kind==K::SketchText||candidate.kind==K::Symbol)return 2;
    // Relation glyphs commonly sit directly on the geometry they describe.
    // Prefer that authored geometry for the ordinary hover/click while keeping
    // the annotation in the same candidate list for RMB cycling.
    if(candidate.kind==K::SketchConstraint)return 3;
    if(candidate.kind==K::SketchExternalReference)return 4;
    if(candidate.kind==K::SketchAxis||candidate.kind==K::Axis)return 5;
    return 6;
}
} // namespace zima::viewer
