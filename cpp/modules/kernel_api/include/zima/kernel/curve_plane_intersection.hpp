#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {
enum class CurvePlaneContactKind { Crossing, Tangent, Endpoint };
struct CurvePlaneContact {
    double parameter{};
    Vec3 position;
    CurvePlaneContactKind kind{CurvePlaneContactKind::Crossing};
    // Crossing direction or tangent support side relative to the plane normal.
    // Open endpoint identity follows its bounded parameter, with orientation 0.
    int orientation{};
};
struct CurvePlaneIntersectionRequest {
    BSplineGeometry curve; // Exact source basis, in the same frame as the plane.
    Vec3 origin;
    Vec3 normal{0,0,1};
    double tolerance{0.001};
    bool closed{}; // Authored closure, not inferred from display samples.
};
struct CurvePlaneIntersectionResult {
    double first_parameter{},last_parameter{};
    std::vector<CurvePlaneContact> points;
    std::vector<std::pair<double,double>> coincident_intervals;
};
// Explicit native queries only. No viewer tessellation, source edits or cache.
[[nodiscard]] CurvePlaneIntersectionResult intersect_curve_plane(
    const CurvePlaneIntersectionRequest&);
// Reuse the exact native Sweep route adapter for authored general 3D curves.
[[nodiscard]] BSplineGeometry exact_path_segment_geometry(
    const Sweep3DRequest::PathSegment&);
}
