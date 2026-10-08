#pragma once
#include <zima/document/placement_types.hpp>
#include <algorithm>
#include <array>
#include <string_view>

namespace zima::document {
// Both the frame solver and rotation feedback use this angular boundary.
inline constexpr double placement_direction_limit_degrees = 0.01;

// A complete Origin is a frame attachment, rather than a choice of one
// supporting plane. Occurrence identity is part of that frame's identity.
inline bool placement_references_use_whole_origin(
        const std::vector<ConstructionReference>& references) {
    const ConstructionReference* first = nullptr;
    std::array<bool, 3> planes{};
    for (const auto& ref : references) {
        if (ref.orientation_only || ref.owner_id.empty()) continue;
        const std::string_view key = ref.semantic_key;
        const int index = key == "origin:plane:xy" || key == "plane:xy" ? 0
            : key == "origin:plane:xz" || key == "plane:xz" ? 1
            : key == "origin:plane:yz" || key == "plane:yz" ? 2 : -1;
        if (index < 0 || !ref.supports_offset || planes[index]) return false;
        if (first && (first->owner_id != ref.owner_id ||
                      first->instance_path != ref.instance_path)) return false;
        first = &ref;
        planes[index] = true;
    }
    return std::ranges::all_of(planes, [](bool present) { return present; });
}

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
