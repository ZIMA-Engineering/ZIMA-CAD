#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
class TemplateOperationError : public std::runtime_error {
public:
    const char* code;
    TemplateOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
};
[[nodiscard]] bool is_drawing_template(const Workspace&,const std::string& document);
[[nodiscard]] const sketcher::Sketch& drawing_template_sketch(const Workspace&,const std::string& document);
[[nodiscard]] document::PartDocument template_part_from_sketch(sketcher::Sketch,std::string name);
[[nodiscard]] std::string create_drawing_template(Workspace&,bool title_block,const std::string& name,const std::filesystem::path&);
[[nodiscard]] std::string open_drawing_template(Workspace&,const std::filesystem::path&);
void save_drawing_template(Workspace&,const std::string& document,const std::filesystem::path&,bool copy=false,bool overwrite=false);
[[nodiscard]] bool commit_template_sketch(Workspace&,const std::string& document,sketcher::Sketch);
}
