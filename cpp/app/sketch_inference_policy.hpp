#pragma once
#include <zima/sketcher/sketch.hpp>
#include <set>
#include <optional>
#include <cmath>
namespace zima::app {
struct SketchInferenceSettings {
    std::set<sketcher::ConstraintKind> disabled;
    bool enabled(sketcher::ConstraintKind kind) const {
        using K=sketcher::ConstraintKind;
        if (kind==K::PointOnLine || kind==K::PointOnCircle || kind==K::PointReference) kind=K::Coincident;
        if (kind==K::EqualRadius) kind=K::EqualLength;
        if (kind==K::MidpointOnLine) kind=K::Midpoint;
        return !disabled.contains(kind);
    }
};
struct SketchPointAlignment {
    std::array<double,2> position;
    std::string point_id;
    sketcher::ConstraintKind kind;
};
struct ExternalPointContact {
    std::array<double,2> end;
    std::string reference_id;
    bool midpoint{};
};
struct SketchCrossAlignment {
    std::array<double,2> position;
    sketcher::ConstraintKind direction;
};
// The point alignment fixes one endpoint coordinate; an independent segment
// direction can fix the other without replacing that existing alignment.
inline std::optional<SketchCrossAlignment> infer_segment_cross_alignment(
        std::array<double,2> start, std::array<double,2> aligned_end,
        sketcher::ConstraintKind alignment, double tolerance,
        const SketchInferenceSettings& settings) {
    using K=sketcher::ConstraintKind;
    if((alignment!=K::Horizontal&&alignment!=K::Vertical)||!settings.enabled(alignment))return std::nullopt;
    const auto direction=alignment==K::Horizontal?K::Vertical:K::Horizontal;
    const auto coordinate=alignment==K::Horizontal?0U:1U;
    if(!settings.enabled(direction)||std::abs(aligned_end[coordinate]-start[coordinate])>tolerance)return std::nullopt;
    aligned_end[coordinate]=start[coordinate];
    if(std::hypot(aligned_end[0]-start[0],aligned_end[1]-start[1])<=1e-9)return std::nullopt;
    return SketchCrossAlignment{aligned_end,direction};
}
// A radial length snap remains on an axis only when the accepted start is
// already on that axis. Other picked supports keep their exact endpoint.
inline std::optional<std::array<double,2>> equal_length_axis_endpoint(
        const sketcher::Sketch& sketch, std::array<double,2> start,
        std::array<double,2> end, const std::string& axis,
        const std::string& reference_id) {
    if(reference_id.empty() || (axis!="sketch_axis:x" && axis!="sketch_axis:y"))return std::nullopt;
    const auto fixed=axis=="sketch_axis:x"?1U:0U;
    if(std::abs(start[fixed])>1e-9 || std::abs(end[fixed])>1e-9)return std::nullopt;
    for(const auto& segment:sketch.segments)if(segment.id==reference_id) {
        const auto* a=sketch.find_point(segment.first_point_id);
        const auto* b=sketch.find_point(segment.second_point_id);
        if(!a||!b)return std::nullopt;
        const auto varying=1U-fixed;
        const double delta=end[varying]-start[varying];
        const double length=std::hypot(b->x-a->x,b->y-a->y);
        if(std::abs(delta)<=1e-12 || length<=1e-12)return std::nullopt;
        end[varying]=start[varying]+std::copysign(length,delta);
        return end;
    }
    return std::nullopt;
}
// Endpoint snapping owns the end position, but does not cancel an already
// offered tangent at the other end. Preview and confirmation share this check.
inline bool start_tangent_matches_endpoint(const sketcher::Sketch& sketch,
        const std::string& curve,std::array<double,2> start,std::array<double,2> end,double tolerance) {
    if(curve.empty())return false;
    const auto tangent=sketch.curve_tangent_at_point(curve,start[0],start[1]);
    return tangent && std::hypot(end[0]-start[0],end[1]-start[1])>1e-9 &&
        std::abs((end[0]-start[0])*(*tangent)[1]-(end[1]-start[1])*(*tangent)[0])<=tolerance;
}
inline std::vector<ExternalPointContact> infer_external_point_contacts(const sketcher::Sketch& sketch,
        std::array<double,2> start,std::array<double,2> end,double tolerance,const SketchInferenceSettings& settings) {
    std::vector<ExternalPointContact> midpoints,contacts;
    const double dx=end[0]-start[0],dy=end[1]-start[1],squared=dx*dx+dy*dy;
    if(squared<1e-16)return {};
    for(const auto& ref:sketch.external_references) {
        if(!sketcher::is_external_point_kind(ref.kind)||ref.broken||ref.cached_points.size()!=1)continue;
        const auto p=ref.cached_points.front();
        const double t=((p[0]-start[0])*dx+(p[1]-start[1])*dy)/squared;
        if(t<=1e-6||t>=1-1e-6)continue; // Endpoints belong to the common point picker.
        if(settings.enabled(sketcher::ConstraintKind::Midpoint)&&
            std::hypot(p[0]-(start[0]+end[0])*.5,p[1]-(start[1]+end[1])*.5)<=tolerance)
            midpoints.push_back({{2*p[0]-start[0],2*p[1]-start[1]},ref.id,true});
        const double rx=p[0]-start[0],ry=p[1]-start[1],r2=rx*rx+ry*ry;
        if(settings.enabled(sketcher::ConstraintKind::Coincident)&&r2>1e-16&&
            std::abs(rx*dy-ry*dx)/std::sqrt(squared)<=tolerance) {
            const double scale=(dx*rx+dy*ry)/r2;
            contacts.push_back({{start[0]+scale*rx,start[1]+scale*ry},ref.id,false});
        }
    }
    midpoints.insert(midpoints.end(),contacts.begin(),contacts.end());return midpoints;
}
inline std::optional<SketchPointAlignment> infer_sketch_point_alignment(
        const sketcher::Sketch& sketch,std::array<double,2> position,double tolerance,
        const SketchInferenceSettings& settings) {
    std::optional<SketchPointAlignment> result;
    double best=tolerance;
    for (const auto& point : sketch.points) {
        if (std::hypot(position[0]-point.x,position[1]-point.y)<=1e-9) continue;
        if (settings.enabled(sketcher::ConstraintKind::Horizontal) && std::abs(position[1]-point.y)<best) {
            best=std::abs(position[1]-point.y);
            result=SketchPointAlignment{{position[0],point.y},point.id,sketcher::ConstraintKind::Horizontal};
        }
        if (settings.enabled(sketcher::ConstraintKind::Vertical) && std::abs(position[0]-point.x)<best) {
            best=std::abs(position[0]-point.x);
            result=SketchPointAlignment{{point.x,position[1]},point.id,sketcher::ConstraintKind::Vertical};
        }
    }
    return result;
}
}
