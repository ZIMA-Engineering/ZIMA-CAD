#pragma once
#include "point_station_annotation.hpp"
#include <zima/document/general_surface.hpp>

namespace zima::app {
// Consume the ordinary Point dimension generator in the Curve's local frame.
// The annotation owners remain the native children so the shared value editor
// applies their existing references and locks.
inline kernel::ViewerMesh curve_point_annotations(const document::ConstructionObject& curve,
        const kernel::ViewerReferenceGeometry& local_references) {
    kernel::ViewerMesh mesh;
    if(curve.kind!=document::ConstructionKind::Curve3D)return mesh;
    for(const auto& point:curve.curve_points) {
        if(!point.reference_valid||point.suppressed)continue;
        auto dimensions=document::construction_point_dimensions(point,local_references);
        for(auto& dimension:dimensions) {
            auto& key=dimension.reference.semantic_key;
            if(key.starts_with("parameter:reference_offset:"))key.insert(10,"placement:");
            mesh.dimensions.push_back(std::move(dimension));
        }
        if(auto station=point_station_annotation(point.id,point.origin,point.references,point.reference_valid))
            mesh.dimensions.push_back(std::move(*station));
    }
    document::Placement frame;
    frame.x=curve.origin.x;frame.y=curve.origin.y;frame.z=curve.origin.z;
    frame.rotation_x=curve.rotation.x;frame.rotation_y=curve.rotation.y;frame.rotation_z=curve.rotation.z;
    return document::general_surface_place_definition_mesh(frame,std::move(mesh));
}
}
