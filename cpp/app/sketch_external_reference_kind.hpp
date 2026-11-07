#pragma once
#include <zima/viewer/picking.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/sketcher/face_curve_reference.hpp>
#include "construction_reference_candidate_policy.hpp"

namespace zima::app {
inline std::vector<viewer::CandidateKind> sketch_external_reference_candidate_kinds(bool profile,bool intersection=false,bool branches=false) {
    using Kind=viewer::CandidateKind;
    if(intersection)return branches?std::vector{Kind::SketchExternalReference}:std::vector{Kind::Face,Kind::Edge,Kind::SketchSegment,Kind::SketchCurve};
    if(profile)return {Kind::Edge,Kind::Face};
    return {Kind::Edge,Kind::Face,Kind::Plane,Kind::SketchSegment,Kind::SketchCurve,
        Kind::SketchPoint,Kind::Vertex,Kind::Axis};
}

inline bool sketch_external_reference_has_stable_geometry(
        const viewer::ViewerCandidate& candidate,bool profile,bool intersection=false,
        const kernel::ViewerMesh* mesh=nullptr) {
    if(intersection)return candidate.kind==viewer::CandidateKind::Face?candidate.geometry==viewer::CandidateGeometry::Display:
        (candidate.kind==viewer::CandidateKind::Edge||candidate.kind==viewer::CandidateKind::SketchSegment||candidate.kind==viewer::CandidateKind::SketchCurve)&&
        candidate.geometry==viewer::CandidateGeometry::OriginalReference&&!sketcher::is_face_curve_key(candidate.semantic_key);
    if(profile&&candidate.kind==viewer::CandidateKind::Edge)return
        candidate.geometry==viewer::CandidateGeometry::Display||
        (candidate.geometry==viewer::CandidateGeometry::OriginalReference&&
            sketcher::is_face_curve_key(candidate.semantic_key));
    // Whole planes and cylinders expose analytic supports. Other curved faces
    // retain the explicit Intersection path; never use arbitrary bulk outlines.
    if(candidate.kind==viewer::CandidateKind::Face) {
        if(candidate.geometry!=viewer::CandidateGeometry::Display||!mesh||candidate.geometry_index>=mesh->triangle_references.size())return false;
        const auto& face=mesh->triangle_references[candidate.geometry_index];
        const auto supported=[](const auto& reference) {
            return reference.surface&&
                (reference.surface->kind==kernel::SurfaceGeometry::Kind::Plane||
                 reference.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder);
        };
        // Boolean and derived-copy display fragments retain the persisted
        // oldest source identity, but need not duplicate its analytic support
        // on every display triangle.  The original-reference packet is the
        // authority for that support; use the exact source/occurrence only.
        const auto source=placement_reference_source_candidate(candidate);
        const bool follows_parent=source.owner_id!=candidate.owner_id||
            source.semantic_key!=candidate.semantic_key;
        if(!follows_parent&&supported(face))return true;
        if(source.geometry_index>=mesh->original_references.triangle_references.size())
            return false;
        const auto& reference=
            mesh->original_references.triangle_references[source.geometry_index];
        return reference.owner_id==source.owner_id&&
            reference.semantic_key==source.semantic_key&&
            reference.instance_path==source.instance_path&&supported(reference);
    }
    return candidate.kind!=viewer::CandidateKind::Face&&
        (candidate.geometry==viewer::CandidateGeometry::OriginalReference||
         (candidate.kind==viewer::CandidateKind::Edge&&
          candidate.geometry==viewer::CandidateGeometry::Display&&
          sketcher::is_edge_treatment_body_edge_key(candidate.semantic_key)));
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
