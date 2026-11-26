#pragma once
#include <zima/document/part_document.hpp>
namespace zima::document {
// Transient direct manipulation. Preserve authored reference offsets, sides,
// branches and Cartesian locks; use the ordinary exact placement equations.
[[nodiscard]] std::optional<kernel::Vec3> project_placement_origin_drag(
    const Placement& baseline, const kernel::ViewerReferenceGeometry&,
    kernel::Vec3 target, bool point_circle_policy = false);
}
