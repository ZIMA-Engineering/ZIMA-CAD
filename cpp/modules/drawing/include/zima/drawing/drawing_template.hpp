#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <functional>

namespace zima::drawing {
using PrepareTemplateText = std::function<void(zima::sketcher::SketchText&)>;
[[nodiscard]] zima::sketcher::Sketch load_template_sketch(
    const std::filesystem::path&, const PrepareTemplateText&);
[[nodiscard]] zima::sketcher::Sketch create_template_sketch(bool title_block, std::string name);
void save_template_sketch(const zima::sketcher::Sketch&, const std::filesystem::path&);
void validate_repeat_region(const zima::sketcher::SketchRepeatRegion&);
} // namespace zima::drawing
