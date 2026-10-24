#pragma once
#include <zima/viewer/picking.hpp>
namespace zima::viewer {
// Ordinary editing: authored geometry wins over coincident helper/reference
// geometry. Keep all candidates available for explicit right-button cycling.
inline int sketch_edit_pick_priority(const ViewerCandidate& candidate) {
    using K=CandidateKind;
    if(candidate.kind==K::SketchPoint)return 0;
    if(candidate.kind==K::Dimension||candidate.kind==K::SketchConstraint)return 1;
    if(candidate.kind==K::SketchSegment||candidate.kind==K::SketchCurve||candidate.kind==K::SketchText)return 2;
    if(candidate.kind==K::SketchExternalReference)return 3;
    if(candidate.kind==K::SketchAxis||candidate.kind==K::Axis)return 4;
    return 5;
}
} // namespace zima::viewer
