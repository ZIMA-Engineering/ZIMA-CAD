#include <zima/workspace/drawing_balloon_operations.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <algorithm>
namespace zima::workspace {
bool edit_drawing_balloon(drawing::DrawingDocument& doc,const std::string& sheet_id,drawing::DrawingBalloon value,bool creating) {
    auto* sheet=doc.find_sheet(sheet_id);
    if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    const auto existing=std::ranges::find(sheet->balloons,value.id,&drawing::DrawingBalloon::id);
    if(creating) {
        for(const auto& s:doc.sheets)for(const auto& b:s.balloons)if(b.id==value.id)
            throw DrawingOperationError("invalid_arguments","The balloon already exists.");
    } else if(existing==sheet->balloons.end())throw DrawingOperationError("balloon_not_found","The balloon does not exist.");
    if(std::ranges::none_of(sheet->views,[&](const auto& v){return v.id==value.view_id;}))
        throw DrawingOperationError("view_not_found","The balloon view must belong to its drawing sheet.");
    drawing::validate_balloon(value);drawing::refresh_balloon(*sheet,value);
    if(value.unresolved&&(creating||existing->view_id!=value.view_id||existing->attachment!=value.attachment))
        throw DrawingOperationError("unresolved_reference","Select geometry belonging to an item in this sheet's BOM.");
    if(creating)sheet->balloons.push_back(std::move(value));
    else {if(*existing==value)return false;*existing=std::move(value);}
    return true;
}
bool erase_drawing_balloon(drawing::DrawingSheet& sheet,const std::string& id) {
    return std::erase_if(sheet.balloons,[&](const auto& b){return b.id==id;})>0;
}
bool set_drawing_balloons(drawing::DrawingDocument& doc,const std::string& sheet_id,const std::vector<drawing::DrawingBalloon>& values) {
    auto next=doc;auto* sheet=next.find_sheet(sheet_id);
    if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    std::set<std::string> ids;
    for(const auto& b:values) {
        if(!ids.insert(b.id).second)throw DrawingOperationError("invalid_arguments","The balloon already exists.");
        const bool creating=std::ranges::none_of(sheet->balloons,[&](const auto& item){return item.id==b.id;});
        edit_drawing_balloon(next,sheet_id,b,creating);
    }
    std::erase_if(sheet->balloons,[&](const auto& b){return !ids.contains(b.id);});
    if(sheet->balloons==doc.find_sheet(sheet_id)->balloons)return false;
    doc=std::move(next);return true;
}
}
