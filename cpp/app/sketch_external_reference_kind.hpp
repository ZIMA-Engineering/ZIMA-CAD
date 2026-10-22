#pragma once
#include <zima/viewer/picking.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/face_curve_reference.hpp>
#include "construction_reference_candidate_policy.hpp"

namespace zima::app {
inline bool sketch_external_reference_has_stable_geometry(
        const viewer::ViewerCandidate& candidate,bool profile) {
    if(profile)return candidate.geometry==viewer::CandidateGeometry::Display;
    return candidate.geometry==viewer::CandidateGeometry::OriginalReference||
        (candidate.kind==viewer::CandidateKind::Face&&
         placement_reference_candidate_has_stable_geometry(candidate));
}

// The caller retains the offered Display candidate for hover/confirmation.
// Only source validation/storage follows the existing persisted parent chain.
inline viewer::ViewerCandidate sketch_external_reference_source_candidate(
        viewer::ViewerCandidate candidate) {
    return placement_reference_source_candidate(std::move(candidate));
}

inline std::optional<sketcher::ExternalReferenceKind> sketch_external_reference_kind(
        const viewer::ViewerCandidate& candidate) {
    using Kind=viewer::CandidateKind;
    switch(candidate.kind) {
    case Kind::Edge:case Kind::SketchSegment:case Kind::SketchCurve:
        if(sketcher::is_face_curve_key(candidate.semantic_key))return sketcher::ExternalReferenceKind::Face;
        return sketcher::ExternalReferenceKind::Edge;
    case Kind::Vertex:case Kind::SketchPoint:return sketcher::ExternalReferenceKind::Point;
    case Kind::Axis:return sketcher::ExternalReferenceKind::Axis;
    case Kind::Face:case Kind::Plane:return sketcher::ExternalReferenceKind::Face;
    default:return {};
    }
}
}
