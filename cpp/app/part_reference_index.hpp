#pragma once
#include <zima/document/part_document.hpp>
#include <zima/workspace/reference_index.hpp>
#include "construction_reference_index.hpp"
#include <zima/document/general_surface.hpp>
#include <zima/kernel/placement_route.hpp>

namespace zima::app {
// Tree diagnostics query the same persisted reference owners as placement.
// Native Sketch points/curves exist independently of calculated solid bodies.
inline workspace::ReferenceIndex part_reference_index(const document::PartDocument& document,
        const kernel::ViewerReferenceGeometry& calculated = {}) {
    workspace::ReferenceIndex index;
    index.add_geometry(calculated);
    index.add_geometry(document.origin_viewer_mesh().original_references);
    index.add_geometry(document.body_origin_reference_geometry());
    for(auto row:document.body_properties) {
        row.visible=true;
        index.add_geometry(document::body_properties_origin(row).original_references);
    }
    index.add_geometry(document.history_origin_reference_geometry_before(""));
    const auto constructions=document.construction_viewer_mesh().original_references;
    const auto sketches=document.sketch_placement_reference_geometry();
    index.add_geometry(constructions);
    index.add_geometry(sketches);
    // A whole route is an authored aggregate of exact native edges. Register
    // only routes actually referenced by this document, using the same route
    // resolver as placement rather than treating the aggregate as a lost edge.
    const auto add_routes=[&](const auto& references) {
        for(const auto& ref:references) {
            if(!ref.semantic_key.starts_with(kernel::placement_route_prefix)||
               index.contains(ref.owner_id,ref.semantic_key,ref.instance_path))continue;
            for(const auto* geometry:{&calculated,&constructions,&sketches})
                if(!kernel::placement_route_pieces(geometry->edges,ref.owner_id,ref.semantic_key,ref.instance_path).empty()) {
                    index.add(ref);break;
                }
        }
    };
    for(const auto& feature:document.history)add_routes(feature.placement.references);
    const auto add_construction_routes=[&](auto&& self,const auto& object)->void {
        add_routes(object.references);
        for(const auto& point:object.curve_points)self(self,point);
    };
    for(const auto& object:document.constructions)add_construction_routes(add_construction_routes,object);
    for(const auto& object:document.constructions)
        add_construction_origin_references(index,object);
    for(const auto& feature:document.history)
        if(feature.feature_kind==document::FeatureKind::Sweep3D && !feature.suppressed)
            add_construction_origin_references(index,feature.sweep3d.path);
        else if(feature.feature_kind==document::FeatureKind::GeneralSurface&&!feature.suppressed) {
            auto definitions=document::general_surface_definition_mesh(feature);
            if(const auto* body=document.body_owner_for_object(feature.id))definitions=document.place_body_mesh(std::move(definitions),body->scope.id);
            index.add_geometry(definitions.original_references);
        }
    return index;
}
}
