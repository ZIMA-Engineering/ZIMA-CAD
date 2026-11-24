#include <zima/kernel/cylindrical_wrap.hpp>
#include <zima/kernel/curved_wrap.hpp>
#include <zima/kernel/surface_wrap.hpp>
#include <iostream>
#include <limits>

using namespace zima::kernel;
namespace {
void check(bool valid,const char* message){if(!valid)throw std::runtime_error(message);}
double distance(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
template<class F>void rejected(F f){bool failed=false;try{f();}catch(const std::invalid_argument&){failed=true;}check(failed,"Invalid cylindrical map accepted");}
Vec3 difference(Vec3 a,Vec3 b,double scale=1){return {(a.x-b.x)*scale,(a.y-b.y)*scale,(a.z-b.z)*scale};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
void curved_maps() {
    for(double angle:{1e-7,.05,.35,1.3})for(double sign:{-1.,1.})
    for(bool reverse_u:{false,true})for(bool reverse_v:{false,true}) {
        SurfaceGeometry cone;cone.kind=SurfaceGeometry::Kind::Cone;cone.radius=30;
        cone.origin={11,-7,20};cone.axis={0,0,sign};cone.semi_angle=sign*angle;
        const Vec3 anchor{41,-7,20};
        ConicalWrap map(cone,anchor,reverse_u,reverse_v);
        check(distance(map.point(0,0),anchor)<1e-11,"Cone anchor moved");
        for(double u:{-7.,0.,7.})for(double v:{-6.,0.,6.}) {
            const auto p=map.point(u,v);const double radial=std::hypot(p.x-11,p.y+7);
            check(std::abs(radial-(30+(p.z-20)*std::tan(angle)))<1e-10,"Mapped curve left the cone");
            constexpr double h=1e-3;
            const auto du=difference(map.point(u+h,v),map.point(u-h,v),.5/h);
            const auto dv=difference(map.point(u,v+h),map.point(u,v-h),.5/h);
            check(std::abs(dot(du,du)-1)<1e-7&&std::abs(dot(dv,dv)-1)<1e-7&&std::abs(dot(du,dv))<1e-7,
                "Conical development changed lengths or angles");
            const auto n=map.normal(u,v);
            check(std::abs(dot(n,n)-1)<1e-12&&std::abs(dot(du,n))<1e-7&&std::abs(dot(dv,n))<1e-7,
                "Conical normal disagrees with the actual derivatives");
            for(double offset:{-2.,0.,3.})check(distance(difference(map.point(u,v,offset),p),wrap_detail::scale(n,offset))<1e-10,
                "Cone offset changed material side");
        }
        rejected([&]{map.point(0,(reverse_v?1.:-1.)*map.slant_distance());});
        cone.radial={0,1,0};cone.reversed=true;
        ConicalWrap other_seam(cone,anchor,reverse_u,reverse_v);
        check(distance(map.point(7,4),other_seam.point(7,4))<1e-12,"Cone kernel seam or face orientation moved the anchor");
    }
    for(double radius:{.05,1.,37.,1000.})for(bool reverse_u:{false,true})for(bool reverse_v:{false,true}) {
        const Vec3 center{3,-7,2},anchor{3,-7,2+radius};
        SphericalWrap map(center,radius,anchor,{1,0,0},reverse_u,reverse_v);
        check(distance(map.point(0,0),anchor)<1e-10,"Sphere anchor moved");
        for(double theta:{1e-7,.02,.5,1.57,2.8})for(double phi:{-2.,-.7,0.,1.3}) {
            const double u=radius*theta*std::cos(phi),v=radius*theta*std::sin(phi);
            const auto p=map.point(u,v),n=map.normal(u,v);
            check(std::abs(distance(p,center)-radius)<1e-10,"Mapped curve left the sphere");
            check(std::abs(n.z-std::cos(theta))<1e-12,"Sphere radial geodesic length changed");
            const double h=radius*1e-4;
            const auto du=difference(map.point(u+h,v),map.point(u-h,v),.5/h);
            const auto dv=difference(map.point(u,v+h),map.point(u,v-h),.5/h);
            const double jacobian=std::sqrt(std::max(0.,dot(du,du)*dot(dv,dv)-std::pow(dot(du,dv),2)));
            check(std::abs(jacobian-std::sin(theta)/theta)<1e-7,"Sphere transverse distortion differs from exponential map");
            check(std::abs(dot(du,n))<1e-7&&std::abs(dot(dv,n))<1e-7,"Sphere normal disagrees with mapped derivatives");
            for(double offset:{-.2*radius,.3*radius})
                check(distance(difference(map.point(u,v,offset),p),wrap_detail::scale(n,offset))<1e-10,"Sphere offset changed material side");
        }
        rejected([&]{map.point(std::numbers::pi*radius,0);});
        rejected([&]{map.point(0,0,-radius);});
        rejected([&]{map.point(std::numeric_limits<double>::infinity(),0);});
    }
    // Non-axis-aligned source frame; no latitude/longitude pole or UV seam.
    SphericalWrap oblique({7,3,-1},12,{11,11,7},{2,-1,0});
    check(distance(oblique.point(0,0),{11,11,7})<1e-11,"Oblique sphere origin changed");
    const auto right=oblique.point(12*std::numbers::pi/2,0);
    check(distance(right,wrap_detail::add(Vec3{7,3,-1},wrap_detail::scale(wrap_detail::unit(Vec3{2,-1,0}),12)))<1e-11,
        "Sphere ignored the source tangent orientation");
    rejected([&]{SphericalWrap invalid({},1,{0,0,1},{0,0,2});});
    std::cout<<"Conical isometry and spherical geodesic mapping: anchors, directions, sides, metrics and singularities passed\n";
}
}
int main(){try {
    for(double radius:{.05,1.,37.,1000.})for(bool reverse_u:{false,true})for(bool reverse_v:{false,true}) {
        SurfaceGeometry cylinder;cylinder.kind=SurfaceGeometry::Kind::Cylinder;cylinder.radius=radius;
        cylinder.origin={13,-7,29};cylinder.axis={0,0,1};
        const Vec3 anchor{13+radius,-7,31};
        CylindricalWrap map(cylinder,anchor,reverse_u,reverse_v);
        check(distance(map.point(0,0),anchor)<1e-10,"Wrap anchor moved");
        const auto quarter=map.point(std::numbers::pi*radius/2,5);
        check(distance(quarter,{13,-7+(reverse_u?-radius:radius),31+(reverse_v?-5.:5.)})<1e-10,
            "Wrap changed authored directions or axial length");
        for(double angle:{-7.,-3.2,-3.1,-1.,0.,1.,3.1,3.2,7.})for(double offset:{-.2*radius,0.,.3*radius}) {
            const auto u=angle*radius;const auto p=map.point(u,-2,offset);
            check(std::abs(std::hypot(p.x-13,p.y+7)-(radius+offset))<1e-9,"Wrap left its offset cylinder");
            check(distance(map.coordinates(p,u),{u,-2,offset})<1e-9,"Wrap inverse lost its turn or side");
            const auto h=radius*1e-5;
            check(std::abs(distance(map.point(u+h,0),map.point(u-h,0))/(2*h)-1)<1e-7,
                "Circumferential mapping changed reference arc length");
        }
        // Continuous inverse crosses the arbitrary atan2 seam without a jump.
        double previous=3*radius;
        for(int i=0;i<=200;++i) {
            const double expected=(3+i*.002)*radius;
            const auto result=map.coordinates(map.point(expected,4),previous);
            check(std::abs(result.x-expected)<1e-9,"Unwrap jumped at the cylinder seam");previous=result.x;
        }
        // Cylinder UV radial direction is not the user's stable wrap anchor.
        cylinder.radial={0,1,0};cylinder.reversed=true;
        CylindricalWrap another_seam(cylinder,anchor,reverse_u,reverse_v);
        check(distance(map.point(radius,7),another_seam.point(radius,7))<1e-10,"Kernel seam changed the wrap");
        rejected([&]{map.point(0,0,-radius);});
        rejected([&]{map.coordinates({13,-7,31});});
    }
    SurfaceGeometry rotated;rotated.kind=SurfaceGeometry::Kind::Cylinder;rotated.radius=5;
    rotated.origin={-10,7,2};rotated.axis={1,2,2};
    const Vec3 anchor{-10,11,3}; // axial component is intentionally nonzero.
    CylindricalWrap map(rotated,anchor);
    for(double u:{-20.,0.,20.})for(double v:{-17.,0.,17.})
        check(distance(map.coordinates(map.point(u,v),u),{u,v,0})<1e-10,"Rotated cylinder inverse is not isometric");
    rotated.radius=0;rejected([&]{CylindricalWrap invalid(rotated,anchor);});
    rotated.radius=5;rotated.axis={};rejected([&]{CylindricalWrap invalid(rotated,anchor);});
    rotated.axis={0,0,1};rotated.kind=SurfaceGeometry::Kind::Cone;rejected([&]{CylindricalWrap invalid(rotated,anchor);});
    rejected([&]{map.point(std::numeric_limits<double>::quiet_NaN(),0);});
    curved_maps();
    for(auto kind:{SurfaceGeometry::Kind::Cylinder,SurfaceGeometry::Kind::Cone,SurfaceGeometry::Kind::Sphere})
    for(bool reverse_u:{false,true})for(bool reverse_v:{false,true}) {
        SurfaceGeometry s;s.kind=kind;s.radius=30;s.axis={0,0,1};s.radial={1,0,0};s.semi_angle=.3;
        const SurfaceWrap mapping(s,{30,0,0},{0,1,0},{0,0,1},reverse_u,reverse_v);
        mapping.validate_image_domain({{{-4,-3},{4,-3},{4,3},{-4,3}}});
        for(double u:{-12.,0.,12.})for(double v:{-8.,0.,8.}) {
            const auto [x,y]=mapping.source_coordinates(mapping.point(u,v),u,v);
            check(std::hypot(x-u,y-v)<1e-10,"Analytic Wrap inverse changed source coordinates");
        }
    }
    SurfaceGeometry cylinder;cylinder.kind=SurfaceGeometry::Kind::Cylinder;cylinder.radius=30;
    const SurfaceWrap cylinder_map(cylinder,{30,0,0},{0,1,0},{0,0,1});
    bool overlap_rejected=false;
    try {cylinder_map.validate_image_domain({{{-100,-3},{100,-3},{100,3},{-100,3}}});}catch(const std::runtime_error&){overlap_rejected=true;}
    check(overlap_rejected,"A multiple-turn image silently overlaps itself");
    SurfaceGeometry cone;cone.kind=SurfaceGeometry::Kind::Cone;cone.radius=30;cone.semi_angle=.3;
    const SurfaceWrap cone_map(cone,{30,0,0},{0,1,0},{0,0,1});
    const double apex=-30/std::sin(.3);bool apex_rejected=false;
    try {cone_map.validate_image_domain({{{-4,apex-3},{4,apex-3},{4,apex+3},{-4,apex+3}}});}catch(const std::runtime_error&){apex_rejected=true;}
    check(apex_rejected,"An image containing the cone apex was accepted");
    std::cout<<"Cylindrical mapping: signed lengths, both directions, radial sides, rotated frames, seams and inverse passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
