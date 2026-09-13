#include <zima/document/hole_profiles.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace zima::document {
void update_hole_profiles(HoleParameters& value) {
    auto draft=value;
    auto bore=sketcher::Sketch::from_serialized(draft.sketch_serialized);
    const auto circle=std::ranges::find(bore.circles,draft.circle_id,&sketcher::SketchCircle::id);
    if(circle!=bore.circles.end()) {
        circle->radius=draft.diameter/2;
        draft.sketch_serialized=bore.serialized();
    }
    const double radius=draft.diameter/2;
    const auto axial_frame=[](sketcher::Sketch& sketch) {
        // These profiles use positive ordinate as bore depth. The default XZ
        // Sketch ordinate points toward -Z, opposite to the Hole bore (+Z).
        // Store the Hole-owned axial frame; the common placement stays intact.
        sketch.resolved_y_axis={0,0,1};
        sketch.resolved_normal={0,-1,0};
    };
    auto chamfer=sketcher::Sketch::from_serialized(draft.chamfer_sketch_serialized);
    axial_frame(chamfer);
    if(draft.entrance_chamfer>0) {
        const std::array<std::array<double,2>,3> points{{
            {{radius,0}},{{radius+draft.entrance_chamfer,0}},{{radius,draft.entrance_chamfer}}}};
        for(std::size_t i=0;i<points.size();++i)if(auto* point=chamfer.find_point(draft.chamfer_point_ids[i])) {
            point->x=points[i][0];point->y=points[i][1];
        }
        draft.chamfer_sketch_serialized=chamfer.serialized();
    }
    auto tip=sketcher::Sketch::from_serialized(draft.tip_sketch_serialized);
    axial_frame(tip);
    const double angle=std::clamp(draft.drill_point_angle_degrees*std::numbers::pi/360,1e-4,std::numbers::pi/2-1e-4);
    const double depth=radius/std::tan(angle);
    const std::array<std::array<double,2>,3> points=draft.drill_point_enabled
        ? std::array<std::array<double,2>,3>{{{{0,draft.bore_length}},{{radius,draft.bore_length}},{{0,draft.bore_length+depth}}}}
        : std::array<std::array<double,2>,3>{{{{radius,draft.bore_length-draft.exit_chamfer}},{{radius+draft.exit_chamfer,draft.bore_length}},{{radius,draft.bore_length}}}};
    for(std::size_t i=0;i<points.size();++i)if(auto* point=tip.find_point(draft.tip_point_ids[i])) {
        point->x=points[i][0];point->y=points[i][1];
    }
    draft.tip_sketch_serialized=tip.serialized();
    value=std::move(draft);
}
}
