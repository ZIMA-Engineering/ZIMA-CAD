#include <zima/viewer/measurement.hpp>
namespace zima::viewer {
using K=kernel::MeasurementKind;
std::optional<kernel::MeasurementReference> measurement_reference(const ViewerCandidate& candidate){
    K kind;
    switch(candidate.kind){
    case CandidateKind::Vertex:case CandidateKind::SketchPoint:kind=K::Point;break;
    case CandidateKind::Edge:case CandidateKind::SketchSegment:case CandidateKind::SketchCurve:kind=K::Curve;break;
    case CandidateKind::SketchExternalReference:kind=candidate.semantic_key.starts_with("external_point:")?K::Point:K::Curve;break;
    case CandidateKind::Face:kind=candidate.semantic_key=="plane"||candidate.semantic_key.starts_with("origin:plane:")?K::Plane:K::Face;break;
    case CandidateKind::Plane:kind=K::Plane;break;
    case CandidateKind::Axis:case CandidateKind::SketchAxis:kind=K::Axis;break;
    case CandidateKind::Container:case CandidateKind::Occurrence:kind=K::Object;break;
    default:return {};
    }
    // Display body topology is not a stable reference owner.
    if((kind==K::Face||kind==K::Curve)&&candidate.geometry==CandidateGeometry::Display&&
       (candidate.semantic_key.empty()||candidate.semantic_key=="container:display"))return {};
    return kernel::MeasurementReference{kind,candidate.owner_id,candidate.semantic_key,candidate.instance_path};
}
}
