#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <cmath>

namespace zima::drawing {
inline bool is_cardinal_projection(ProjectionDirection direction) {
    return direction==ProjectionDirection::Right||direction==ProjectionDirection::Top||
        direction==ProjectionDirection::Left||direction==ProjectionDirection::Bottom;
}
// Drawing paper X grows to the left; projected placement follows that frame.
inline ProjectionDirection cardinal_projection_direction(Point2 displacement) {
    if(std::abs(displacement.x)>=std::abs(displacement.y))
        return displacement.x<=0?ProjectionDirection::Right:ProjectionDirection::Left;
    return displacement.y>=0?ProjectionDirection::Top:ProjectionDirection::Bottom;
}
inline bool same_view_orientation(const ProjectionCamera& a,const ProjectionCamera& b) {
    const auto same=[](kernel::Vec3 x,kernel::Vec3 y) {
        return std::abs(x.x-y.x)<1e-9&&std::abs(x.y-y.y)<1e-9&&std::abs(x.z-y.z)<1e-9;
    };
    return same(a.horizontal,b.horizontal)&&same(a.vertical,b.vertical)&&same(a.depth,b.depth);
}
}
