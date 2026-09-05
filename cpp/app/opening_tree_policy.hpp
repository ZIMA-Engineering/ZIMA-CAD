#pragma once

#include <zima/document/part_document.hpp>
#include <algorithm>
#include <set>
#include <string>

namespace zima::app {
// Group only existing calculated edges by the persisted parents of the
// opening's owned profiles. No kernel calculation or geometric picking.
inline std::set<std::size_t> opening_component_edges(
        const document::HistoryContainer& opening, const std::string& component,
        const kernel::ViewerMesh& mesh, const std::string& instance_path = {}) {
    std::set<std::string> face_keys;
    if (component == "bore") face_keys.insert("generated:"+opening.hole.circle_id);
    if (component == "chamfer" || component == "tip") {
        const auto sketch=sketcher::Sketch::from_serialized(component == "chamfer"
            ? opening.hole.chamfer_sketch_serialized : opening.hole.tip_sketch_serialized);
        for (const auto& segment : sketch.segments)
            face_keys.insert("generated:"+segment.id);
    }
    std::set<std::size_t> result;
    for (std::size_t i=0;i<mesh.edges.size();++i) {
        const auto& edge=mesh.edges[i];
        if (edge.reference.instance_path != instance_path) continue;
        const bool thread=component == "thread" && edge.reference.owner_id==opening.id &&
            edge.reference.semantic_key.starts_with("thread:boundary:");
        const bool profile=std::ranges::any_of(edge.edge_treatment_side_references,
            [&](const auto& face) {return face.owner_id==opening.id &&
                face.instance_path==instance_path && face_keys.contains(face.semantic_key);});
        if (thread || profile) result.insert(i);
    }
    return result;
}

inline bool disable_opening_component(document::HistoryContainer& opening,
        const std::string& component) {
    if (opening.feature_kind != document::FeatureKind::Thread) return false;
    if (component == "thread" && opening.thread.enabled) {
        opening.thread.enabled=false;
        opening.thread.nominal_diameter=opening.thread.profile_diameter;
    } else if (component == "chamfer" && opening.thread.chamfer_enabled) {
        opening.thread.chamfer_enabled=false;
    } else if (component == "tip" && opening.hole.drill_point_enabled) {
        opening.hole.drill_point_enabled=false;
    } else return false;
    return true;
}
}
