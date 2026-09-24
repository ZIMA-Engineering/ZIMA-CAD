#pragma once
#include <zima/viewer/picking.hpp>

namespace zima::app {
// Command-local ordering of the existing common candidates; hit tolerances,
// eligibility and RMB cycling remain owned by the viewer.
inline int sketch_point_pick_priority(const viewer::ViewerCandidate& candidate) {
    if(candidate.kind==viewer::CandidateKind::SketchPoint)return 0;
    if(candidate.kind==viewer::CandidateKind::SketchExternalReference&&
        (candidate.semantic_key.starts_with("external_point:")||
         candidate.semantic_key.starts_with("sketch_midpoint:")||
         candidate.semantic_key.starts_with("sketch_intersection:")||
         candidate.semantic_key.starts_with("sketch_curve_keypoint:")))return 0;
    return 1;
}
inline int sketch_placement_pick_priority(const viewer::ViewerCandidate& candidate) {
    if (candidate.semantic_key.starts_with("sketch_intersection:")) return -1;
    if (candidate.semantic_key.starts_with("sketch_midpoint:")) return 1;
    // A native segment must not hide its own midpoint. External contact C/CC
    // retains precedence; away from the midpoint the segment remains available.
    if (candidate.kind == viewer::CandidateKind::SketchExternalReference) return 0;
    return sketch_point_pick_priority(candidate)==0 ? 0 : 2;
}
} // namespace zima::app
