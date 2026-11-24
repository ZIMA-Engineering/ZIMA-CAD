#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <numbers>

namespace zima::kernel {
namespace wrap_detail {
inline bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 scale(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double length(Vec3 p){return std::hypot(p.x,p.y,p.z);}
inline Vec3 unit(Vec3 p){const auto d=length(p);if(!finite(p)||!std::isfinite(d)||d<=0)throw std::invalid_argument("Invalid Feature definition.");return scale(p,1/d);}
inline void valid(bool value){if(!value)throw std::invalid_argument("Invalid Feature definition.");}
}

// Isometry from a flat sector to a cone. The source origin is the anchor;
// u is circumferential and v runs along a generator away from the apex.
// Neither the OCCT parameter seam nor the face's material side sets this frame.
class ConicalWrap {
public:
    ConicalWrap(const SurfaceGeometry& cone,Vec3 anchor,bool reverse_u=false,bool reverse_v=false)
        :u_sign_(reverse_u?-1.:1.),v_sign_(reverse_v?-1.:1.) {
        using namespace wrap_detail;
        valid(cone.kind==SurfaceGeometry::Kind::Cone&&finite(anchor)&&finite(cone.origin)&&
            std::isfinite(cone.radius)&&cone.radius>=0&&std::isfinite(cone.semi_angle)&&
            std::abs(cone.semi_angle)>0&&std::abs(cone.semi_angle)<std::numbers::pi/2);
        const double angle=std::abs(cone.semi_angle);
        sin_=std::sin(angle);cos_=std::cos(angle);
        axis_=scale(unit(cone.axis),cone.semi_angle<0?-1.:1.);
        const auto delta=sub(anchor,cone.origin);
        const double z=dot(delta,axis_);
        radial_=unit(sub(delta,scale(axis_,z)));
        radius_=cone.radius+z*std::tan(angle);
        valid(std::isfinite(radius_)&&radius_>0);
        slant_=radius_/sin_;
        valid(std::isfinite(slant_));
        anchor_=add(add(cone.origin,scale(axis_,z)),scale(radial_,radius_));
        tangent_=cross(axis_,radial_);
    }
    [[nodiscard]] Vec3 point(double u,double v,double offset=0)const {
        using namespace wrap_detail;
        const auto q=polar(u,v);
        valid(std::isfinite(offset)&&q.first*sin_+offset*cos_>0);
        const auto radial=add(scale(radial_,std::cos(q.second)),scale(tangent_,std::sin(q.second)));
        // Rationalized slant difference avoids cancellation on shallow cones.
        const double x=u_sign_*u,y=v_sign_*v;
        const double delta=(x*(x/(q.first+slant_))+y*((2*slant_+y)/(q.first+slant_)));
        return add(add(anchor_,scale(axis_,delta*cos_-offset*sin_)),
            sub(scale(radial,q.first*sin_+offset*cos_),scale(radial_,radius_)));
    }
    [[nodiscard]] Vec3 normal(double u,double v)const {
        using namespace wrap_detail;
        const auto q=polar(u,v);
        return sub(scale(add(scale(radial_,std::cos(q.second)),scale(tangent_,std::sin(q.second))),cos_),scale(axis_,sin_));
    }
    [[nodiscard]] Vec3 anchor()const{return anchor_;}
    [[nodiscard]] Vec3 tangent_u()const{return wrap_detail::scale(tangent_,u_sign_);}
    [[nodiscard]] Vec3 tangent_v()const{return wrap_detail::scale(wrap_detail::add(wrap_detail::scale(radial_,sin_),wrap_detail::scale(axis_,cos_)),v_sign_);}
    [[nodiscard]] double slant_distance()const{return slant_;}
    [[nodiscard]] double sine_angle()const{return sin_;}
    [[nodiscard]] std::pair<double,double> coordinates(Vec3 point,double reference_u=0,double reference_v=0)const {
        using namespace wrap_detail;
        const auto d=sub(point,anchor_);
        const double s=slant_+dot(d,axis_)/cos_;
        const auto radial=add(scale(radial_,radius_),sub(d,scale(axis_,dot(d,axis_))));
        double angle=std::atan2(dot(radial,tangent_),dot(radial,radial_));
        const double expected=polar(reference_u,reference_v).second;
        angle+=2*std::numbers::pi*std::round((expected-angle)/(2*std::numbers::pi));
        return {u_sign_*s*std::sin(angle*sin_),v_sign_*(s*std::cos(angle*sin_)-slant_)};
    }
private:
    std::pair<double,double> polar(double u,double v)const {
        using namespace wrap_detail;
        valid(std::isfinite(u)&&std::isfinite(v));
        const double x=u_sign_*u,y=slant_+v_sign_*v;
        const double slant=std::hypot(x,y);
        valid(std::isfinite(slant)&&slant>0);
        // The negative developed ray is the chart cut, distinct from OCCT's
        // arbitrary UV seam. Curves crossing it must be split into charts.
        return {slant,std::atan2(x,y)/sin_};
    }
    Vec3 anchor_,axis_,radial_,tangent_;
    double radius_{},slant_{},sin_{},cos_{},u_sign_{1},v_sign_{1};
};

// Exponential map on a sphere: radial source distances are geodesic lengths.
// This intentionally is not an isometry: transverse scale is sinc(rho/R).
// The chart ends at the antipode, where all directions meet and rank is lost.
class SphericalWrap {
public:
    SphericalWrap(Vec3 center,double radius,Vec3 anchor,Vec3 tangent_u,
            bool reverse_u=false,bool reverse_v=false)
        :center_(center),radius_(radius),u_sign_(reverse_u?-1.:1.),v_sign_(reverse_v?-1.:1.) {
        using namespace wrap_detail;
        valid(finite(center)&&finite(anchor)&&finite(tangent_u)&&std::isfinite(radius)&&radius>0);
        radial_=unit(sub(anchor,center));
        tangent_=unit(sub(tangent_u,scale(radial_,dot(tangent_u,radial_))));
        bitangent_=cross(radial_,tangent_);
    }
    [[nodiscard]] Vec3 normal(double u,double v)const {
        using namespace wrap_detail;
        valid(std::isfinite(u)&&std::isfinite(v));
        const double rho=std::hypot(u,v),theta=rho/radius_;
        valid(std::isfinite(theta)&&theta<std::numbers::pi);
        if(rho==0)return radial_;
        const double sinc=theta<1e-4?1-theta*theta/6+theta*theta*theta*theta/120:std::sin(theta)/theta;
        return add(scale(radial_,std::cos(theta)),scale(add(scale(tangent_,u_sign_*u/radius_),scale(bitangent_,v_sign_*v/radius_)),sinc));
    }
    [[nodiscard]] Vec3 point(double u,double v,double offset=0)const {
        wrap_detail::valid(std::isfinite(offset)&&radius_+offset>0);
        return wrap_detail::add(center_,wrap_detail::scale(normal(u,v),radius_+offset));
    }
    [[nodiscard]] Vec3 tangent_u()const{return wrap_detail::scale(tangent_,u_sign_);}
    [[nodiscard]] Vec3 tangent_v()const{return wrap_detail::scale(bitangent_,v_sign_);}
    [[nodiscard]] double radius()const{return radius_;}
    [[nodiscard]] std::pair<double,double> coordinates(Vec3 point,double=0,double=0)const {
        using namespace wrap_detail;
        const auto n=unit(sub(point,center_));const double c=std::clamp(dot(n,radial_),-1.,1.);
        const auto t=sub(n,scale(radial_,c));const double s=length(t);
        if(s<1e-14){valid(c>0);return {0,0};}
        const double factor=radius_*std::atan2(s,c)/s;
        return {u_sign_*factor*dot(t,tangent_),v_sign_*factor*dot(t,bitangent_)};
    }
private:
    Vec3 center_,radial_,tangent_,bitangent_;
    double radius_{},u_sign_{1},v_sign_{1};
};
}
