#pragma once
#include <zima/drawing/drawing_document.hpp>
namespace zima::drawing {
DrawingBalloon make_drawing_balloon();
const BomRow* balloon_bom_row(const DrawingSheet&, const DrawingView&, const kernel::EdgeReference&);
struct BalloonEvaluation {
    std::optional<Point2> anchor;
    int item_number{};
    bool unresolved{true};
};
BalloonEvaluation evaluate_balloon(const DrawingSheet&, const DrawingBalloon&);
void refresh_balloon(const DrawingSheet&, DrawingBalloon&);
void refresh_balloons(DrawingSheet&);
// One automatic balloon per immediate BOM row represented in this view.
void show_all_balloons(DrawingSheet&,const std::string& view);
void erase_all_balloons(DrawingSheet&,const std::string& view);
void validate_balloon(const DrawingBalloon&);
std::string serialize_balloons(const std::vector<DrawingBalloon>&);
std::vector<DrawingBalloon> deserialize_balloons(const std::string&);
}
