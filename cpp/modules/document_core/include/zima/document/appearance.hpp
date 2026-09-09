#pragma once
#include <zima/kernel/appearance.hpp>
namespace zima::document {
std::string serialize_appearance(const kernel::Appearance &value);
kernel::Appearance deserialize_appearance(const std::string &value);
void validate_appearance(const kernel::Appearance &value);
std::vector<kernel::NamedStyle> default_surface_palette();
std::string serialize_palette(const std::vector<kernel::NamedStyle> &value);
std::vector<kernel::NamedStyle> deserialize_palette(const std::string &value);
} // namespace zima::document
