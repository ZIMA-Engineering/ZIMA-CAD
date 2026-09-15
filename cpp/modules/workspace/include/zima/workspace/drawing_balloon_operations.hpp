#pragma once
#include <zima/drawing/balloon.hpp>
namespace zima::workspace {
bool edit_drawing_balloon(drawing::DrawingDocument&,const std::string& sheet,drawing::DrawingBalloon,bool creating);
bool erase_drawing_balloon(drawing::DrawingSheet&,const std::string& id);
bool set_drawing_balloons(drawing::DrawingDocument&,const std::string& sheet,const std::vector<drawing::DrawingBalloon>&);
}
