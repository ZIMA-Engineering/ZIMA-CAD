#include <zima/document/placement_drag.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try {
    kernel::ViewerReferenceGeometry geometry;
    const auto plane=[&](const char* id,kernel::Vec3 normal,kernel::Vec3 origin={}) {
        auto support=std::make_shared<kernel::SurfaceGeometry>();support->axis=normal;support->origin=origin;
        kernel::FaceReference face{"support",id,{}};face.surface=std::move(support);geometry.triangle_references.push_back(face);
        return document::ConstructionReference{{},"support",id};
    };
    auto free=document::Placement{};free.x=5;free.y=6;free.z=7;
    const auto moved=document::project_placement_origin_drag(free,geometry,{21,-9,13});
    check(moved&&*moved==kernel::Vec3{21,-9,13},"Free origin did not follow target");
    for(const auto* axis:{"x","y","z"}) {
        auto locked=free;locked.value_locks.insert(axis);const auto result=document::project_placement_origin_drag(locked,geometry,{21,-9,13});
        check(result.has_value(),"Cartesian coordinate lock rejected allowed motion");
        check(axis[0]!='x'||result->x==5,"Locked X moved");check(axis[0]!='y'||result->y==6,"Locked Y moved");check(axis[0]!='z'||result->z==7,"Locked Z moved");
    }
    const double n=std::sqrt(14.);auto oblique=plane("oblique",{1/n,2/n,3/n});oblique.offset=2.;
    auto on_plane=free;on_plane.references={oblique};check(document::resolve_placement(on_plane,geometry),"Oblique baseline unresolved");
    on_plane.value_locks.insert("x");const auto source=on_plane.references;const double x=on_plane.x;
    const auto started=std::chrono::steady_clock::now();
    for(int iteration=0;iteration<120;++iteration) {
        const auto result=document::project_placement_origin_drag(on_plane,geometry,{20.+iteration,11.-iteration*.2,-7.+iteration*.3});
        check(result.has_value(),"Oblique plane drag failed");check(std::abs(result->x-x)<1e-7,"Oblique projection changed locked X");
        check(std::abs((result->x+2*result->y+3*result->z)/n-2)<1e-7,"Oblique drag changed offset");
        on_plane.x=result->x;on_plane.y=result->y;on_plane.z=result->z;
        check(on_plane.references==source,"Drag altered authored reference identity, side or offset");
    }
    std::cout<<"120 exact oblique plane drags: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" ms\n";
    auto cylinder=std::make_shared<kernel::SurfaceGeometry>();cylinder->kind=kernel::SurfaceGeometry::Kind::Cylinder;cylinder->radius=10;
    kernel::FaceReference face{"support","cylinder",{}};face.surface=cylinder;geometry.triangle_references.push_back(face);
    document::Placement curved;curved.x=12;curved.z=5;curved.value_locks.insert("z");curved.references={{{},"support","cylinder",2.}};
    for(const auto target:{kernel::Vec3{8,8,35},kernel::Vec3{-6,11,-4},kernel::Vec3{-10,-3,40},kernel::Vec3{5,-8,-12}}) {
        const auto result=document::project_placement_origin_drag(curved,geometry,target);
        check(result&&std::abs(std::hypot(result->x,result->y)-12)<1e-7&&std::abs(result->z-5)<1e-7,"Cylinder drag changed radius, offset or locked height");
        curved.x=result->x;curved.y=result->y;
    }
    // A rational quadratic is an exact quarter circle. Corrupting only the
    // displayed polyline must not change the attachment or the drag equation.
    kernel::ViewerEdge edge;edge.reference={"support","exact-arc",{}};
    edge.exact_spline=kernel::BSplineGeometry{2,{{10,0,0},{10,10,0},{0,10,0}},
        {0,0,0,1,1,1},{1,std::sqrt(.5),1}};
    edge.points={{900,-700,100},{-100,20,400}};geometry.edges.push_back(edge);
    document::Placement on_curve;on_curve.x=10;on_curve.references={{{},"support","exact-arc"}};
    const auto curve_reference=on_curve.references;
    for(int i=0;i<80;++i) {
        const double angle=(10.+(i%30)*2.)*std::acos(-1.)/180.;
        const auto result=document::project_placement_origin_drag(on_curve,geometry,{15*std::cos(angle),15*std::sin(angle),8.});
        check(result&&std::abs(std::hypot(result->x,result->y)-10)<1e-7&&std::abs(result->z)<1e-7,"Curve drag left the exact authored circle");
        if(std::abs(std::atan2(result->y,result->x)-angle)>=1e-7)std::cerr<<"Curve step "<<i<<" expected="<<angle<<" actual="<<std::atan2(result->y,result->x)<<" xyz="<<result->x<<","<<result->y<<","<<result->z<<"\n";
        check(std::abs(std::atan2(result->y,result->x)-angle)<1e-7,"Curve drag did not follow the available parameter");
        on_curve.x=result->x;on_curve.y=result->y;on_curve.z=result->z;
        check(on_curve.references==curve_reference,"Curve drag changed reference identity");
    }
    on_curve.value_locks.insert("x");const auto fixed_curve=document::project_placement_origin_drag(on_curve,geometry,{0,10,0});
    check(fixed_curve&&std::hypot(fixed_curve->x-on_curve.x,fixed_curve->y-on_curve.y,fixed_curve->z-on_curve.z)<1e-7,"Locked curve point escaped its position");
    document::Placement fixed;fixed.references={plane("x",{1,0,0}),plane("y",{0,1,0}),plane("z",{0,0,1})};
    const auto still=document::project_placement_origin_drag(fixed,geometry,{8,9,10});check(still&&*still==kernel::Vec3{},"Fully constrained origin escaped");
    fixed.references.front().semantic_key="missing";check(!document::project_placement_origin_drag(fixed,geometry,{8,9,10}),"Missing support allowed a drag");
    std::cout<<"Container origin exact projection, repeated drags, locks and invalid-reference contracts passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
