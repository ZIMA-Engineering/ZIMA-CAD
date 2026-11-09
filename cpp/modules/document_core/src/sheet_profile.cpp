#include <zima/document/sheet_profile.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>

namespace zima::document {
namespace {
using Point = std::array<double,2>;

Point point(const zima::sketcher::Sketch& sketch,const std::string& id) {
    const auto* value=sketch.find_point(id);
    if(!value)throw std::runtime_error("Sheet Profile curve endpoint is missing");
    return {value->x,value->y};
}
double distance(Point a,Point b) {
    return std::hypot(b[0]-a[0],b[1]-a[1]);
}
Point unit(Point value) {
    const auto length=std::hypot(value[0],value[1]);
    if(!std::isfinite(length)||length<1e-12)
        throw std::runtime_error("Sheet Profile tangent is invalid");
    return {value[0]/length,value[1]/length};
}
}

SheetProfileRoute sheet_profile_route(const zima::sketcher::Sketch& source,
        const std::string& requested_start) {
    SheetProfileRoute result;
    result.evaluated=source.evaluated_profile_sketch();
    const auto& sketch=result.evaluated;
    const auto modeled=[](const auto& values) {
        return std::ranges::any_of(values,[](const auto& value) {
            return !value.construction;
        });
    };
    if(modeled(sketch.circles)||modeled(sketch.ellipses)||
       modeled(sketch.elliptical_arcs)||modeled(sketch.bsplines)||
       std::ranges::any_of(sketch.texts,[](const auto& value) {
           return value.modeling_geometry;
       })) {
        throw std::runtime_error(
            "Sheet Profile supports one open chain of lines and circular arcs");
    }
    struct Curve {
        SheetProfileSpanKind kind;
        std::string id,first,last;
    };
    std::vector<Curve> curves;
    for(const auto& segment:sketch.segments)if(!segment.construction)
        curves.push_back({SheetProfileSpanKind::Flat,segment.id,
            segment.first_point_id,segment.second_point_id});
    for(const auto& arc:sketch.arcs)if(!arc.construction)
        curves.push_back({SheetProfileSpanKind::Bend,arc.id,
            arc.start_point_id,arc.end_point_id});
    if(curves.empty())throw std::runtime_error("Sheet Profile path is empty");
    std::map<std::string,std::vector<std::size_t>> adjacency;
    for(std::size_t i=0;i<curves.size();++i) {
        adjacency[curves[i].first].push_back(i);
        adjacency[curves[i].last].push_back(i);
    }
    std::vector<std::string> ends;
    for(const auto& [id,connected]:adjacency) {
        if(connected.size()==1)ends.push_back(id);
        else if(connected.size()!=2)
            throw std::runtime_error(
                "Sheet Profile path must be one unbranched open chain");
    }
    if(ends.size()!=2)
        throw std::runtime_error("Sheet Profile path must have two free ends");
    std::ranges::sort(ends);
    if(!requested_start.empty()) {
        const auto selected=std::ranges::find(ends,requested_start);
        if(selected==ends.end())
            throw std::runtime_error(
                "Sheet Profile start must be a free path endpoint");
        if(selected==std::next(ends.begin()))std::swap(ends[0],ends[1]);
    }
    result.start_point_id=ends.front();result.end_point_id=ends.back();
    std::set<std::size_t> visited;
    auto current=result.start_point_id;
    while(visited.size()<curves.size()) {
        const auto candidates=adjacency.find(current);
        if(candidates==adjacency.end())
            throw std::runtime_error("Sheet Profile path is disconnected");
        const auto next=std::ranges::find_if(candidates->second,[&](auto index) {
            return !visited.contains(index);
        });
        if(next==candidates->second.end())
            throw std::runtime_error("Sheet Profile path closes before its end");
        const auto index=*next;visited.insert(index);const auto& curve=curves[index];
        const bool reversed=current==curve.last;
        const auto following=reversed?curve.first:curve.last;
        SheetProfileSpan span;span.kind=curve.kind;span.curve_id=curve.id;
        span.start_point_id=current;span.end_point_id=following;
        span.reversed=reversed;span.start=point(sketch,current);
        span.end=point(sketch,following);
        if(curve.kind==SheetProfileSpanKind::Flat) {
            span.length=distance(span.start,span.end);
            if(!std::isfinite(span.length)||span.length<.001)
                throw std::runtime_error(
                    "Sheet Profile straight span is shorter than 0.001 mm");
            span.start_tangent=span.end_tangent=unit(
                {span.end[0]-span.start[0],span.end[1]-span.start[1]});
        } else {
            const auto arc=std::ranges::find(sketch.arcs,curve.id,
                &zima::sketcher::SketchArc::id);
            if(arc==sketch.arcs.end())
                throw std::logic_error("Sheet Profile arc identity is missing");
            span.center=point(sketch,arc->center_point_id);
            span.radius=arc->radius;
            double sweep=arc->end_angle-arc->start_angle;
            while(sweep<=0)sweep+=2*std::numbers::pi;
            if(sweep>std::numbers::pi+1e-9)
                throw std::runtime_error(
                    "Sheet Profile Bend arc cannot exceed 180 degrees");
            if(reversed)sweep=-sweep;
            const auto start_radial=unit({span.start[0]-span.center[0],
                span.start[1]-span.center[1]});
            const auto end_radial=unit({span.end[0]-span.center[0],
                span.end[1]-span.center[1]});
            const double sign=sweep>0?1.:-1.;
            span.start_tangent={-sign*start_radial[1],sign*start_radial[0]};
            span.end_tangent={-sign*end_radial[1],sign*end_radial[0]};
            span.turn_degrees=sweep*180/std::numbers::pi;
            span.length=std::abs(sweep)*span.radius;
            if(!std::isfinite(span.radius)||span.radius<.001||
               !std::isfinite(span.turn_degrees)||
               std::abs(span.turn_degrees)<1e-7)
                throw std::runtime_error("Sheet Profile Bend arc is invalid");
        }
        if(!result.spans.empty()) {
            const auto& previous=result.spans.back();
            const double cross=previous.end_tangent[0]*span.start_tangent[1]-
                previous.end_tangent[1]*span.start_tangent[0];
            const double dot=previous.end_tangent[0]*span.start_tangent[0]+
                previous.end_tangent[1]*span.start_tangent[1];
            if(std::abs(cross)>1e-6||dot<1-1e-8)
                throw std::runtime_error(
                    "Sheet Profile path spans must meet tangentially");
        }
        result.spans.push_back(std::move(span));current=following;
    }
    if(current!=result.end_point_id)
        throw std::runtime_error("Sheet Profile path is disconnected");
    return result;
}

std::string initialize_attached_sheet_profile(
        zima::sketcher::Sketch& sketch,double thickness) {
    if(!std::isfinite(thickness)||thickness<.001||
       !sketch.points.empty()||!sketch.segments.empty()||
       !sketch.arcs.empty()||!sketch.circles.empty()||
       !sketch.ellipses.empty()||!sketch.elliptical_arcs.empty()||
       !sketch.bsplines.empty()||!sketch.texts.empty())
        throw std::invalid_argument(
            "An attached Sheet Profile needs an empty Sketch and a positive thickness");
    const double outer_radius=2*thickness;
    const auto arc=sketch.add_arc(0,outer_radius,0,0,
        outer_radius,outer_radius);
    const auto contact=sketch.arcs.front().end_point_id;
    const auto line=sketch.add_segment(outer_radius,outer_radius,
        outer_radius,outer_radius+40);
    static_cast<void>(sketch.add_tangent_constraint(arc,line,contact));
    const auto start=sketch.arcs.front().start_point_id;
    if(auto* point=sketch.find_point(start))point->fixed=true;
    sketch.validate();
    return start;
}

} // namespace zima::document
