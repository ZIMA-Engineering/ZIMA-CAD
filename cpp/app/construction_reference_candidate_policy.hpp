#pragma once

#include <zima/viewer/picking.hpp>

namespace zima::app {

[[nodiscard]] inline bool construction_reference_candidate_is_persisted_origin_display(
    const zima::viewer::ViewerCandidate& candidate) {
    return candidate.geometry == zima::viewer::CandidateGeometry::Display &&
        candidate.semantic_key.starts_with("origin:");
}

// Shared static admission rules for Point/Axis/Plane reference picking.
// FRONT/TOP orientation rows deliberately reject every `origin:plane:*`
// candidate: the document's built-in datum planes looked selectable but were
// confusing no-ops there, while a container-origin plane is only derived
// preview geometry from the container's already-resolved rotation, not an
// independent stable anchor that could define that rotation without a cycle.
[[nodiscard]] inline bool construction_reference_candidate_passes_static_filters(
    const zima::viewer::ViewerCandidate& candidate,
    bool orientation_reference,
    bool owns_reference_owner,
    bool unavailable_owner) {
    if (candidate.owner_id.empty() || candidate.semantic_key.empty()) return false;
    if (candidate.geometry != zima::viewer::CandidateGeometry::OriginalReference &&
        !construction_reference_candidate_is_persisted_origin_display(candidate)) {
        return false;
    }
    if (owns_reference_owner || unavailable_owner) return false;
    if (orientation_reference &&
        candidate.semantic_key.starts_with("origin:plane:")) {
        return false;
    }
    return true;
}

}  // namespace zima::app
