#pragma once
#include <zima/workspace/drawing_sources.hpp>
namespace zima::workspace {
struct DrawingTitleEdit {
    std::string sheet, source_document, bom_row;
    std::filesystem::path drawing_path, source_path;
    drawing::TitleBlockContext context;
    std::vector<drawing::TitleBlockField> fields;
    std::set<std::string> calculated;
    std::map<std::string,std::string> initial_values;
};
DrawingTitleEdit prepare_drawing_title_edit(const drawing::DrawingDocument&,const std::string& sheet,
    const Workspace*,const std::filesystem::path& drawing_path,const std::string& bom_row={});
bool drawing_title_field_writable(const drawing::TitleBlockField&,const DrawingTitleEdit&);
std::string drawing_title_focus_field(const DrawingTitleEdit&,const std::string& expression);
// Revalidate the whole request before source or drawing writes. Model metadata
// belongs to the original source; local text belongs to the drawing sheet.
struct DrawingTitleChange {bool changed{},source_changed{};std::string source_document;};
DrawingTitleChange edit_drawing_title(drawing::DrawingDocument&,Workspace*,const DrawingTitleEdit&,
    const std::map<std::string,std::string>& changes);
}
