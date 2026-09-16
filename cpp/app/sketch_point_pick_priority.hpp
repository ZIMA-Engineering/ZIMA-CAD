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
} // namespace zima::app
