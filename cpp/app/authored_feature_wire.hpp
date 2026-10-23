#pragma once
#include <zima/document/feature_material_wire.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/feature_up_to.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/workspace/operation_input.hpp>

namespace zima::app {
// Use the command's analytical preview, rather than its calculated topology.
// The viewer prepares this only when its source changes and caches one GPU batch.
inline std::vector<kernel::ViewerEdge> authored_feature_wire(
        const document::DocumentSession& session,const std::string& owner) {
    const auto& doc=session.document();
    const auto* feature=doc.find_container(owner);
    if(!feature||feature->suppressed)return {};
    kernel::ViewerMesh wire;
    const auto* body=doc.body_owner_for_object(owner);
    const auto place=[&](kernel::ViewerMesh mesh) {
        if(body)mesh=doc.place_body_mesh(std::move(mesh),body->scope.id);
        return std::move(mesh.edges);
    };
    const auto* input=workspace::calculated_operation_input(session,owner);
    const kernel::ViewerMesh empty;
    const auto& input_mesh=input?input->mesh:empty;
    using document::FeatureKind;
    switch(feature->feature_kind) {
    case FeatureKind::Feature:
        if(feature->feature.type!=document::FeatureType::Modeling)break;
        // Ordered clipping is calculated only on OK. Reuse its own first
        // stored operand wire below, before later body Booleans can add edges.
        if(document::feature_uses_extended_up_to(feature->feature))break;
        wire.edges=doc.feature_preview_edges(*feature,input_mesh);return place(std::move(wire));
    case FeatureKind::Extrusion:
        wire.edges=doc.extrusion_preview_edges(*feature,input_mesh);return place(std::move(wire));
    case FeatureKind::Revolution:
        wire.edges=doc.revolution_preview_edges(*feature);return place(std::move(wire));
    case FeatureKind::Bend:
    case FeatureKind::Flat:
    case FeatureKind::Holes: {
        const auto id=feature->feature_kind==FeatureKind::Bend?feature->bend.sketch_id:
            feature->feature_kind==FeatureKind::Flat?feature->flat.sketch_id:feature->holes.sketch_id;
        const auto sketch=std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);
        if(sketch==doc.sketches.end())return {};
        if(feature->feature_kind==FeatureKind::Bend)wire=document::bend_preview(*feature,*sketch,document::sheet_metal_defaults(doc));
        else if(feature->feature_kind==FeatureKind::Flat)wire=document::flat_preview(*feature,*sketch,document::sheet_metal_defaults(doc));
        else wire=document::holes_preview(*feature,*sketch);
        return place(std::move(wire));
    }
    case FeatureKind::Hole:
    case FeatureKind::TwistedSheet:
        wire.edges=doc.primitive_preview_edges(*feature,input_mesh);return place(std::move(wire));
    case FeatureKind::Thread:
        wire.edges=doc.thread_edges(*feature,input?&input_mesh:nullptr);return place(std::move(wire));
    case FeatureKind::Sweep2D:
        return place(document::PartDocument::sweep2d_preview_mesh(*feature));
    case FeatureKind::HelicalSweep:
        wire.edges=document::PartDocument::helical_preview_edges(*feature);return place(std::move(wire));
    case FeatureKind::SheetTransition:
        return place(document::sheet_transition_preview(*feature,document::sheet_metal_defaults(doc)));
    default:break;
    }
    // Use the first stored contribution, at the creation boundary. In sheet
    // history even the original-reference packet of a later state can carry
    // changed material geometry; it is not the authored inspection wire.
    const auto& calculated=session.calculated_boundaries();
    if(calculated.empty())return {};
    const auto* boundaries=&calculated;
    bool local_body=false;
    if(body)if(const auto found=calculated.back().body_boundaries.find(body->scope.id);
            found!=calculated.back().body_boundaries.end()) {
        boundaries=&found->second.get();local_body=true;
    }
    for(const auto& boundary:*boundaries) {
        for(const auto& edge:boundary.mesh.original_references.edges)
            if(zima::document::feature_material_wire_edge(*feature,edge))
                wire.edges.push_back(edge);
        if(!wire.edges.empty())break;
    }
    if(local_body)wire=doc.place_body_mesh(std::move(wire),body->scope.id);
    return std::move(wire.edges);
}
} // namespace zima::app
