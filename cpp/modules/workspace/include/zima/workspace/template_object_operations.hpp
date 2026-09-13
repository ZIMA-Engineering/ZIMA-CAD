#pragma once
#include <zima/workspace/template_operations.hpp>
namespace zima::workspace {
[[nodiscard]] const sketcher::TemplateImage& template_image(const Workspace&,const std::string& document,const std::string& image);
[[nodiscard]] const sketcher::SketchRepeatRegion& template_region(const Workspace&,const std::string& document,const std::string& region);
// Empty previous ID creates an object; editing must preserve an existing identity.
[[nodiscard]] bool commit_template_image(Workspace&,const std::string& document,const std::string& previous,sketcher::TemplateImage);
[[nodiscard]] bool commit_template_region(Workspace&,const std::string& document,const std::string& previous,sketcher::SketchRepeatRegion);
[[nodiscard]] bool remove_template_image(Workspace&,const std::string& document,const std::string& image);
[[nodiscard]] bool remove_template_region(Workspace&,const std::string& document,const std::string& region);
}
