#pragma once
#include <zima/kernel/profile_centerlines.hpp>

namespace zima::kernel {
// A Feature side is measured from the Sketch normal, independently of the
// order in which its construction axis was drawn. Standalone Revolution keeps
// its separately authored Forward/Reverse convention.
inline bool feature_rotation_reversed(zima::kernel::Vec3 axis,
                                      zima::kernel::Vec3 radial,
                                      zima::kernel::Vec3 normal) {
    return zima::kernel::dimension_dot(zima::kernel::dimension_cross(axis,radial),normal)<0;
}
inline bool feature_rotation_reversed(zima::kernel::RevolutionRequest& request) {
    using namespace zima::kernel;
    Vec3 point;
    if(request.open_profile_end_id.empty()) {
        const auto saved=request.centerlines;
        request.centerlines.normal=request.profile_normal;
        point=profile_centerlines::centroid(request);
        request.centerlines=saved;
    } else {
        // Open thin/surface profiles have no area centroid. Their real native
        // endpoints supply the side; do not include construction-axis points.
        double distance=-1;
        const auto offer=[&](Vec3 candidate) {
            const double value=std::abs(dimension_dot(dimension_cross(request.axis_direction,
                dimension_sub(candidate,request.axis_point)),request.profile_normal));
            if(value>distance){distance=value;point=candidate;}
        };
        std::visit([&](const auto& profile) {
            if constexpr(requires{profile.vertices;})for(const auto vertex:profile.vertices)offer(vertex);
            else if constexpr(requires{profile.curves;})for(const auto& curve:profile.curves)
                std::visit([&](const auto& value){offer(value.start);offer(value.end);
                    if constexpr(requires{value.middle;})offer(value.middle);},curve);
            else offer(profile.center);
        },request.outer_profile);
    }
    return feature_rotation_reversed(request.axis_direction,dimension_sub(point,request.axis_point),request.profile_normal);
}
} // namespace zima::kernel
