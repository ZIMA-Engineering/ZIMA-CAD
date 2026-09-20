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
