#pragma once
#include <zima/viewer/picking.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/face_curve_reference.hpp>

namespace zima::app {
inline std::optional<sketcher::ExternalReferenceKind> sketch_external_reference_kind(
        const viewer::ViewerCandidate& candidate) {
    using Kind=viewer::CandidateKind;
    switch(candidate.kind) {
    case Kind::Edge:case Kind::SketchSegment:case Kind::SketchCurve:
        if(sketcher::is_face_curve_key(candidate.semantic_key))return sketcher::ExternalReferenceKind::Face;
        return sketcher::ExternalReferenceKind::Edge;
    case Kind::Vertex:case Kind::SketchPoint:return sketcher::ExternalReferenceKind::Point;
    case Kind::Axis:return sketcher::ExternalReferenceKind::Axis;
    default:return {};
    }
}
}
