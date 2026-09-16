#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <zima/document/placement_orientation.hpp>

namespace zima::document {
// FRONT is local +Y. A plane normal or a point-followed-by-curve tangent
// therefore selects local XZ for an automatic Sketch work plane.
inline bool sketch_placement_uses_front_plane(
        const std::vector<ConstructionReference>& references) {
    return std::ranges::any_of(references, [](const auto& reference) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) return false;
        return (reference.orientation_drives_rotation &&
                (reference.orientation_role == "front" || reference.orientation_role == "direction"));
    });
}
}
