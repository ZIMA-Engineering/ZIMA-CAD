#pragma once
#include <QPointF>
#include <QPolygonF>
#include <cmath>
namespace zima::viewer {
// Existing Sketcher arrow: 10-unit length and 1.763269807-unit half-width.
inline constexpr double annotation_arrow_half_width(double length){return length*.1763269807;}
inline QPolygonF annotation_arrow(QPointF tip,QPointF direction,double length){
    const double size=std::hypot(direction.x(),direction.y());
    if(!std::isfinite(size)||size<=1e-9||!std::isfinite(length)||length<=0)return {};
    direction/=size;const QPointF normal{-direction.y(),direction.x()};const auto base=tip-direction*length;
    const auto width=annotation_arrow_half_width(length);return {tip,base+normal*width,base-normal*width};
}
} // namespace zima::viewer
