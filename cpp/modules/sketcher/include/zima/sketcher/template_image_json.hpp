#pragma once
#include <zima/sketcher/template_image.hpp>
#include <nlohmann/json_fwd.hpp>
namespace zima::sketcher {
void to_json(nlohmann::json&, const TemplateImage&);
void from_json(const nlohmann::json&, TemplateImage&);
}
