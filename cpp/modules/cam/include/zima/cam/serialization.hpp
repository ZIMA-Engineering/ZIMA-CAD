#pragma once
#include <zima/cam/facing.hpp>
#include <zima/cam/profile_milling.hpp>
#include <zima/cam/simulation.hpp>
#include <nlohmann/json_fwd.hpp>

namespace zima::cam {
void to_json(nlohmann::json&,const Machine&);
void from_json(const nlohmann::json&,Machine&);
void to_json(nlohmann::json&,const Tool&);
void from_json(const nlohmann::json&,Tool&);
void to_json(nlohmann::json&,const Program&);
void from_json(const nlohmann::json&,Program&);
void to_json(nlohmann::json&,const Facing&);
void from_json(const nlohmann::json&,Facing&);
void to_json(nlohmann::json&,const ProfileMilling&);
void from_json(const nlohmann::json&,ProfileMilling&);
void to_json(nlohmann::json&,const Collision&);
void from_json(const nlohmann::json&,Collision&);
void to_json(nlohmann::json&,const ClampingWarning&);
void from_json(const nlohmann::json&,ClampingWarning&);
void to_json(nlohmann::json&,const ClampingContact&);
void from_json(const nlohmann::json&,ClampingContact&);
// Definition validation is independent of machine execution and permits a
// missing tool reference to remain repairable. Compilation resolves it.
void validate_program_definition(const Program&);
}
