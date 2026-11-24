#pragma once
#include <zima/kernel/cylindrical_wrap.hpp>
#include <zima/kernel/curved_wrap.hpp>

namespace zima::kernel {
// The Feature's placed frame owns the anchor and rotation. A target's native
// UV origin never controls the inscription position. Projection onto the
// analytic support is derived state, not another independently editable Origin.
class SurfaceWrap {
public:
    SurfaceWrap(const SurfaceGeometry& surface,Vec3 origin,Vec3 x_axis,Vec3 y_axis,
            bool reverse_u=false,bool reverse_v=false)
        :map_(make_map(surface,origin,x_axis,y_axis)),reverse_u_(reverse_u?-1.:1.),reverse_v_(reverse_v?-1.:1.) {
        using namespace wrap_detail;
        const auto normal0=normal(0,0);
        auto projected_x=sub(x_axis,scale(normal0,dot(x_axis,normal0)));
        if(length(projected_x)<1e-12)projected_x=cross(y_axis,normal0);
        const auto x=unit(projected_x);
        const auto basis=std::visit([](const auto& map)->std::pair<Vec3,Vec3> {
            if constexpr(std::is_same_v<std::decay_t<decltype(map)>,CylindricalWrap>)
                return {wrap_detail::cross(map.axis(),map.radial()),map.axis()};
            else return {map.tangent_u(),map.tangent_v()};
        },map_);
        cosine_=dot(x,basis.first);sine_=dot(x,basis.second);
        const auto rotated_y=add(scale(basis.first,-sine_),scale(basis.second,cosine_));
        handedness_=dot(y_axis,rotated_y)<0?-1.:1.;
    }
    [[nodiscard]] Vec3 point(double u,double v,double offset=0)const {
        const auto [a,b]=coordinates(u,v);
        return std::visit([&](const auto& map){return map.point(a,b,offset);},map_);
    }
    [[nodiscard]] Vec3 normal(double u,double v)const {
        const auto [a,b]=coordinates(u,v);
        return std::visit([&](const auto& map){
            if constexpr(std::is_same_v<std::decay_t<decltype(map)>,CylindricalWrap>)return map.normal(a);
            else return map.normal(a,b);
        },map_);
    }
    [[nodiscard]] std::pair<double,double> canonical_coordinates(double u,double v)const{return coordinates(u,v);}
    void validate_image_domain(const std::array<std::array<double,2>,4>& corners)const {
        std::array<std::pair<double,double>,4> points;
        for(std::size_t i=0;i<4;++i)points[i]=coordinates(corners[i][0],corners[i][1]);
        const auto invalid=[]{throw std::runtime_error("Wrapped image crosses a singularity or overlaps itself.");};
        std::visit([&](const auto& map) {
            using T=std::decay_t<decltype(map)>;
            if constexpr(std::is_same_v<T,CylindricalWrap>) {
                const auto [lo,hi]=std::minmax_element(points.begin(),points.end(),[](auto a,auto b){return a.first<b.first;});
                if(hi->first-lo->first>2*std::numbers::pi*map.radius())invalid();
            } else if constexpr(std::is_same_v<T,SphericalWrap>) {
                for(const auto& [u,v]:points)if(std::hypot(u,v)>=std::numbers::pi*map.radius())invalid();
            } else {
                double center_u=0,center_v=0;bool positive=false,negative=false;
                for(auto& [u,v]:points){v+=map.slant_distance();center_u+=u/4;center_v+=v/4;}
                for(std::size_t i=0;i<4;++i) {
                    const auto a=points[i],b=points[(i+1)%4];const double cross=a.first*b.second-a.second*b.first;
                    positive|=cross>0;negative|=cross<0;
                }
                if(!positive||!negative)invalid(); // The developed apex is inside or on the image.
                const double angle=std::atan2(center_u,center_v);
                double minimum=angle,maximum=angle;
                for(const auto& [u,v]:points) {
                    double value=std::atan2(u,v);value+=2*std::numbers::pi*std::round((angle-value)/(2*std::numbers::pi));
                    minimum=std::min(minimum,value);maximum=std::max(maximum,value);
                }
                if(maximum-minimum>=2*std::numbers::pi*map.sine_angle())invalid();
                // The forward cone map uses one continuous developed chart.
                if(minimum<-std::numbers::pi||maximum>std::numbers::pi)invalid();
            }
        },map_);
    }
    [[nodiscard]] std::pair<double,double> source_coordinates(Vec3 point,double reference_u=0,double reference_v=0)const {
        const auto [ra,rb]=coordinates(reference_u,reference_v);
        const auto [a,b]=std::visit([&](const auto& map){
            if constexpr(std::is_same_v<std::decay_t<decltype(map)>,CylindricalWrap>){const auto p=map.coordinates(point,ra);return std::pair{p.x,p.y};}
            else return map.coordinates(point,ra,rb);
        },map_);
        return {reverse_u_*(cosine_*a+sine_*b),reverse_v_*handedness_*(-sine_*a+cosine_*b)};
    }
private:
    using Map=std::variant<CylindricalWrap,ConicalWrap,SphericalWrap>;
    static Map make_map(const SurfaceGeometry& s,Vec3 origin,Vec3 x_axis,Vec3 y_axis) {
        using namespace wrap_detail;
        const auto x=unit(x_axis),y=unit(y_axis),n=unit(cross(x,y));
        if(s.kind==SurfaceGeometry::Kind::Sphere) {
            auto radial=sub(origin,s.origin);if(length(radial)<1e-12)radial=n;
            const auto direction=unit(radial);
            auto tangent=sub(x,scale(direction,dot(x,direction)));
            if(length(tangent)<1e-12)tangent=cross(y,direction);
            return SphericalWrap(s.origin,s.radius,add(s.origin,scale(direction,s.radius)),tangent);
        }
        valid(s.kind==SurfaceGeometry::Kind::Cylinder||s.kind==SurfaceGeometry::Kind::Cone);
        const auto axis=unit(s.axis),delta=sub(origin,s.origin);
        const double z=dot(delta,axis);
        auto radial=sub(delta,scale(axis,z));
        if(length(radial)<1e-12)radial=sub(n,scale(axis,dot(n,axis)));
        if(length(radial)<1e-12)radial=sub(x,scale(axis,dot(x,axis)));
        radial=unit(radial);
        const double radius=s.kind==SurfaceGeometry::Kind::Cylinder?s.radius:s.radius+z*std::tan(s.semi_angle);
        valid(std::isfinite(radius)&&radius>0);
        const auto anchor=add(add(s.origin,scale(axis,z)),scale(radial,radius));
        if(s.kind==SurfaceGeometry::Kind::Cylinder)return CylindricalWrap(s,anchor);
        return ConicalWrap(s,anchor);
    }
    std::pair<double,double> coordinates(double u,double v)const {
        u*=reverse_u_;v*=reverse_v_*handedness_;
        return {cosine_*u-sine_*v,sine_*u+cosine_*v};
    }
    Map map_;
    double reverse_u_{1},reverse_v_{1},cosine_{1},sine_{},handedness_{1};
};
}
