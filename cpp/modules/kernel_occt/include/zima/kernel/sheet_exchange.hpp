#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
struct FlatContourArc {Vec3 center,start,end;double radius{};bool clockwise{},full{};};
struct FlatContour {
    Vec3 origin, x_axis, y_axis;
    std::vector<BSplineGeometry> curves;
    std::vector<FlatContourArc> arcs;
    std::vector<std::array<Vec3,2>> bend_axes;
};
// Explicit export calculation. Curves are returned in millimetres in XY.
[[nodiscard]] FlatContour sheet_flat_contour(const BodyResult&,double thickness,double tolerance);
// Explicit conversion calculation. Intersect a bounded native planar face
// with local XY half-planes a*x+b*y+c >= 0, retaining exact contour curves.
[[nodiscard]] FlatContour clip_sheet_face_contour(const BodyResult& face,
    const ModelEnvelope& frame,const std::vector<std::array<double,3>>& half_planes,
    double tolerance);
}
