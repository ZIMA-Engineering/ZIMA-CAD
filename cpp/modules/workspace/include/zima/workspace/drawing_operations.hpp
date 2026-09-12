#pragma once
#include <zima/workspace/workspace.hpp>
#include <functional>
namespace zima::workspace {
class DrawingOperationError : public std::runtime_error {
public:
    std::string code;
    DrawingOperationError(std::string code,const char* message):std::runtime_error(message),code(std::move(code)){}
};
struct SheetSettings {
    std::string name{"List"};
    drawing::SheetFormat format{drawing::SheetFormat::A4};
    drawing::ProjectionMethod projection{drawing::ProjectionMethod::FirstAngle};
    double scale{1},thick_line_mm{.5},thin_line_mm{.25},red_line_mm{.7};
    std::string locale{"cs"};
    bool operator==(const SheetSettings&)const=default;
};
using DrawingSourceReader=std::function<kernel::ViewerMesh(const drawing::DrawingView&)>;
SheetSettings sheet_settings(const drawing::DrawingSheet&);
void validate_sheet_settings(const SheetSettings&);
std::string create_drawing_sheet(drawing::DrawingDocument&,const SheetSettings&);
void delete_drawing_sheet(drawing::DrawingDocument&,const std::string&);
bool set_drawing_sheet(drawing::DrawingDocument&,const std::string&,const SheetSettings&,const DrawingSourceReader& = {});
bool clear_drawing_template(drawing::DrawingDocument&,const std::string&,bool title_block);
void load_drawing_template(drawing::DrawingDocument&,const std::string&,const std::filesystem::path&,bool title_block);
// Explicit projection/edit only. Uses stored mesh, never calculates OCCT or opens tabs.
std::pair<std::string,kernel::ViewerMesh> read_drawing_source(const Workspace*,const std::filesystem::path&,const std::string& expected_id={});
}
