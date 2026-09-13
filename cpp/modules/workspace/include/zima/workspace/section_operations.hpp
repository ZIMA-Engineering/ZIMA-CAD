#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
class SectionOperationError : public std::runtime_error {
public:
    const char* code;
    SectionOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
};
// An empty ID activates the permanent unsectioned display. These operations
// preserve calculated body data and share the ordinary document Undo history.
[[nodiscard]] bool activate_section(Workspace&,const std::string& document,const std::string& section = {});
[[nodiscard]] bool remove_section(Workspace&,const std::string& document,const std::string& section);
}
