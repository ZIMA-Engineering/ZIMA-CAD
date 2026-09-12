#include <zima/workspace/drawing_dimension_operations.hpp>
#include <algorithm>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
namespace zima::workspace {
bool edit_drawing_dimension(drawing::DrawingDocument& doc,const std::string& sheet_id,drawing::DrawingDimension value,bool creating) {
    auto* sheet=doc.find_sheet(sheet_id);
    if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    auto existing=std::ranges::find(sheet->dimensions,value.id,&drawing::DrawingDimension::id);
    if(creating) {
        for(const auto& s:doc.sheets)for(const auto& d:s.dimensions)if(d.id==value.id)
            throw DrawingOperationError("invalid_arguments","The measured drawing dimension already exists.");
    } else if(existing==sheet->dimensions.end())
        throw DrawingOperationError("dimension_not_found","The measured drawing dimension does not exist.");
    const auto view=std::ranges::find(sheet->views,value.view_id,&drawing::DrawingView::id);
    if(view==sheet->views.end())throw DrawingOperationError("view_not_found","The dimension view must belong to its drawing sheet.");
    drawing::validate_drawing_dimension(value);
    const auto evaluation=drawing::evaluate_drawing_dimension(*view,value);
    if(evaluation.state==drawing::MeasurementState::Unresolved)
        throw DrawingOperationError("unresolved_reference",evaluation.message.c_str());
    drawing::refresh_drawing_dimension(*view,value);
    if(creating)sheet->dimensions.push_back(std::move(value));
    else {if(*existing==value)return false;*existing=std::move(value);}
    return true;
}
bool erase_drawing_dimension(drawing::DrawingSheet& sheet,const std::string& id) {
    return std::erase_if(sheet.dimensions,[&](const auto& value){return value.id==id;})>0;
}
}
