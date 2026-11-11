#include <zima/document/sheet_rip.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>

namespace zima::document {
namespace {
using E=kernel::ExtrusionRequest;
using Loop=E::ProfileLoop;

kernel::Vec3 direction(kernel::Vec3 from,kernel::Vec3 to,double length) {
    if(!std::isfinite(length)||length<=1e-12)
        throw std::invalid_argument("RIP Sketch contains a degenerate ellipse axis.");
    return {(to.x-from.x)/length,(to.y-from.y)/length,
        (to.z-from.z)/length};
}
E::BSplineCurve spline_curve(const sketcher::Sketch& sketch,
        const std::string& id) {
    const auto exact=sketcher::sketch_curve_geometry(sketch,id);
    E::BSplineCurve result;
    result.degree=exact.degree;
    result.knots=exact.knots;
    result.weights=exact.weights;
    result.control_points.reserve(exact.poles.size());
    for(const auto pole:exact.poles)
        result.control_points.push_back(sketch.world_point(pole.x,pole.y));
    if(result.control_points.empty())
        throw std::invalid_argument("RIP Sketch contains an empty B-spline.");
    result.start=result.control_points.front();
    result.end=result.control_points.back();
    return result;
}
E::EllipticalArcCurve elliptical_arc_curve(const sketcher::Sketch& sketch,
        const sketcher::SketchEllipticalArc& arc) {
    const auto* center=sketch.find_point(arc.center_point_id);
    const auto* major=sketch.find_point(arc.major_point_id);
    const auto* minor=sketch.find_point(arc.minor_point_id);
    const auto* start=sketch.find_point(arc.start_point_id);
    const auto* end=sketch.find_point(arc.end_point_id);
    if(!center||!major||!minor||!start||!end)
        throw std::invalid_argument("RIP Sketch elliptical Arc points are missing.");
    const auto center_world=sketch.world_point(center->x,center->y);
    const bool swap_axes=arc.major_radius<arc.minor_radius;
    const auto axis_world=swap_axes?
        sketch.world_point(minor->x,minor->y):
        sketch.world_point(major->x,major->y);
    E::EllipticalArcCurve result;
    result.start=sketch.world_point(start->x,start->y);
    result.end=sketch.world_point(end->x,end->y);
    result.center=center_world;
    result.major_radius=swap_axes?arc.minor_radius:arc.major_radius;
    result.minor_radius=swap_axes?arc.major_radius:arc.minor_radius;
    result.major_axis_direction=direction(center_world,axis_world,
        result.major_radius);
    result.start_parameter=arc.start_parameter-
        (swap_axes?std::numbers::pi/2:0.);
    result.end_parameter=arc.end_parameter-
        (swap_axes?std::numbers::pi/2:0.);
    result.reversed=arc.reversed;
    return result;
}
void add_curve(kernel::SheetRipRequest& request,const sketcher::Sketch& sketch,
        std::string id,Loop profile) {
    request.curves.push_back({sketch.id,std::move(id),sketch.resolved_normal,
        std::move(profile)});
}
}

kernel::SheetRipRequest sheet_rip_request(
        const std::vector<const sketcher::Sketch*>& sources,double tolerance) {
    if(!std::isfinite(tolerance)||tolerance<=0||tolerance>1)
        throw std::invalid_argument("Invalid RIP calculation tolerance.");
    kernel::SheetRipRequest result;
    result.tolerance=tolerance;
    std::set<std::string> seen;
    for(const auto* source:sources) {
        if(!source||!seen.insert(source->id).second)
            throw std::invalid_argument("RIP Sketch references must be unique.");
        const auto sketch=source->evaluated_profile_sketch();
        const auto* original=source;
        const std::size_t before=result.curves.size();
        for(const auto& segment:sketch.segments)if(!segment.construction) {
            const auto* first=sketch.find_point(segment.first_point_id);
            const auto* second=sketch.find_point(segment.second_point_id);
            if(!first||!second)throw std::invalid_argument(
                "RIP Sketch segment endpoints are missing.");
            E::CurvedProfile profile;
            profile.curves.push_back(E::LineCurve{
                sketch.world_point(first->x,first->y),
                sketch.world_point(second->x,second->y)});
            add_curve(result,*original,segment.id,std::move(profile));
        }
        for(const auto& circle:sketch.circles)if(!circle.construction) {
            const auto* center=sketch.find_point(circle.center_point_id);
            if(!center)throw std::invalid_argument(
                "RIP Sketch Circle center is missing.");
            add_curve(result,*original,circle.id,E::CircleProfile{
                sketch.world_point(center->x,center->y),circle.radius});
        }
        for(const auto& arc:sketch.arcs)if(!arc.construction) {
            const auto* center=sketch.find_point(arc.center_point_id);
            const auto* start=sketch.find_point(arc.start_point_id);
            const auto* end=sketch.find_point(arc.end_point_id);
            if(!center||!start||!end)throw std::invalid_argument(
                "RIP Sketch Arc endpoints are missing.");
            const double middle=(arc.start_angle+arc.end_angle)*.5;
            E::CurvedProfile profile;
            profile.curves.push_back(E::ArcCurve{
                sketch.world_point(start->x,start->y),
                sketch.world_point(
                    center->x+
                        arc.radius*std::cos(middle),
                    center->y+
                        arc.radius*std::sin(middle)),
                sketch.world_point(end->x,end->y)});
            add_curve(result,*original,arc.id,std::move(profile));
        }
        for(const auto& ellipse:sketch.ellipses)if(!ellipse.construction) {
            const auto* center=sketch.find_point(ellipse.center_point_id);
            const auto* major=sketch.find_point(ellipse.major_point_id);
            const auto* minor=sketch.find_point(ellipse.minor_point_id);
            if(!center||!major||!minor)throw std::invalid_argument(
                "RIP Sketch Ellipse points are missing.");
            const bool swap_axes=ellipse.major_radius<ellipse.minor_radius;
            const auto center_world=sketch.world_point(center->x,center->y);
            const auto axis_world=swap_axes?
                sketch.world_point(minor->x,minor->y):
                sketch.world_point(major->x,major->y);
            add_curve(result,*original,ellipse.id,E::EllipseProfile{
                center_world,direction(center_world,axis_world,
                    swap_axes?ellipse.minor_radius:ellipse.major_radius),
                std::max(ellipse.major_radius,ellipse.minor_radius),
                std::min(ellipse.major_radius,ellipse.minor_radius)});
        }
        for(const auto& arc:sketch.elliptical_arcs)if(!arc.construction) {
            E::CurvedProfile profile;
            profile.curves.push_back(elliptical_arc_curve(sketch,arc));
            add_curve(result,*original,arc.id,std::move(profile));
        }
        for(const auto& spline:sketch.bsplines)if(!spline.construction) {
            E::CurvedProfile profile;
            profile.curves.push_back(spline_curve(sketch,spline.id));
            add_curve(result,*original,spline.id,std::move(profile));
        }
        if(result.curves.size()==before)
            throw std::invalid_argument(
                "Each RIP Sketch must contain ordinary curve geometry.");
    }
    if(result.curves.empty())
        throw std::invalid_argument("Select at least one RIP Sketch.");
    return result;
}
}
