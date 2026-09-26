#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <optional>
#include <span>
#include <cmath>
#include <limits>

namespace zima::app {
// Locate a point on the already confirmed face, never choose another entity.
inline std::optional<kernel::Vec3> confirmed_face_hit(std::span<const kernel::Vec3> triangles,
    kernel::Vec3 origin,kernel::Vec3 direction) {
    const auto sub=[](auto a,auto b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto cross=[](auto a,auto b){return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    double nearest=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i+2<triangles.size();i+=3) {
        const auto a=triangles[i],e1=sub(triangles[i+1],a),e2=sub(triangles[i+2],a);
        const auto p=cross(direction,e2);const double determinant=dot(e1,p);
        if(std::abs(determinant)<=1e-14)continue;
        const auto t=sub(origin,a);const double u=dot(t,p)/determinant;
        const auto q=cross(t,e1);const double v=dot(direction,q)/determinant;
        if(u < -1e-9 || v < -1e-9 || u+v > 1+1e-9)continue;
        const double distance=dot(e2,q)/determinant;
        if(distance>=0 && distance<nearest)nearest=distance;
    }
    if(!std::isfinite(nearest))return {};
    return kernel::Vec3{origin.x+nearest*direction.x,origin.y+nearest*direction.y,origin.z+nearest*direction.z};
}
}
