#pragma once
#include <zima/kernel/curved_wrap.hpp>

namespace zima::kernel {
// A plane participates in the same developed chart as a cylinder. Coordinates
// are lengths in its tangent frame; the authored placement owns the anchor.
class PlanarWrap {
public:
    PlanarWrap(const SurfaceGeometry& surface,Vec3 anchor) {
        using namespace wrap_detail;
        valid(surface.kind==SurfaceGeometry::Kind::Plane);
        normal_=unit(surface.axis);
        origin_=sub(anchor,scale(normal_,dot(sub(anchor,surface.origin),normal_)));
        auto tangent=sub(surface.radial,scale(normal_,dot(surface.radial,normal_)));
        if(length(tangent)<1e-12) {
            const Vec3 guide=std::abs(normal_.x)<.9?Vec3{1,0,0}:Vec3{0,1,0};
            tangent=sub(guide,scale(normal_,dot(guide,normal_)));
        }
        tangent_=unit(tangent);
        axial_=cross(normal_,tangent_);
    }
    Vec3 point(double u,double v,double offset=0)const {
        using namespace wrap_detail;
        valid(std::isfinite(u)&&std::isfinite(v)&&std::isfinite(offset));
        return add(add(origin_,scale(tangent_,u)),add(scale(axial_,v),scale(normal_,offset)));
    }
    Vec3 normal(double,double)const{return normal_;}
    Vec3 tangent_u()const{return tangent_;}
    Vec3 tangent_v()const{return axial_;}
    std::pair<double,double> coordinates(Vec3 point,double=0,double=0)const {
        const auto delta=wrap_detail::sub(point,origin_);
        return {wrap_detail::dot(delta,tangent_),wrap_detail::dot(delta,axial_)};
    }
private:
    Vec3 origin_,normal_,tangent_,axial_;
};
}
