#pragma once
#include <zima/kernel/dimension_layout.hpp>
#include <algorithm>
#include <limits>
#include <numbers>

namespace zima::kernel {
// Cheap wire preview only. Explicit calculation intersects the exact face;
// these persisted triangles never define the calculated angular end.
inline double rotation_target_preview_angle(Vec3 point,Vec3 origin,Vec3 direction,
        const ExtrusionLimit& target) {
    const auto axis=dimension_unit(direction),delta=dimension_sub(point,origin);
    const auto center=dimension_add(origin,dimension_scale(axis,dimension_dot(delta,axis)));
    const auto u=dimension_sub(point,center),v=dimension_cross(axis,u);
    double nearest=std::numeric_limits<double>::infinity();
    const auto hit=[&](Vec3 base,Vec3 normal,const Vec3* triangle) {
        const double a=dimension_dot(u,normal),b=dimension_dot(v,normal);
        const double c=dimension_dot(dimension_sub(center,base),normal),radius=std::hypot(a,b);
        if(radius<1e-14||std::abs(c)>radius+1e-10)return;
        const double phase=std::atan2(b,a),offset=std::acos(std::clamp(-c/radius,-1.,1.));
        for(double t:{phase-offset,phase+offset}) {
            t=std::fmod(t,2*std::numbers::pi);if(t<0)t+=2*std::numbers::pi;
            if(t<1e-9)continue;
            if(triangle) {
                const auto p=dimension_add(center,dimension_add(dimension_scale(u,std::cos(t)),dimension_scale(v,std::sin(t))));
                const auto e=dimension_sub(triangle[1],triangle[0]),f=dimension_sub(triangle[2],triangle[0]),d=dimension_sub(p,triangle[0]);
                const double ee=dimension_dot(e,e),ff=dimension_dot(f,f),ef=dimension_dot(e,f),det=ee*ff-ef*ef;
                if(det<1e-24)continue;
                const double x=(dimension_dot(d,e)*ff-dimension_dot(d,f)*ef)/det;
                const double y=(dimension_dot(d,f)*ee-dimension_dot(d,e)*ef)/det;
                if(x< -1e-8||y< -1e-8||x+y>1+1e-8)continue;
            }
            nearest=std::min(nearest,t);
        }
    };
    if(target.planar)hit(target.origin,target.normal,nullptr);
    else for(std::size_t i=0;i+2<target.triangles.size();i+=3) {
        const auto* p=target.triangles.data()+i;
        hit(p[0],dimension_cross(dimension_sub(p[1],p[0]),dimension_sub(p[2],p[0])),p);
    }
    if(!std::isfinite(nearest))throw std::runtime_error("Revolution profile misses target surface.");
    return nearest*180/std::numbers::pi;
}
}
