#pragma once
#include "transition_half.hpp"
#include <filesystem>
namespace zima::research::transition {
enum class PatternRole {Outline,BendAxis};
struct PatternLine {Vec3 first,second;PatternRole role;double signed_angle_radians{};};
using Pattern=std::vector<PatternLine>;
[[nodiscard]] Pattern pattern(const Result&);
[[nodiscard]] Pattern pattern(const HalfResult&);
// Derived surface-study output only: no material allowance or manufacturing approval.
void export_pattern(const Pattern&,const std::filesystem::path&);
}
