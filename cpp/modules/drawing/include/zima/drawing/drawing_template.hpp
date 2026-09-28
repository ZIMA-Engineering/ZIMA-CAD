#pragma once
#include <zima/sketcher/sketch.hpp>
#include <zima/drawing/drawing_document.hpp>
#include <functional>

namespace zima::drawing {
using TemplateData=std::map<std::string,std::map<std::string,std::string>>;
[[nodiscard]] bool is_native_template_file(const std::filesystem::path&);
[[nodiscard]] TemplateData template_sketch_data(const zima::sketcher::Sketch&);
void load_template_data(DrawingSheet&,const TemplateData&,bool title_block);
void load_template_details(DrawingSheet&,const TemplateData&,bool title_block);
void append_title_block_sketch(DrawingSheet&,const zima::sketcher::Sketch&);
using PrepareTemplateText = std::function<void(zima::sketcher::SketchText&)>;
[[nodiscard]] zima::sketcher::Sketch load_template_sketch(
    const std::filesystem::path&, const PrepareTemplateText&);
[[nodiscard]] zima::sketcher::Sketch create_template_sketch(bool title_block, std::string name);
void save_template_sketch(const zima::sketcher::Sketch&, const std::filesystem::path&);
void validate_repeat_region(const zima::sketcher::SketchRepeatRegion&);
} // namespace zima::drawing
