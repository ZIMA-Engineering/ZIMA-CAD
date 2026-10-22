#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <optional>
#include <zima/document/placement_orientation.hpp>

namespace zima::document {
// Plane entry chooses a real TOP equation for an unplaced profile. Replacing
// an authored placement retains its chosen axis; no stored frame is migrated.
inline void prepare_profile_plane_reference(std::size_t index,ConstructionReference& reference,
        const std::vector<ConstructionReference>& existing) {
    if(index!=0||!reference.supports_offset)return;
    const auto first=std::ranges::find_if(existing,[](const auto& candidate){
        return !candidate.orientation_only&&candidate.supports_offset&&
            (!candidate.owner_id.empty()||!candidate.semantic_key.empty());
    });
    if(first!=existing.end()) {
        if(first->orientation_drives_rotation&&first->orientation_role!="none") {
            reference.orientation_role=first->orientation_role;reference.orientation_drives_rotation=true;
        }
        return;
    }
    if(std::ranges::any_of(existing,[](const auto& candidate){return candidate.orientation_drives_rotation;}))return;
    reference.orientation_role="top";reference.orientation_drives_rotation=true;
}

inline std::optional<sketcher::SketchPlane> automatic_sketch_work_plane(
        const std::vector<ConstructionReference>& references) {
    if(placement_references_use_whole_origin(references))return std::nullopt;
    const auto plane=[](const ConstructionReference& ref)->std::optional<sketcher::SketchPlane> {
        if(!ref.orientation_drives_rotation)return std::nullopt;
        if(ref.orientation_role=="top"||ref.orientation_role=="bottom")return sketcher::SketchPlane::XY;
        if(ref.orientation_role=="left"||ref.orientation_role=="right")return sketcher::SketchPlane::YZ;
        return sketcher::SketchPlane::XZ;
    };
    for(const auto& ref:references) {
        if(ref.orientation_only||ref.owner_id.empty())continue;
        if(const auto result=plane(ref))return result;
        for(const auto& twin:references)if(twin.orientation_only&&twin.owner_id==ref.owner_id&&
            twin.semantic_key==ref.semantic_key&&twin.instance_path==ref.instance_path)
            if(const auto result=plane(twin))return result;
    }
    for(const auto& ref:references)if(const auto result=plane(ref))return result;
    return std::nullopt;
}
inline void apply_automatic_sketch_work_plane(sketcher::Sketch& sketch,
        const std::vector<ConstructionReference>& references) {
    if(sketch.plane_auto)if(const auto plane=automatic_sketch_work_plane(references))sketch.plane=*plane;
}

// FRONT is local +Y. A plane normal or a point-followed-by-curve tangent
// therefore selects local XZ for an automatic Sketch work plane.
inline bool sketch_placement_uses_front_plane(
        const std::vector<ConstructionReference>& references) {
    return automatic_sketch_work_plane(references)==sketcher::SketchPlane::XZ;
}

// Recover the container axes from its resolved work plane without treating
// every plane as XZ. Used only to paint the properties preview; the document
// resolver continues to own the calculated Sketch frame.
inline std::array<kernel::Vec3, 3> sketch_container_frame_axes(
        const sketcher::Sketch& sketch) {
    if (sketch.plane == sketcher::SketchPlane::XY)
        return {sketch.resolved_x_axis, sketch.resolved_y_axis, sketch.resolved_normal};
    if (sketch.plane == sketcher::SketchPlane::YZ)
        return {sketch.resolved_normal, sketch.resolved_x_axis, sketch.resolved_y_axis};
    return {sketch.resolved_x_axis, sketch.resolved_normal,
        kernel::Vec3{-sketch.resolved_y_axis.x, -sketch.resolved_y_axis.y, -sketch.resolved_y_axis.z}};
}
}
