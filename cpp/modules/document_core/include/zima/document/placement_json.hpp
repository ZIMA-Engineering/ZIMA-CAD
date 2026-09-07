#pragma once
#include <zima/document/placement_types.hpp>
#include <nlohmann/json.hpp>

namespace zima::document {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConstructionReference, instance_path, owner_id, semantic_key,
    offset, supports_offset, orientation_role, orientation_drives_rotation, orientation_only, flip)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Placement, x, y, z, rotation_x, rotation_y, rotation_z,
    absolute_rotation_x, absolute_rotation_y, absolute_rotation_z, orientation_back, orientation_quarter_turns,
    rotation_offset_x, rotation_offset_y, rotation_offset_z, references, reference_valid)
} // namespace zima::document
