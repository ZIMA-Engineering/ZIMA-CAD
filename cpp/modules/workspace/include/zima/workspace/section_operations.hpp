#pragma once
#include <zima/workspace/workspace.hpp>
namespace zima::workspace {
class SectionOperationError : public std::runtime_error {
public:
    const char* code;
    SectionOperationError(const char* code,const char* message):std::runtime_error(message),code(code){}
};
struct SectionEdit {
    std::string document_id;
    std::uint64_t revision{},generation{};
    std::shared_ptr<const int> runtime_identity;
    bool creating{};
    document::SectionDefinition initial;
};
[[nodiscard]] SectionEdit prepare_section_edit(const Workspace&,const std::string& document,const std::string& section = {});
// Validate a complete private Section draft before one native-document commit.
// Geometry comes from already calculated viewer data; no body or mate solve.
[[nodiscard]] bool commit_section(Workspace&,const SectionEdit&,document::SectionDefinition);
[[nodiscard]] kernel::ViewerReferenceGeometry section_placement_geometry(const Workspace&,const std::string& document);
// An empty ID activates the permanent unsectioned display. These operations
// preserve calculated body data and share the ordinary document Undo history.
[[nodiscard]] bool activate_section(Workspace&,const std::string& document,const std::string& section = {});
[[nodiscard]] bool remove_section(Workspace&,const std::string& document,const std::string& section);
}
