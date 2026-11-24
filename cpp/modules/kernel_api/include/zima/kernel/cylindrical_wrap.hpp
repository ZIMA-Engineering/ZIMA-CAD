#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <numbers>

namespace zima::kernel {

// Shared analytic map for explicit native construction and lightweight preview.
// The anchor is independent of a kernel's arbitrary cylindrical parameter seam.
// u and v are authored lengths, not angles or normalized texture coordinates.
class CylindricalWrap {
public:
    CylindricalWrap(const SurfaceGeometry& cylinder,Vec3 anchor,
            bool reverse_u=false,bool reverse_v=false)
        :radius_(cylinder.radius),u_sign_(reverse_u?-1.:1.),v_sign_(reverse_v?-1.:1.) {
        if(cylinder.kind!=SurfaceGeometry::Kind::Cylinder||!finite(anchor)||
                !finite(cylinder.origin)||!std::isfinite(radius_)||radius_<=0)
            throw std::invalid_argument("Invalid Feature definition.");
        axis_=unit(cylinder.axis);
        const auto delta=sub(anchor,cylinder.origin);
        origin_=add(cylinder.origin,scale(axis_,dot(delta,axis_)));
        radial_=unit(sub(anchor,origin_));
        tangent_=cross(axis_,radial_);
    }
    [[nodiscard]] Vec3 point(double u,double v,double radial_offset=0)const {
        if(!std::isfinite(u)||!std::isfinite(v)||!std::isfinite(radial_offset)||radius_+radial_offset<=0)
            throw std::invalid_argument("Invalid Feature definition.");
        return add(add(origin_,scale(axis_,v_sign_*v)),scale(normal(u),radius_+radial_offset));
    }
    [[nodiscard]] Vec3 normal(double u)const {
        if(!std::isfinite(u))throw std::invalid_argument("Invalid Feature definition.");
        const auto angle=u_sign_*u/radius_;
        return add(scale(radial_,std::cos(angle)),scale(tangent_,std::sin(angle)));
    }
    // A reference u retains the selected turn when a continuous curve crosses
    // atan2's branch cut. It never clamps or silently discards a complete turn.
    // The third coordinate is signed radial distance from the reference cylinder.
    [[nodiscard]] Vec3 coordinates(Vec3 point,double reference_u=0)const {
        if(!finite(point)||!std::isfinite(reference_u))throw std::invalid_argument("Invalid Feature definition.");
        const auto delta=sub(point,origin_);
        const auto axial=dot(delta,axis_);
        const auto radial=sub(delta,scale(axis_,axial));
        const auto radius=length(radial);
        if(radius<=0)throw std::invalid_argument("Invalid Feature definition.");
        const auto angle=std::atan2(dot(radial,tangent_),dot(radial,radial_));
        const auto period=2*std::numbers::pi*radius_;
        const auto u=u_sign_*angle*radius_;
        return {u+period*std::round((reference_u-u)/period),v_sign_*axial,radius-radius_};
    }
    [[nodiscard]] double radius()const{return radius_;}
    [[nodiscard]] Vec3 origin()const{return origin_;}
    [[nodiscard]] Vec3 axis()const{return axis_;}
    [[nodiscard]] Vec3 radial()const{return radial_;}
    [[nodiscard]] double u_sign()const{return u_sign_;}
    [[nodiscard]] double v_sign()const{return v_sign_;}
private:
    static bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
    static double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
    static double length(Vec3 v){return std::hypot(v.x,v.y,v.z);}
    static Vec3 scale(Vec3 v,double s){return {v.x*s,v.y*s,v.z*s};}
    static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
    static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
    static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    static Vec3 unit(Vec3 v){const auto n=length(v);if(!finite(v)||n<=0)throw std::invalid_argument("Invalid Feature definition.");return scale(v,1/n);}
    double radius_{},u_sign_{1},v_sign_{1};
    Vec3 origin_,axis_,radial_,tangent_;
};
}
