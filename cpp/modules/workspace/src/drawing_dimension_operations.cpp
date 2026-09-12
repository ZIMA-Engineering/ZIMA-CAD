#include <zima/workspace/drawing_dimension_operations.hpp>
#include <algorithm>
namespace zima::workspace {
bool erase_drawing_dimension(drawing::DrawingSheet& sheet,const std::string& id) {
    return std::erase_if(sheet.dimensions,[&](const auto& value){return value.id==id;})>0;
}
}
