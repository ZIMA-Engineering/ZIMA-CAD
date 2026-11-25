#pragma once
#include <zima/kernel/surface_wrap.hpp>
#include <zima/kernel/surface_continuation.hpp>

namespace zima::kernel {
struct WrapHalfPlane {
    double u{},v{},constant{}; // Interior: u*x + v*y + constant >= 0.
    std::string key;
    double value(double x,double y)const{return u*x+v*y+constant;}
};
struct WrapChart {
    SurfaceGeometry surface;
    FaceReference anchor;
    std::vector<FaceReference> coverage;
    SurfaceWrap map;
    std::vector<WrapHalfPlane> limits;
    bool contains(double u,double v,double tolerance=0)const {
        return std::ranges::all_of(limits,[&](const auto& limit){return limit.value(u,v)>=-tolerance;});
    }
};
struct WrapJunction {
    Vec3 point,direction,previous_interior;
    std::string key;
};
inline WrapChart continue_wrap_chart(WrapChart& previous,SurfaceGeometry surface,
        FaceReference anchor,std::vector<FaceReference> coverage,const WrapJunction& join,double tolerance) {
    using namespace wrap_detail;
    const auto ruled=[](const auto& s){return s.kind==SurfaceGeometry::Kind::Cylinder||s.kind==SurfaceGeometry::Kind::Plane;};
    const auto invalid=[]{throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");};
    if(!ruled(previous.surface)||!ruled(surface))invalid();
    const auto edge=unit(join.direction);
    for(const auto& s:{previous.surface,surface})
        if(s.kind==SurfaceGeometry::Kind::Cylinder&&std::abs(dot(unit(s.axis),edge))<1-1e-10)invalid();
    const auto [u,v]=previous.map.source_coordinates(join.point);
    const auto [eu,ev]=previous.map.source_coordinates(add(join.point,edge),u,v);
    const double length2=std::hypot(eu-u,ev-v);
    if(std::abs(length2-1)>tolerance)invalid();
    WrapHalfPlane boundary{-(ev-v)/length2,(eu-u)/length2,0,join.key};
    boundary.constant=-boundary.u*u-boundary.v*v;
    const auto [inside_u,inside_v]=previous.map.source_coordinates(join.previous_interior,u,v);
    if(std::abs(boundary.value(inside_u,inside_v))<1e-12)invalid();
    if(boundary.value(inside_u,inside_v)<0){boundary.u=-boundary.u;boundary.v=-boundary.v;boundary.constant=-boundary.constant;}
    // The symmetric derivative is exact in direction for cylinders and planes.
    const double step=std::max(tolerance,1e-4);
    auto x=unit(sub(previous.map.point(u+step,v),previous.map.point(u-step,v)));
    auto y=unit(sub(previous.map.point(u,v+step),previous.map.point(u,v-step)));
    auto old_normal=previous.map.normal(u,v);
    const SurfaceWrap probe(surface,join.point,x,y);
    auto new_normal=probe.normal(0,0);
    old_normal=scale(old_normal,previous.surface.reversed?-1.:1.);
    new_normal=scale(new_normal,surface.reversed?-1.:1.);
    if(std::abs(dot(edge,old_normal))>1e-8||std::abs(dot(edge,new_normal))>1e-8)invalid();
    const double cosine=std::clamp(dot(old_normal,new_normal),-1.,1.),sine=dot(edge,cross(old_normal,new_normal));
    const auto rotate=[&](Vec3 direction){return add(add(scale(direction,cosine),scale(cross(edge,direction),sine)),scale(edge,dot(edge,direction)*(1-cosine)));};
    SurfaceWrap map(surface,join.point,rotate(x),rotate(y));
    map.set_source_anchor(u,v);
    if(length(sub(map.point(u,v),previous.map.point(u,v)))>tolerance||
       length(sub(map.point(eu,ev),previous.map.point(eu,ev)))>tolerance)invalid();
    previous.limits.push_back(boundary);
    boundary.u=-boundary.u;boundary.v=-boundary.v;boundary.constant=-boundary.constant;
    return {surface,std::move(anchor),std::move(coverage),std::move(map),{std::move(boundary)}};
}
}
