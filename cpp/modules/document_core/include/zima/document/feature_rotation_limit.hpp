#pragma once
#include <zima/document/profile_parameters.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace zima::document {
// A uniform rotation can finish on a plane only when that plane contains the
// rotation axis. Preserve the oriented target normal: opposite plane sides
// describe different angular ends. No kernel query is needed for this datum.
inline double feature_rotation_limit_angle(kernel::Vec3 axis_point,
        kernel::Vec3 axis, kernel::Vec3 profile_normal,
        const ExtrusionParameters::EndTarget& target) {
    const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto unit=[&](kernel::Vec3 v) {
        const double size=std::sqrt(dot(v,v));
        if(!std::isfinite(size)||size<1e-12)throw std::invalid_argument("Invalid Feature definition.");
        return kernel::Vec3{v.x/size,v.y/size,v.z/size};
    };
    axis=unit(axis);profile_normal=unit(profile_normal);
    const auto normal=unit(target.fallback_normal);
    const kernel::Vec3 delta{target.fallback_origin.x-axis_point.x,
        target.fallback_origin.y-axis_point.y,target.fallback_origin.z-axis_point.z};
    if(target.kind!=EndTargetKind::Plane || std::abs(dot(normal,axis))>1e-8 ||
       std::abs(dot(profile_normal,axis))>1e-8 || !std::isfinite(dot(delta,normal)) ||
       std::abs(dot(delta,normal))>1e-6)
        throw std::invalid_argument("Rotation end plane must contain the rotation axis.");
    const kernel::Vec3 cross{profile_normal.y*normal.z-profile_normal.z*normal.y,
        profile_normal.z*normal.x-profile_normal.x*normal.z,
        profile_normal.x*normal.y-profile_normal.y*normal.x};
    double angle=std::atan2(dot(axis,cross),dot(profile_normal,normal))*180/std::numbers::pi;
    if(angle<=1e-10)angle+=360;
    return angle;
}
} // namespace zima::document
