#pragma once

#include <zima/viewer/picking.hpp>

#include <vector>
#include <algorithm>

namespace zima::app {

[[nodiscard]] inline bool placement_reference_exists(
    const zima::viewer::ViewerCandidate& candidate,
    const zima::kernel::ViewerReferenceGeometry& geometry,
    const std::string& local_path) {
    const auto matches=[&](const auto& ref) {
        return ref.owner_id==candidate.owner_id && ref.semantic_key==candidate.semantic_key &&
            ref.instance_path==local_path;
    };
    return std::ranges::any_of(geometry.points,[&](const auto& p){return matches(p.reference);}) ||
        std::ranges::any_of(geometry.edges,[&](const auto& e){return matches(e.reference);}) ||
        std::ranges::any_of(geometry.axes,[&](const auto& a){return matches(a.reference);}) ||
        std::ranges::any_of(geometry.triangle_references,matches);
}

[[nodiscard]] inline std::vector<zima::viewer::CandidateKind>
placement_reference_candidate_kinds() {
    using zima::viewer::CandidateKind;
    return {CandidateKind::Vertex, CandidateKind::Axis, CandidateKind::Edge,
        CandidateKind::SketchPoint, CandidateKind::SketchSegment, CandidateKind::SketchCurve,
        CandidateKind::Plane, CandidateKind::Face, CandidateKind::Dimension};
}

[[nodiscard]] inline bool construction_reference_candidate_is_persisted_origin_display(
    const zima::viewer::ViewerCandidate& candidate) {
    return candidate.geometry == zima::viewer::CandidateGeometry::Display &&
        candidate.semantic_key.starts_with("origin:");
}

// Placement/reference commands consume persisted ZIMA entities, never live
// result-body topology. Construction datum geometry is intentionally stored
// in the display packet (it needs no OCCT body), so admit its point/axis/plane
// entities alongside Document Origin. A calculated Part Body carries the
// same persisted source identity on every surviving visible Face, Edge and
// Vertex fragment; those Display candidates are stable rather than anonymous
// OCCT result topology. Persisted original curves constrain the origin to their
// locus and supply the tangent at that location. Centers and axes are selected
// explicitly. Result-body/transient edges remain barred.
[[nodiscard]] inline bool placement_reference_candidate_has_stable_geometry(
    const zima::viewer::ViewerCandidate& candidate) {
    using zima::viewer::CandidateGeometry;
    using zima::viewer::CandidateKind;
    if ((candidate.geometry == CandidateGeometry::Display ||
         candidate.geometry == CandidateGeometry::OriginalReference) &&
        (candidate.kind == CandidateKind::SketchPoint ||
         candidate.kind == CandidateKind::SketchSegment || candidate.kind == CandidateKind::SketchCurve))
        return !candidate.owner_id.empty() && !candidate.semantic_key.empty();
    if (candidate.geometry == CandidateGeometry::Display) {
        return ((candidate.kind == CandidateKind::Face ||
                 candidate.kind == CandidateKind::Edge ||
                 candidate.kind == CandidateKind::Vertex) &&
                !candidate.owner_id.empty() && !candidate.semantic_key.empty()) ||
            candidate.semantic_key.starts_with("origin:") ||
            (candidate.kind == CandidateKind::Plane &&
             candidate.semantic_key == "plane") ||
            (candidate.kind == CandidateKind::Axis &&
             candidate.semantic_key == "axis") ||
            (candidate.kind == CandidateKind::Vertex &&
             candidate.semantic_key == "point");
    }
    if (candidate.geometry != CandidateGeometry::OriginalReference) return false;
    return candidate.kind == CandidateKind::Vertex ||
        candidate.kind == CandidateKind::Axis ||
        candidate.kind == CandidateKind::Edge ||
        candidate.kind == CandidateKind::Face ||
        (candidate.kind == CandidateKind::Plane &&
         (candidate.semantic_key == "plane" ||
          candidate.semantic_key.starts_with("origin:plane:")));
}

// Once a Point has fixed the container origin, a second Point is a valid
// direction vector from that origin, just like a linear Edge/Axis or a
// Plane/Face normal. Keep this separate from candidate_drives_rotation(): a
// bare first Point still fixes position only and must not orient a container
// until it is deliberately entered in a following direction row.
[[nodiscard]] inline bool placement_reference_candidate_can_define_direction(
    const zima::viewer::ViewerCandidate& candidate) {
    using zima::viewer::CandidateKind;
    if (!placement_reference_candidate_has_stable_geometry(candidate))
        return false;
    return candidate.kind == CandidateKind::Vertex ||
        candidate.kind == CandidateKind::SketchPoint ||
        candidate.kind == CandidateKind::SketchSegment || candidate.kind == CandidateKind::SketchCurve ||
        candidate.kind == CandidateKind::Axis ||
        candidate.kind == CandidateKind::Edge ||
        candidate.kind == CandidateKind::Plane ||
        candidate.kind == CandidateKind::Face;
}

// Shared static admission rules for Point/Axis/Plane reference picking.
[[nodiscard]] inline bool construction_reference_candidate_passes_static_filters(
    const zima::viewer::ViewerCandidate& candidate,
    bool orientation_reference,
    bool owns_reference_owner,
    bool unavailable_owner) {
    if (candidate.owner_id.empty() || candidate.semantic_key.empty()) return false;
    if (!placement_reference_candidate_has_stable_geometry(candidate)) return false;
    if (owns_reference_owner || unavailable_owner) return false;
    static_cast<void>(orientation_reference);
    return true;
}

}  // namespace zima::app
