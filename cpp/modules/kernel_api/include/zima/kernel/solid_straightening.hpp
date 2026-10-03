#pragma once
#include <zima/kernel/geometry_kernel.hpp>

namespace zima::kernel {

// Prepared only during explicit body calculation. Authored sources remain
// immutable; callers evaluate this replacement at a solid-state boundary.
struct SolidStraighteningPlan {
    PrimitiveRequest primitive;
    double section_area{};
    double centroid_path_length{};
    double coefficient{1};
    Vec3 source_start_centroid;
    Vec3 source_end_centroid;
    Vec3 straight_end_centroid;
    // Rigid transport of the initial section to the formed end. Numerical
    // calculation data only; never topology identity or a stored placement.
    std::array<Vec3,4> end_transform{{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}};
    Vec3 start_tangent;
};

} // namespace zima::kernel
