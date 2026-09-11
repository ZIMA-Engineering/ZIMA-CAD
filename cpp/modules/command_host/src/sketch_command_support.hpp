#pragma once
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <cmath>
#include <algorithm>

namespace zima::command_host::sketch_commands {
using Sketch=sketcher::Sketch;
using Type=commands::ArgumentType;
inline void invalid(const char* message) {throw workspace::SketchOperationError("invalid_arguments",message);}
inline double number(const Json& value) {
    if(!value.is_number())invalid("Coordinates must be finite numbers in millimetres.");
    const double result=value.get<double>();if(!std::isfinite(result))invalid("Coordinates must be finite numbers in millimetres.");return result;
}
inline std::array<double,2> point(const Json& args,const char* key) {
    const auto& data=args.at(key);if(!data.is_array() || data.size()!=2)invalid("A point must be [x, y] in Sketch millimetres.");
    return {number(data[0]),number(data[1])};
}
inline double snap(const Json& args) {
    const double value=args.value("snap_mm",1e-6);if(!std::isfinite(value)||value<=0)invalid("Snap tolerance must be positive millimetres.");return value;
}
inline std::vector<std::string> ids(const Json& args,const char* key) {
    if(!args.contains(key))return {};const auto& data=args.at(key);
    if(!data.is_array() || data.size()>4096)invalid("An ID list must contain at most 4096 strings.");
    std::vector<std::string> result;
    for(const auto& value:data) {if(!value.is_string() || value.get_ref<const std::string&>().empty())invalid("An ID list must contain nonempty strings.");result.push_back(value.get<std::string>());}
    return result;
}
inline std::size_t integer(const Json& args,const char* key,std::size_t fallback,std::size_t low,std::size_t high) {
    if(!args.contains(key))return fallback;const auto& data=args.at(key);
    if(!data.is_number_integer() || data<low || data>high)invalid("Integer argument is outside its allowed range.");return data.get<std::size_t>();
}
inline Json metadata(const Sketch& sketch) {
    return {{"sketch",sketch.id},{"owner",sketch.owner_container_id},{"name",sketch.name},
        {"plane",sketch.plane==sketcher::SketchPlane::XY?"XY":sketch.plane==sketcher::SketchPlane::XZ?"XZ":"YZ"},
        {"suppressed",sketch.suppressed},{"coordinates","sketch"},{"length_unit","mm"},{"curve_angle_unit","radians"},{"dimension_angle_unit","degrees"},
        {"counts",{{"points",sketch.points.size()},{"segments",sketch.segments.size()},
            {"circles",sketch.circles.size()},{"arcs",sketch.arcs.size()},{"ellipses",sketch.ellipses.size()},
            {"elliptical_arcs",sketch.elliptical_arcs.size()},{"bsplines",sketch.bsplines.size()},
            {"texts",sketch.texts.size()},{"constraints",sketch.constraints.size()},{"dimensions",sketch.dimensions.size()},
            {"external_references",sketch.external_references.size()},{"offsets",sketch.offsets.size()}}}};
}
inline bool geometry_exists(const Sketch& sketch,const std::string& id) {
    const auto contains=[&](const auto& items){return std::ranges::any_of(items,[&](const auto& item){return item.id==id;});};
    return contains(sketch.segments)||contains(sketch.circles)||contains(sketch.arcs)||contains(sketch.ellipses)||
        contains(sketch.elliptical_arcs)||contains(sketch.bsplines)||contains(sketch.texts)||contains(sketch.corner_radii);
}
inline void require_geometry(const Sketch& sketch,const std::string& id) {
    if(!geometry_exists(sketch,id))throw workspace::SketchOperationError("geometry_not_found","The native Sketch geometry does not exist.");
}
inline void require_point(const Sketch& sketch,const std::string& id) {
    if(!sketch.find_point(id))throw workspace::SketchOperationError("point_not_found","The native Sketch point does not exist.");
}
} // namespace zima::command_host::sketch_commands
