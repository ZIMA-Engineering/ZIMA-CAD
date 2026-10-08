#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <zima/document/placement_orientation.hpp>

namespace zima::document {
// FRONT is local +Y. A plane normal or a point-followed-by-curve tangent
// therefore selects local XZ for an automatic Sketch work plane.
inline bool sketch_placement_uses_front_plane(
        const std::vector<ConstructionReference>& references) {
    if (placement_references_use_whole_origin(references)) return false;
    return std::ranges::any_of(references, [](const auto& reference) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) return false;
        return (reference.orientation_drives_rotation &&
                (reference.orientation_role == "front" || reference.orientation_role == "direction"));
    });
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
