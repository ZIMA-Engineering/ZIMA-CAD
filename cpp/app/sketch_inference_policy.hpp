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
