#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <array>
#include <cstddef>
#include <vector>

// Surface-only research. Not a persisted feature or manufacturing flat pattern.
namespace zima::research::transition {
using kernel::Vec3;
struct QuarterArc {
    Vec3 center, first_axis, second_axis;
    [[nodiscard]] Vec3 point(double parameter) const;
    [[nodiscard]] Vec3 tangent(double parameter) const;
};
struct Options {
    std::size_t facets{4}; // Four faces, three internal folds, endpoint tangency.
    double linear_tolerance{1e-7};
    double deviation_resolution{0.1}; // Maximum sampling step in mm, not model tolerance.
};
enum class Failure {
    None, InvalidInput, IncompatibleEndpoints, AmbiguousCorrespondence,
    NonMonotoneCorrespondence, SingularSurface, SingularPlanes,
    InvalidFacet, FoldedIntersection, UnfoldedIntersection, MetricMismatch,
    DeviationResolution
};
struct Facet {
    std::array<Vec3,4> folded, unfolded; // A_i, B_i, B_next, A_next.
    Vec3 normal;
};
struct Fold {
    Vec3 first, second;
    double signed_angle_radians{};
};
struct Deviation {
    double lower{}, upper{}; // Two-sided Hausdorff distance bracket, mm.
};
struct Result {
    Failure failure{Failure::None};
    std::vector<Facet> facets;
    std::vector<Fold> folds;
    std::array<Deviation,2> boundary_deviation{};
    double maximum_planarity_error{}, maximum_metric_error{};
    double folded_area{}, unfolded_area{};
    double maximum_developability_residual{};
    [[nodiscard]] bool valid() const {return failure==Failure::None;}
};
[[nodiscard]] Result calculate(const QuarterArc&,const QuarterArc&,const Options& = {});
}
