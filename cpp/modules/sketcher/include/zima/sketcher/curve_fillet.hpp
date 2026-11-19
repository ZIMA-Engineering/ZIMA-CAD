#pragma once
#include <zima/sketcher/sketch.hpp>

namespace zima::sketcher {
// Native endpoints only. Closed curves do not define a corner to trim.
[[nodiscard]] std::optional<std::array<std::string,2>> fillet_curve_endpoints(
    const Sketch&,const std::string&);
struct CurveFilletGeometry {
    kernel::Vec3 center;
    std::array<kernel::Vec3,2> contacts;
    std::array<double,2> parameters;
    std::array<bool,2> trim_start;
};
// Finds a tangent circle on the exact native supports, independently of display
// tessellation. The optional shared vertex fixes the intended corner branch.
[[nodiscard]] CurveFilletGeometry curve_fillet_geometry(const Sketch&,
    const std::string&,const std::string&,double radius,
    const std::string& shared_vertex = {});
// Ordinary native curves and a real tangent arc. No radius dimension is added.
// The complete operation is transactional; failed geometry leaves input intact.
[[nodiscard]] CornerFilletResult fillet_sketch_curves(Sketch&,
    const std::string&,const std::string&,double radius,bool derived = false);
}
