#pragma once
#include <zima/sketcher/sketch.hpp>
#include <set>

namespace zima::sketcher {
// Exact source-driven splines keep their poles as native data, but those
// coefficients are not contour points or editable handles. Keep endpoints and
// any point also used by authored geometry, a relation or a dimension visible.
inline std::set<std::string> hidden_derived_control_points(const Sketch& sketch) {
    std::set<std::string> hidden;
    for(const auto& spline:sketch.bsplines)
        if(sketch.bspline_properties_read_only(spline.id)&&spline.control_point_ids.size()>2)
            hidden.insert(spline.control_point_ids.begin()+1,spline.control_point_ids.end()-1);
    if(hidden.empty())return hidden;
    const auto retain=[&](const std::string& id){hidden.erase(id);};
    for(const auto& spline:sketch.bsplines) {
        if(!sketch.bspline_properties_read_only(spline.id))for(const auto& id:spline.control_point_ids)retain(id);
        else if(!spline.control_point_ids.empty()){retain(spline.control_point_ids.front());retain(spline.control_point_ids.back());}
    }
    for(const auto& line:sketch.segments){retain(line.first_point_id);retain(line.second_point_id);}
    for(const auto& circle:sketch.circles)retain(circle.center_point_id);
    for(const auto& arc:sketch.arcs){retain(arc.center_point_id);retain(arc.start_point_id);retain(arc.end_point_id);}
    for(const auto& ellipse:sketch.ellipses){retain(ellipse.center_point_id);retain(ellipse.major_point_id);retain(ellipse.minor_point_id);}
    for(const auto& arc:sketch.elliptical_arcs){retain(arc.center_point_id);retain(arc.major_point_id);retain(arc.minor_point_id);retain(arc.start_point_id);retain(arc.end_point_id);}
    for(const auto& text:sketch.texts)retain(text.anchor_point_id);
    for(const auto& constraint:sketch.constraints){retain(constraint.first_point_id);retain(constraint.second_point_id);retain(constraint.geometry_id);}
    for(const auto& dimension:sketch.dimensions){retain(dimension.first_point_id);retain(dimension.second_point_id);retain(dimension.geometry_id);}
    return hidden;
}
}
