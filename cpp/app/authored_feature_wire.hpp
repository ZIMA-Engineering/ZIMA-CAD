#pragma once
#include <zima/document/document_session.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/workspace/operation_input.hpp>

namespace zima::app {
// Parameter inspection shows the authored tool/contribution at its creation
// boundary. This wire is a non-pickable overlay; it never replaces the current
// state body, resolves placement, or calculates topology.
inline std::vector<kernel::ViewerEdge> authored_feature_wire(
        const document::DocumentSession& session,const std::string& owner) {
    const auto& doc=session.document();
    const auto* feature=doc.find_container(owner);
    if(!feature||feature->suppressed)return {};
    kernel::ViewerMesh wire;
    const auto* input=workspace::calculated_operation_input(session,owner);
    const kernel::ViewerMesh empty;
    const auto& bounds=input?input->mesh:empty;
    using document::FeatureKind;
    if(feature->feature_kind==FeatureKind::Feature)
        wire.edges=doc.feature_preview_edges(*feature,bounds);
    else if(feature->feature_kind==FeatureKind::Extrusion)
        wire.edges=doc.extrusion_preview_edges(*feature,bounds);
    else if(feature->feature_kind==FeatureKind::Revolution)
        wire.edges=doc.revolution_preview_edges(*feature);
    else if(feature->feature_kind==FeatureKind::HelicalSweep)
        wire.edges=document::PartDocument::helical_preview_edges(*feature);
    else if(feature->feature_kind==FeatureKind::Bend||feature->feature_kind==FeatureKind::Flat) {
        const auto sketch_id=feature->feature_kind==FeatureKind::Bend?feature->bend.sketch_id:feature->flat.sketch_id;
        if(const auto sketch=std::ranges::find(doc.sketches,sketch_id,&sketcher::Sketch::id);sketch!=doc.sketches.end())
            wire=feature->feature_kind==FeatureKind::Bend
                ?document::bend_preview(*feature,*sketch,document::sheet_metal_defaults(doc))
                :document::flat_preview(*feature,*sketch,document::sheet_metal_defaults(doc));
    }
    if(!wire.edges.empty()) {
        if(const auto* body=doc.body_owner_for_object(owner))wire=doc.place_body_mesh(std::move(wire),body->scope.id);
        return std::move(wire.edges);
    }
    // Use the first stored contribution, at the creation boundary. In sheet
    // history even the original-reference packet of a later state can carry
    // changed material geometry; it is not the authored inspection wire.
    const auto& calculated=session.calculated_boundaries();
    if(calculated.empty())return {};
    const auto* boundaries=&calculated;
    const auto* body=doc.body_owner_for_object(owner);
    bool local_body=false;
    if(body)if(const auto found=calculated.back().body_boundaries.find(body->scope.id);
            found!=calculated.back().body_boundaries.end()) {
        boundaries=&found->second;local_body=true;
    }
    for(const auto& boundary:*boundaries) {
        for(const auto& edge:boundary.mesh.original_references.edges)
            if(edge.reference.owner_id==owner&&edge.reference.instance_path.empty()&&!edge.parameter_seam)
                wire.edges.push_back(edge);
        if(!wire.edges.empty())break;
    }
    if(local_body)wire=doc.place_body_mesh(std::move(wire),body->scope.id);
    return std::move(wire.edges);
}
} // namespace zima::app
