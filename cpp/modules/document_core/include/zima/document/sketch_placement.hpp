#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>

namespace zima::document {
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
        if (reference.orientation_only) continue;
        if (&reference == &*first_position_plane) {
            reference.orientation_drives_rotation = true;
            reference.orientation_role = "front";
        } else {
            reference.orientation_drives_rotation = false;
            reference.orientation_role = "none";
        }
    }
    // A Sketch's first planar row owns its complete work-plane frame. Any
    // automatic orientation-only twin belonging to rows 1/2 (FRONT, TOP or
    // another role) would rotate that local Origin even though those rows
    // are position constraints only. Retain orientation copies solely for
    // the row-0 source.
    std::erase_if(references, [&](const auto& reference) {
        return reference.orientation_only && !same_source(reference);
    });
}
}
