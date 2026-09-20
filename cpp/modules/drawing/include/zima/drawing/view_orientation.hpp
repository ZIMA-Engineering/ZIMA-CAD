#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <cmath>

namespace zima::drawing {
inline bool same_view_orientation(const ProjectionCamera& a,const ProjectionCamera& b) {
    const auto same=[](kernel::Vec3 x,kernel::Vec3 y) {
        return std::abs(x.x-y.x)<1e-9&&std::abs(x.y-y.y)<1e-9&&std::abs(x.z-y.z)<1e-9;
    };
    return same(a.horizontal,b.horizontal)&&same(a.vertical,b.vertical)&&same(a.depth,b.depth);
}
}
