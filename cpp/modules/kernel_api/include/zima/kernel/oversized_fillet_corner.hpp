#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
#include <optional>

namespace zima::kernel {

// Derived frame only: both the native tool and its lightweight preview consume
// these exact contact equations. No document fields or reference identities.
struct OversizedFilletCorner {
    Vec3 first,second,normal,apex;
    double radius{};
    [[nodiscard]] Vec3 surface(const Vec3& point,double angle) const {
        const double c=std::cos(angle),s=radius*(std::sin(angle)-1.);
        return {apex.x+c*(point.x-apex.x)+s*normal.x,
            apex.y+c*(point.y-apex.y)+s*normal.y,
            apex.z+c*(point.z-apex.z)+s*normal.z};
    }
};

inline std::optional<OversizedFilletCorner> oversized_fillet_corner_frame(
        Vec3 first,Vec3 second,Vec3 center,Vec3 normal,
        Vec3 first_outward,Vec3 second_outward,double radius) {
    const auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto sub=[](Vec3 a,Vec3 b){return Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto offset=[](Vec3 p,Vec3 d,double t){return Vec3{p.x+d.x*t,p.y+d.y*t,p.z+d.z*t};};
    const auto unit=[&](Vec3 p){const double length=std::sqrt(dot(p,p));return length>1.e-12?offset({},p,1./length):Vec3{};};
    normal=unit(normal);first_outward=unit(first_outward);second_outward=unit(second_outward);
    const auto radial_a=sub(center,first),radial_b=sub(center,second);
    const double small=std::sqrt(dot(radial_a,radial_a));
    if(!std::isfinite(radius)||radius<=small+1.e-7||small<1.e-9||dot(normal,normal)<.5||
       std::abs(std::sqrt(dot(radial_b,radial_b))-small)>1.e-7)return {};
    const auto a=unit(radial_a),b=unit(radial_b);
    if(std::abs(dot(first_outward,a))>1.e-9||std::abs(dot(second_outward,b))>1.e-9||
       std::abs(dot(a,normal))>1.e-9||std::abs(dot(b,normal))>1.e-9)return {};
    const double denominator=dot(cross(first_outward,second_outward),normal);
    if(std::abs(denominator)<1.e-9)return {};
    const auto ta=offset(first,a,radius),tb=offset(second,b,radius);
    const auto apex=offset(ta,first_outward,dot(cross(sub(tb,ta),second_outward),normal)/denominator);
    if(dot(sub(apex,ta),first_outward)<-1.e-7||dot(sub(apex,tb),second_outward)<-1.e-7)return {};
    return OversizedFilletCorner{first,second,normal,apex,radius};
}
} // namespace zima::kernel
