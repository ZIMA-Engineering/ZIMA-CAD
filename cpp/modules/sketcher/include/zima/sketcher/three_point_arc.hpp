#pragma once
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace zima::sketcher {
struct ThreePointArcGeometry {
    std::array<double,2> center;
    double radius{}, start_angle{}, end_angle{};
    bool clockwise{};
};

// Exact circumcircle in a translated, scaled frame. The linear tolerance is
// also the minimum separation from the endpoint chord; no display samples or
// model calculation are needed for the pointer preview.
[[nodiscard]] inline ThreePointArcGeometry three_point_arc_geometry(
        const std::array<double,2>& start,const std::array<double,2>& end,
        const std::array<double,2>& through,double tolerance=1e-9) {
    constexpr auto error="An arc needs three distinct, non-collinear points.";
    if(!std::isfinite(tolerance)||tolerance<=0)throw std::invalid_argument(error);
    for(const auto& p:{start,end,through})for(double v:p)if(!std::isfinite(v))throw std::invalid_argument(error);
    const double bx=end[0]-start[0],by=end[1]-start[1],cx=through[0]-start[0],cy=through[1]-start[1];
    const double length=std::hypot(bx,by),other=std::hypot(cx,cy),last=std::hypot(through[0]-end[0],through[1]-end[1]);
    if(std::min({length,other,last})<=tolerance)throw std::invalid_argument(error);
    const double scale=std::max({length,other,last});
    const double x=bx/scale,y=by/scale,u=cx/scale,v=cy/scale;
    const double determinant=std::fma(x,v,-y*u);
    if(!std::isfinite(scale)||std::abs(determinant)*scale/(length/scale)<=tolerance)throw std::invalid_argument(error);
    const double b2=x*x+y*y,c2=u*u+v*v;
    ThreePointArcGeometry result;
    result.center={start[0]+scale*(b2*v-c2*y)/(2*determinant),start[1]+scale*(x*c2-u*b2)/(2*determinant)};
    result.radius=std::hypot(start[0]-result.center[0],start[1]-result.center[1]);
    if(!std::isfinite(result.radius)||result.radius<=tolerance)throw std::invalid_argument(error);
    for(const auto& p:{end,through})
        if(std::abs(std::hypot(p[0]-result.center[0],p[1]-result.center[1])-result.radius)>
                tolerance+32*std::numeric_limits<double>::epsilon()*result.radius)throw std::invalid_argument(error);
    result.clockwise=determinant>0;
    result.start_angle=std::atan2(start[1]-result.center[1],start[0]-result.center[0]);
    result.end_angle=std::atan2(end[1]-result.center[1],end[0]-result.center[0]);
    if(result.clockwise)std::swap(result.start_angle,result.end_angle);
    while(result.end_angle<=result.start_angle)result.end_angle+=2*std::acos(-1.);
    return result;
}

struct ThreePointArcResult {
    std::string arc_id,start_point_id,end_point_id,through_point_id;
};
[[nodiscard]] inline ThreePointArcResult add_three_point_arc(Sketch& sketch,
        const std::array<double,2>& start,const std::array<double,2>& end,
        const std::array<double,2>& through,bool construction=false,double snap_tolerance=1e-6) {
    const auto geometry=three_point_arc_geometry(start,end,through);
    auto next=sketch;
    ThreePointArcResult result;
    result.arc_id=next.add_arc(geometry.center[0],geometry.center[1],start[0],start[1],end[0],end[1],
        construction,snap_tolerance,geometry.clockwise);
    const auto arc=next.arcs.back();
    result.start_point_id=geometry.clockwise?arc.end_point_id:arc.start_point_id;
    result.end_point_id=geometry.clockwise?arc.start_point_id:arc.end_point_id;
    result.through_point_id=next.add_point(through[0],through[1],snap_tolerance,construction);
    if(result.through_point_id==arc.center_point_id || result.through_point_id==result.start_point_id || result.through_point_id==result.end_point_id)
        throw std::invalid_argument("An arc needs three distinct, non-collinear points.");
    static_cast<void>(next.add_point_on_circle_constraint(result.through_point_id,result.arc_id));
    next.validate();
    sketch=std::move(next);
    return result;
}
} // namespace zima::sketcher
