#pragma once
#include <zima/document/placement_types.hpp>
#include <nlohmann/json.hpp>

namespace zima::document {
inline void to_json(nlohmann::json& j,const ConstructionReference& v) {
    j={{"instance_path",v.instance_path},{"owner_id",v.owner_id},{"semantic_key",v.semantic_key},{"offset",v.offset},{"supports_offset",v.supports_offset},{"orientation_role",v.orientation_role},{"orientation_drives_rotation",v.orientation_drives_rotation},{"orientation_only",v.orientation_only},{"flip",v.flip},{"offset_locked",v.offset_locked}};
}
inline void from_json(const nlohmann::json& j,ConstructionReference& v) {
    j.at("instance_path").get_to(v.instance_path);j.at("owner_id").get_to(v.owner_id);j.at("semantic_key").get_to(v.semantic_key);j.at("offset").get_to(v.offset);j.at("supports_offset").get_to(v.supports_offset);j.at("orientation_role").get_to(v.orientation_role);j.at("orientation_drives_rotation").get_to(v.orientation_drives_rotation);j.at("orientation_only").get_to(v.orientation_only);j.at("flip").get_to(v.flip);
    v.offset_locked=j.value("offset_locked",false);v.measured_offset.reset();
}
inline void to_json(nlohmann::json& j,const Placement& v) {
    j={{"x",v.x},{"y",v.y},{"z",v.z},{"rotation_x",v.rotation_x},{"rotation_y",v.rotation_y},{"rotation_z",v.rotation_z},{"absolute_rotation_x",v.absolute_rotation_x},{"absolute_rotation_y",v.absolute_rotation_y},{"absolute_rotation_z",v.absolute_rotation_z},{"orientation_back",v.orientation_back},{"orientation_quarter_turns",v.orientation_quarter_turns},{"rotation_offset_x",v.rotation_offset_x},{"rotation_offset_y",v.rotation_offset_y},{"rotation_offset_z",v.rotation_offset_z},{"references",v.references},{"reference_valid",v.reference_valid},{"value_locks",v.value_locks}};
}
inline void from_json(const nlohmann::json& j,Placement& v) {
    j.at("x").get_to(v.x);j.at("y").get_to(v.y);j.at("z").get_to(v.z);j.at("rotation_x").get_to(v.rotation_x);j.at("rotation_y").get_to(v.rotation_y);j.at("rotation_z").get_to(v.rotation_z);j.at("absolute_rotation_x").get_to(v.absolute_rotation_x);j.at("absolute_rotation_y").get_to(v.absolute_rotation_y);j.at("absolute_rotation_z").get_to(v.absolute_rotation_z);j.at("orientation_back").get_to(v.orientation_back);j.at("orientation_quarter_turns").get_to(v.orientation_quarter_turns);j.at("rotation_offset_x").get_to(v.rotation_offset_x);j.at("rotation_offset_y").get_to(v.rotation_offset_y);j.at("rotation_offset_z").get_to(v.rotation_offset_z);j.at("references").get_to(v.references);j.at("reference_valid").get_to(v.reference_valid);
    v.value_locks=j.value("value_locks",std::set<std::string>{});
}
} // namespace zima::document
