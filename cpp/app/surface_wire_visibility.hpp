#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <unordered_map>

namespace zima::app {

inline const std::string& surface_boundary_wire_owner(const document::GeneralSurfaceBoundary& boundary) {
    return boundary.curve?boundary.curve->id:boundary.sketch_feature->feature.sketch_id;
}

// Resolve persisted wire packet owners to their authored object identity.
// Surface-result rim edges deliberately do not count as construction wires.
inline std::optional<std::string> surface_wire_owner(
        const document::PartDocument& part,const std::string& id) {
    for(const auto& sketch:part.sketches)if(sketch.id==id||sketch.owner_container_id==id)return sketch.id;
    for(const auto& curve:part.constructions)if(curve.kind==document::ConstructionKind::Curve3D&&
        (curve.id==id||curve.entity_id==id))return curve.id;
    for(const auto& feature:part.history) {
        if(feature.feature_kind==document::FeatureKind::SurfaceIntersection&&feature.id==id)return id;
        if(feature.feature_kind!=document::FeatureKind::GeneralSurface)continue;
        for(const auto& boundary:feature.general_surface.boundaries) {
            if(boundary.curve) {
                if(boundary.curve->id==id||boundary.curve->entity_id==id)return boundary.curve->id;
            } else {
                if(boundary.sketch_feature&&(boundary.sketch_feature->feature.sketch_id==id||boundary.sketch_feature->id==id))
                    return boundary.sketch_feature->feature.sketch_id;
            }
        }
    }
    return {};
}

// A solid feature's source Sketch is a wire owner, but its outer Tree row
// represents the calculated result. Only actual wire containers offer Hide.
inline std::optional<std::string> surface_wire_container_owner(
        const document::PartDocument& part,const std::string& id) {
    const auto* feature=part.find_container(id);
    if(feature&&(feature->feature_kind==document::FeatureKind::Sketch||
        feature->feature_kind==document::FeatureKind::SurfaceIntersection||
        (feature->feature_kind==document::FeatureKind::Feature&&feature->feature.type==document::FeatureType::Sketch)))
        return surface_wire_owner(part,id);
    return {};
}

inline std::set<std::string> hidden_surface_wires(const document::PartDocument& part,
        std::size_t history_limit=std::numeric_limits<std::size_t>::max()) {
    std::set<std::string> active,hidden;
    for(std::size_t i=0;i<std::min(part.effective_history_cursor(),part.history_order.size());++i)
        active.insert(part.history_order[i].id);
    for(std::size_t i=0;i<std::min(history_limit,part.history.size());++i) {
        const auto& feature=part.history[i];
        if(feature.suppressed||(!part.history_order.empty()&&!active.contains(feature.id)))continue;
        if(feature.feature_kind==document::FeatureKind::GeneralSurface)
            for(const auto& boundary:feature.general_surface.boundaries)
                hidden.insert(surface_boundary_wire_owner(boundary));
        if(feature.feature_kind==document::FeatureKind::SurfaceTrim)
            for(const auto& tool:feature.surface_trim.tools)if(!tool.face)
                if(const auto owner=surface_wire_owner(part,tool.reference.owner_id))hidden.insert(*owner);
    }
    for(const auto& [owner,visible]:part.surface_wire_visibility)
        if(visible)hidden.erase(owner);else hidden.insert(owner);
    return hidden;
}

inline std::set<std::string> surface_wire_packet_owners(const document::PartDocument& part,
        const std::set<std::string>& wires) {
    auto owners=wires;
    const auto curve=[&](const document::ConstructionObject& value) {
        if(!wires.contains(value.id))return;
        owners.insert(value.entity_id);owners.insert(value.container_origin.id);owners.insert(value.id+":origin");
        for(const auto& point:value.curve_points) {
            owners.insert(point.id);owners.insert(point.entity_id);
            owners.insert(point.container_origin.id);owners.insert(point.id+":origin");
        }
    };
    for(const auto& value:part.constructions)curve(value);
    for(const auto& feature:part.history)if(feature.feature_kind==document::FeatureKind::GeneralSurface)
        for(const auto& boundary:feature.general_surface.boundaries)
            if(boundary.curve)curve(*boundary.curve);
            else if(boundary.sketch_feature&&wires.contains(boundary.sketch_feature->feature.sketch_id)) {
                owners.insert(boundary.sketch_feature->id);owners.insert(boundary.sketch_feature->container_origin.id);
            }
    return owners;
}

inline void filter_surface_wires(kernel::ViewerMesh& mesh,const std::set<std::string>& owners) {
    const auto hidden=[&](const auto& value){return owners.contains(value.reference.owner_id);};
    std::erase_if(mesh.edges,hidden);std::erase_if(mesh.points,hidden);
    std::erase_if(mesh.axes,hidden);std::erase_if(mesh.dimensions,hidden);
    // Original reference packets remain available for explicit reference entry.
}

inline void filter_hidden_body_geometry(kernel::ViewerMesh& mesh,const document::PartDocument& part) {
    if(std::ranges::none_of(part.body_history.bodies(),[](const auto& body){return !body.visible;}))return;
    std::unordered_map<std::string,bool> hidden_owners;
    const auto hidden=[&](const auto& item) {
        const auto& owner=item.reference.owner_id;
        if(const auto found=hidden_owners.find(owner);found!=hidden_owners.end())return found->second;
        const auto* body=part.body_history.find(owner);
        if(!body)body=part.body_owner_for_object(owner);
        const bool value=body&&!body->visible;hidden_owners.emplace(owner,value);return value;
    };
    std::erase_if(mesh.edges,hidden);std::erase_if(mesh.points,hidden);
    std::erase_if(mesh.axes,hidden);std::erase_if(mesh.dimensions,hidden);std::erase_if(mesh.constraint_markers,hidden);
    // Keep reference data available to explicit editing/inspection commands.
}
} // namespace zima::app
