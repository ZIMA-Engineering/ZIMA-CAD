#pragma once
#include "sketch_profile_wire.hpp"
#include "workspace/edge_treatment_preview.hpp"
#include <zima/document/feature_material_wire.hpp>
#include <zima/document/edge_treatment_selection.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/feature_up_to.hpp>
#include <zima/document/bend.hpp>
#include <zima/document/flat.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sheet_transition.hpp>
#include <zima/document/loft.hpp>
#include <zima/document/sheet_form_definition.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/workspace/operation_input.hpp>
#include <zima/kernel/mirror_geometry.hpp>

namespace zima::app {
inline bool authored_sketch_profile(const document::HistoryContainer& feature) {
    return feature.feature_kind==document::FeatureKind::Sketch||
        (feature.feature_kind==document::FeatureKind::Feature&&feature.feature.shows_sketch());
}
// Use the command's analytical preview, rather than its calculated topology.
// The viewer prepares this only when its source changes and caches one GPU batch.
inline std::vector<kernel::ViewerEdge> authored_feature_wire(
        const document::DocumentSession& session,const std::string& owner) {
    const auto& doc=session.document();
    const auto selected_sketch=std::ranges::find(doc.sketches,owner,&sketcher::Sketch::id);
    const auto* feature=doc.find_container(selected_sketch==doc.sketches.end()?owner:selected_sketch->owner_container_id);
    if(!feature||feature->suppressed)return {};
    kernel::ViewerMesh wire;
    const auto* body=doc.body_owner_for_object(feature->id);
    const auto place=[&](kernel::ViewerMesh mesh) {
        if(body)mesh=doc.place_body_mesh(std::move(mesh),body->scope.id);
        return std::move(mesh.edges);
    };
    if(selected_sketch!=doc.sketches.end()||authored_sketch_profile(*feature)) {
        const auto sketch=selected_sketch!=doc.sketches.end()?selected_sketch:
            std::ranges::find(doc.sketches,feature->id,&sketcher::Sketch::owner_container_id);
        if(sketch==doc.sketches.end()||sketch->suppressed)return {};
        wire=sketch->viewer_mesh();sketch->filter_hidden_3d_geometry(wire);
        std::erase_if(wire.edges,[](const auto& edge){return !native_sketch_profile_edge(edge);});
        // A pure Sketch has no material operand. Its calculated datum packet
        // may contain interpolated profile supports; selection must use the
        // actual Sketch curves, exactly like its ordinary visible profile.
        return place(std::move(wire));
    }
    const auto* input=workspace::calculated_operation_input(session,owner);
    const auto has_spatial_bounds=[](const kernel::ViewerMesh& mesh) {
        if(!mesh.vertices.empty())return true;
        const auto has_points=[](const auto& edges) {
            return std::ranges::any_of(edges,[](const auto& edge) {
                return !edge.construction&&!edge.points.empty();
            });
        };
        return has_points(mesh.edges)||has_points(mesh.original_references.edges);
    };
    // A final-first cache may omit an intermediate input mesh. For a Through
    // All extrusion, its persisted operation result still carries the exact
    // finite span created from that input. Reuse only that already calculated
    // packet so ordinary selection remains analytical and never invokes OCCT.
    if(feature->feature_kind==document::FeatureKind::Feature&&
            feature->feature.type==document::FeatureType::Modeling&&
            (!input||!has_spatial_bounds(input->mesh))) {
        bool through_all{};
        for(std::size_t side=0;side<2;++side) {
            const auto parameters=feature->feature.effective_side(side);
            through_all|=parameters.operation==document::FeatureSideOperation::Extrusion&&
                parameters.extrusion_extent==document::EndCondition::ThroughAll;
        }
        if(through_all)if(const auto* output=
                workspace::calculated_operation_output(session,owner);
                output&&has_spatial_bounds(output->mesh))input=output;
    }
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
    case FeatureKind::LineBend:
    case FeatureKind::Flat:
    case FeatureKind::Holes: {
        const auto id=feature->feature_kind==FeatureKind::Bend?feature->bend.sketch_id:
            feature->feature_kind==FeatureKind::LineBend?feature->line_bend.sketch_id:
            feature->feature_kind==FeatureKind::Flat?feature->flat.sketch_id:feature->holes.sketch_id;
        const auto sketch=std::ranges::find(doc.sketches,id,&sketcher::Sketch::id);
        if(sketch==doc.sketches.end())return {};
        if(feature->feature_kind==FeatureKind::Bend)wire=document::bend_preview(*feature,*sketch,document::sheet_metal_defaults(doc));
        else if(feature->feature_kind==FeatureKind::LineBend)
            wire.edges=sketch->viewer_mesh().edges;
        else if(feature->feature_kind==FeatureKind::Flat)wire=document::flat_preview(*feature,*sketch,document::sheet_metal_defaults(doc));
        else wire=document::holes_preview(*feature,*sketch);
        return place(std::move(wire));
    }
    case FeatureKind::Hole:
    case FeatureKind::TwistedSheet:
        wire.edges=doc.primitive_preview_edges(*feature,input_mesh);return place(std::move(wire));
    case FeatureKind::Thread:
        wire.edges=doc.thread_edges(*feature,input?&input_mesh:nullptr);return place(std::move(wire));
    case FeatureKind::SheetForm: {
        // Match the FORM Properties tool preview, not supporting stock-edge
        // fragments introduced by its Boolean intersection with the sheet.
        wire.edges=document::sheet_form_preview_edges(feature->sheet_form);
        const auto& p=feature->placement;
        const auto frame=kernel::annotation_frame({p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z});
        for(auto& edge:wire.edges)for(auto& point:edge.points)point=frame.world(point);
        return place(std::move(wire));
    }
    case FeatureKind::Loft:
        return place(document::loft_definition_mesh(doc,*feature));
    case FeatureKind::Sweep2D:
        return place(document::PartDocument::sweep2d_preview_mesh(*feature));
    case FeatureKind::HelicalSweep:
        wire.edges=document::PartDocument::helical_preview_edges(*feature);return place(std::move(wire));
    case FeatureKind::SheetTransition:
        return place(document::sheet_transition_preview(*feature,document::sheet_metal_defaults(doc)));
    case FeatureKind::Fillet:
    case FeatureKind::Chamfer:
        // Rebuild the same analytical outer cage used by creation/Properties:
        // two longitudinal boundary rails plus the start/end Fillet arcs or
        // Chamfer sections. The persisted input edge defines the operation but
        // is not itself the authored shape presented for ordinary selection.
        // This uses only the already calculated input packet and never invokes
        // OCCT or extracts edges from the later Boolean result.
        if (input) {
            std::vector<std::vector<kernel::ViewerEdge>> groups;
            groups.reserve(feature->edge_treatment.routes.size());
            for (std::size_t route=0;
                    route<feature->edge_treatment.routes.size();++route) {
                groups.push_back(document::treatment_selection_wire(
                    feature->edge_treatment,route,std::nullopt,input_mesh));
            }
            wire.edges=workspace_detail::edge_treatment_preview_wire(
                groups,feature->edge_treatment,feature->feature_kind,
                feature->id,&input_mesh).edges;
            return place(std::move(wire));
        }
        return {};
    case FeatureKind::DerivedCopy: {
        // Mirror/Pattern copy the source's complete authored tool wire. A
        // downstream Boolean can consume or split source edges; those result
        // fragments cannot define the copy's creation/inspection preview.
        kernel::ViewerMesh source;
        for(const auto& source_id:feature->derived_copy.source_ids) {
            auto edges=authored_feature_wire(session,source_id);
            source.edges.insert(source.edges.end(),
                std::make_move_iterator(edges.begin()),
                std::make_move_iterator(edges.end()));
        }
        if(body) {
            kernel::ViewerReferenceGeometry geometry;geometry.edges=std::move(source.edges);
            source.edges=doc.construction_reference_geometry_for(body->scope.id,geometry).edges;
        }
        auto parameters=feature->derived_copy;
        document::PartDocument::resolve_copy_reference(parameters,feature->id,feature->placement,{});
        if(parameters.pattern) {
            const auto pattern=kernel::validated_pattern(*parameters.pattern);
            for(unsigned i=1;i<pattern.count;++i) {
                auto copy=kernel::pattern_copy_mesh(source,pattern,i);
                wire.edges.insert(wire.edges.end(),copy.edges.begin(),copy.edges.end());
            }
        } else wire=kernel::mirrored_viewer_mesh(std::move(source),parameters.resolved_plane,feature->id);
        return place(std::move(wire));
    }
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
