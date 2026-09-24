#pragma once
#include <zima/document/object_annotation_frames.hpp>
#include <algorithm>
namespace zima::document {
inline kernel::Appearance component_appearance(const PartDocument& doc) {
    auto appearance=doc.appearance;appearance.body.color=doc.body_color;
    for(const auto& [key,color]:doc.face_colors)
        if(std::ranges::none_of(appearance.groups,[&](const auto& group){return std::ranges::find(group.faces,key)!=group.faces.end();}))
            appearance.groups.push_back({"face-"+key,"Plochy "+std::to_string(appearance.groups.size()+1),{},kernel::SurfaceStyle{color,.55,0},{key}});
    appearance.owner_bodies.clear();
    for(const auto& feature:doc.history)if(const auto* body=doc.body_history.owner(feature.id))appearance.owner_bodies[feature.id]=body->scope.id;
    for(const auto& body:doc.body_history.bodies())if(body.derived_copy)appearance.owner_bodies[body.scope.id]=body.scope.id;
    return appearance;
}
inline void append_component_mesh(zima::kernel::ViewerMesh& target,
                 const zima::kernel::ViewerMesh& source) {
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.axes.insert(target.axes.end(), source.axes.begin(), source.axes.end());
    target.edges.insert(target.edges.end(), source.edges.begin(), source.edges.end());
    target.constraint_markers.insert(target.constraint_markers.end(),
        source.constraint_markers.begin(), source.constraint_markers.end());
    auto& references = target.original_references;
    references.edges.insert(references.edges.end(), source.original_references.edges.begin(), source.original_references.edges.end());
    references.points.insert(references.points.end(), source.original_references.points.begin(), source.original_references.points.end());
    references.axes.insert(references.axes.end(), source.original_references.axes.begin(), source.original_references.axes.end());
    const auto offset = static_cast<std::uint32_t>(references.vertices.size());
    references.vertices.insert(references.vertices.end(),
        source.original_references.vertices.begin(),
        source.original_references.vertices.end());
    for (const auto index : source.original_references.triangles) {
        references.triangles.push_back(offset + index);
    }
    references.triangle_references.insert(references.triangle_references.end(),
        source.original_references.triangle_references.begin(),
        source.original_references.triangle_references.end());
}

inline zima::kernel::BodyResult component_source(const PartDocument& document, const std::vector<kernel::BodyResult>& calculated) {
    auto result = calculated.empty() ? zima::kernel::BodyResult{}
        : calculated.back();

    for (const auto& sketch : document.sketches) {
        if (sketch.suppressed) continue;
        const auto owner = std::ranges::find(document.history, sketch.owner_container_id,
            &zima::document::HistoryContainer::id);
        if (owner != document.history.end() && (owner->suppressed ||
            owner->feature_kind != zima::document::FeatureKind::Sketch)) continue;
        auto mesh = sketch.viewer_mesh();
        // A component publishes the idle Sketch profile, not Sketcher tools.
        // Keep the original-reference packet intact for mates and future edits.
        std::erase_if(mesh.edges, [](const auto& edge) {
            const auto& key = edge.reference.semantic_key;
            return edge.construction || !(key.starts_with("segment:") ||
                key.starts_with("circle:") || key.starts_with("arc:") ||
                key.starts_with("corner_radius:") || key.starts_with("ellipse:") ||
                key.starts_with("elliptical_arc:") || key.starts_with("bspline:") ||
                key.starts_with("text:"));
        });
        for (auto& edge : mesh.edges)
            edge.display_owner_id = sketch.owner_container_id.empty()
                ? sketch.id : sketch.owner_container_id;
        std::erase_if(mesh.points, [](const auto& point) {
            return point.construction ||
                (!point.reference.semantic_key.starts_with("point:") &&
                 point.reference.semantic_key != "external_point:sketch_origin");
        });
        for (auto& point : mesh.points)
            if (point.reference.semantic_key == "external_point:sketch_origin") {
                point.reference.semantic_key = "sketch:origin-marker";
                point.always_visible = false;
            }
        mesh.axes.clear();
        mesh.constraint_markers.clear();
        if (const auto* body = document.body_owner_for_object(sketch.id)) {
            if (!body->visible) continue;
            mesh = document.place_body_mesh(std::move(mesh),body->scope.id);
        }
        append_component_mesh(result.mesh,mesh);
    }
    append_component_mesh(result.mesh, document.construction_viewer_mesh());
    zima::kernel::ViewerMesh sketch_references;
    sketch_references.original_references=document.sketch_placement_reference_geometry();
    append_component_mesh(result.mesh,sketch_references);
    // Publish the persisted datum frames in the component snapshot when it
    // is explicitly inserted or regenerated. Display visibility stays local
    // to the editing View; mate resolution uses this reference packet.
    zima::kernel::ViewerMesh origins;
    origins.original_references = document.body_origin_reference_geometry();
    append_component_mesh(result.mesh, origins);
    origins.original_references = document.history_origin_reference_geometry_before({});
    append_component_mesh(result.mesh, origins);
    result.mesh.annotation_frames=zima::document::part_annotation_envelopes(document,result.mesh);
    return result;
}

}
