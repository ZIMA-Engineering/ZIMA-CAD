#pragma once
#include <zima/sketcher/template_image.hpp>
#include <filesystem>
namespace zima::drawing_render {
// Normalizes raster data to embedded PNG, or preserves validated SVG data.
[[nodiscard]] sketcher::TemplateImage read_template_image(const std::filesystem::path&);
}
