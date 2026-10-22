#pragma once
#include <zima/viewer/picking.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/face_curve_reference.hpp>
#include "construction_reference_candidate_policy.hpp"

namespace zima::app {
inline std::vector<viewer::CandidateKind> sketch_external_reference_candidate_kinds(bool profile,bool intersection=false,bool branches=false) {
    using Kind=viewer::CandidateKind;
    if(intersection)return branches?std::vector{Kind::SketchExternalReference}:std::vector{Kind::Face};
    if(profile)return {Kind::Edge};
    return {Kind::Edge,Kind::Plane,Kind::SketchSegment,Kind::SketchCurve,
        Kind::SketchPoint,Kind::Vertex,Kind::Axis};
}

inline bool sketch_external_reference_has_stable_geometry(
        const viewer::ViewerCandidate& candidate,bool profile,bool intersection=false) {
    // Ordinary Reference consumes one curve/point/axis/plane. Whole-face
    // entry belongs to the separate intersection command, not this picker.
    if(intersection)return candidate.kind==viewer::CandidateKind::Face&&candidate.geometry==viewer::CandidateGeometry::Display;
    if(profile)return candidate.kind==viewer::CandidateKind::Edge&&
        candidate.geometry==viewer::CandidateGeometry::Display;
    return candidate.kind!=viewer::CandidateKind::Face&&
        candidate.geometry==viewer::CandidateGeometry::OriginalReference;
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
