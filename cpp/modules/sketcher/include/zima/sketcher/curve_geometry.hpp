#pragma once
#include <zima/sketcher/sketch.hpp>

namespace zima::sketcher {
// Clamp the full active knot interval without approximating or changing the curve.
// Input may have unclamped/periodic control data; output satisfies BSplineGeometry.
[[nodiscard]] kernel::BSplineGeometry clamp_curve_geometry(
    kernel::BSplineGeometry curve);
// Native supporting geometry; independent of display tessellation and OCCT.
[[nodiscard]] kernel::BSplineGeometry sketch_curve_geometry(
    const Sketch& sketch, const std::string& geometry_id);
[[nodiscard]] kernel::BSplineGeometry trim_curve_geometry(
    const kernel::BSplineGeometry& curve, double start, double end);
[[nodiscard]] kernel::BSplineGeometry offset_curve_geometry(
    const kernel::BSplineGeometry& curve, double signed_distance,
    double tolerance = 1.0e-5);
[[nodiscard]] kernel::Vec3 curve_offset_point(
    const kernel::BSplineGeometry& curve, double parameter, double signed_distance);
}
