#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>

namespace zima::document {
// FRONT is local +Y. A plane normal or a point-followed-by-curve tangent
// therefore selects local XZ for an automatic Sketch work plane.
inline bool sketch_placement_uses_front_plane(
        const std::vector<ConstructionReference>& references) {
    return std::ranges::any_of(references, [](const auto& reference) {
        if (reference.owner_id.empty() && reference.semantic_key.empty()) return false;
        return (!reference.orientation_only && reference.supports_offset) ||
            (reference.orientation_drives_rotation &&
                (reference.orientation_role == "front" || reference.orientation_role == "direction"));
    });
}
inline void normalize_sketch_front_references(
        std::vector<zima::document::ConstructionReference>& references) {
    auto first_position_plane = std::find_if(references.begin(), references.end(),
        [](const auto& reference) {
            return !reference.orientation_only && reference.supports_offset &&
                (!reference.owner_id.empty() || !reference.semantic_key.empty());
        });
    if (first_position_plane == references.end()) return;
    const auto front_owner = first_position_plane->owner_id;
    const auto front_path = first_position_plane->instance_path;
    const auto front_semantic = first_position_plane->semantic_key;
    const auto same_source = [&](const auto& reference) {
        return reference.owner_id == front_owner &&
            reference.instance_path == front_path &&
            reference.semantic_key == front_semantic;
    };
    for (auto& reference : references) {
        if (reference.orientation_only) {
            // Replacing row 0 can temporarily allocate its new automatic
            // orientation in the free TOP slot. The Sketch work plane owns
            // FRONT; its retained twin must follow that same role.
            if (same_source(reference)) {
                reference.orientation_drives_rotation = true;
                reference.orientation_role = "front";
            }
            continue;
        }
        if (&reference == &*first_position_plane) {
            reference.orientation_drives_rotation = true;
            reference.orientation_role = "front";
        } else {
            reference.orientation_drives_rotation = false;
            reference.orientation_role = "none";
        }
    }
    // A Sketch's first planar row owns its container frame. Its selected
    // work plane may be automatic or manually overridden within that frame. Any
    // automatic orientation-only twin belonging to rows 1/2 (FRONT, TOP or
    // another role) would rotate that local Origin even though those rows
    // are position constraints only. Retain orientation copies solely for
    // the row-0 source.
    std::erase_if(references, [&](const auto& reference) {
        return reference.orientation_only && !same_source(reference);
    });
}
}
