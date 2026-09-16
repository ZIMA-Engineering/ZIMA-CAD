#pragma once
#include <zima/document/placement_types.hpp>
#include <algorithm>

namespace zima::document {
// Both the frame solver and rotation feedback use this angular boundary.
inline constexpr double placement_direction_limit_degrees = 0.01;

inline void assign_container_orientation_role(ConstructionReference& reference,
        const std::vector<ConstructionReference>& existing) {
    const bool has_front = std::ranges::any_of(existing, [](const auto& ref) {
        return ref.orientation_drives_rotation &&
            (ref.orientation_role == "front" || ref.orientation_role == "back" ||
             ref.orientation_role == "direction" || ref.orientation_role == "none");
    });
    reference.orientation_role = has_front ? "top" : "front";
    reference.orientation_drives_rotation = true;
    // Keep later candidates eligible: a parallel second direction does not
    // consume TOP. Geometry, not the number of populated rows, decides rank.
}

inline void normalize_container_front_references(
        std::vector<ConstructionReference>& references) {
    bool front = false;
    for (auto& ref : references) {
        if (ref.owner_id.empty() && ref.semantic_key.empty()) continue;
        if (ref.orientation_only) {
            if (ref.orientation_drives_rotation && ref.orientation_role == "direction")
                front = true;
            continue;
        }
        if (!ref.supports_offset && !ref.orientation_drives_rotation) continue;
        ref.orientation_drives_rotation = true;
        ref.orientation_role = front ? "top" : "front";
        front = true;
    }
    // Never discard an orientation-only reference: it may complete the frame
    // after an anchored point and tangent.
}
}
