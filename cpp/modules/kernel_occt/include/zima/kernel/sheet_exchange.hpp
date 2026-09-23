#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
struct FlatContourArc {Vec3 center,start,end;double radius{};bool clockwise{},full{};};
struct FlatContour {
    Vec3 origin, x_axis, y_axis;
    std::vector<BSplineGeometry> curves;
    std::vector<FlatContourArc> arcs;
};
// Explicit export calculation. Curves are returned in millimetres in XY.
[[nodiscard]] FlatContour sheet_flat_contour(const BodyResult&,double thickness,double tolerance);
}
