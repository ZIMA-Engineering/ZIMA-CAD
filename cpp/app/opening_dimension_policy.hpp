#pragma once

#include <zima/sketcher/sketch.hpp>

#include <cmath>
#include <numbers>
#include <optional>

namespace zima::app {

// A cone in axial section is the Sketcher's existing A-B-A symmetric angle:
// one real generator and its mirror about the opening axis. The temporary
// section only presents feature parameters; it is never solved or persisted
// as a second editable Sketch. The feature remains the parameter owner.
[[nodiscard]] inline std::optional<zima::kernel::ViewerDimension>
opening_cone_angle_dimension(const std::string& owner, const std::string& key,
    double degrees, double rim_depth, double rim_radius) {
    if (!(degrees > 0.0 && degrees < 180.0 && rim_radius > 0.0))
        return std::nullopt;
    const double height=rim_radius/std::tan(degrees*std::numbers::pi/360.0);
    const double apex=rim_depth+height;
    zima::sketcher::Sketch section;
    section.id=owner+":dimension-section:"+key;
    section.points={{"apex",0.0,apex},{"rim",rim_radius,rim_depth}};
    section.segments={{"generator","apex","rim"}};
    zima::sketcher::SketchDimension angle;
    angle.id=key;
    angle.kind=zima::sketcher::DimensionKind::AngleSymmetric;
    angle.geometry_id="sketch_axis:y";
    angle.second_geometry_id="generator";
    angle.value=degrees;
    angle.placement=std::array<double,2>{0.0,
        apex-std::hypot(rim_radius,height)*0.8};
    section.dimensions.push_back(angle);
    auto mesh=section.viewer_mesh();
    if (mesh.dimensions.empty()) return std::nullopt;
    auto result=std::move(mesh.dimensions.front());
    result.reference={owner,"parameter:"+key,{}};
    return result;
}

} // namespace zima::app
