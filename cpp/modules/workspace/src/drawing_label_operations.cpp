#include <zima/workspace/drawing_label_operations.hpp>
#include <algorithm>
#include <cmath>
namespace zima::workspace {
bool set_drawing_label_position(drawing::DrawingView& view,DrawingLabel label,std::optional<drawing::Point2> value) {
    if((label!=DrawingLabel::Caption&&label!=DrawingLabel::Section)||(value&&(!std::isfinite(value->x)||!std::isfinite(value->y))))
        throw DrawingOperationError("invalid_arguments","Invalid drawing label position.");
    auto& target=label==DrawingLabel::Caption?view.caption_position:view.section_label_position;
    if(target==value)return false;target=value;return true;
}
const document::SectionDefinition& drawing_section_marker(const drawing::DrawingView& view,const std::string& id) {
    const document::SectionDefinition* found=nullptr;
    for(const auto& marker:view.section_markers)if(marker.id==id) {
        if(found)throw DrawingOperationError("ambiguous_reference","The drawing Section marker is ambiguous.");found=&marker;
    }
    if(!found)throw DrawingOperationError("section_not_found","The drawing Section marker does not exist.");return *found;
}
bool set_drawing_section_end(drawing::DrawingView& view,const std::string& id,std::size_t end,double offset,double minimum) {
    drawing_section_marker(view,id);
    if(end>1||!std::isfinite(offset)||!std::isfinite(minimum))
        throw DrawingOperationError("invalid_arguments","Invalid drawing Section end offset.");
    const auto value=std::max(minimum,offset);const auto found=view.section_marker_offsets.find(id);
    if((found==view.section_marker_offsets.end()?0:found->second[end])==value)return false;
    view.section_marker_offsets[id][end]=value;return true;
}
bool reset_drawing_section_ends(drawing::DrawingView& view,const std::string& id) {
    drawing_section_marker(view,id);return view.section_marker_offsets.erase(id)!=0;
}
}
