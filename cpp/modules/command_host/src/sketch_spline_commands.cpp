#include "sketch_command_support.hpp"

namespace zima::command_host {
using namespace sketch_commands;
namespace {
const sketcher::SketchBSpline& spline(const Sketch& sketch, const std::string& id) {
    const auto found = std::ranges::find(sketch.bsplines, id, &sketcher::SketchBSpline::id);
    if (found == sketch.bsplines.end())
        throw workspace::SketchOperationError("spline_not_found", "B-spline no longer exists");
    return *found;
}
std::vector<std::array<double,2>> positions(const Sketch& sketch, const sketcher::SketchBSpline& curve) {
    std::vector<std::array<double,2>> result;
    for (const auto& id : curve.control_point_ids) {
        const auto* point = sketch.find_point(id);
        if (!point) throw std::runtime_error("Missing B-spline control point");
        result.push_back({point->x, point->y});
    }
    return result;
}
Json data(const Sketch& sketch, const sketcher::SketchBSpline& curve, std::size_t limit) {
    Json result = {{"geometry",curve.id},{"degree",curve.degree},{"closed",curve.closed},
        {"interpolating",curve.interpolating},{"construction",curve.construction},
        {"exact",!curve.knots.empty() || !curve.weights.empty()},
        {"read_only",sketch.bspline_properties_read_only(curve.id)},
        {"point_count",curve.control_point_ids.size()}};
    const bool omitted = curve.control_point_ids.size() > limit;
    result["geometry_omitted_by_limit"] = omitted;
    if (!omitted) {
        result["points"] = positions(sketch,curve);
        result["point_ids"] = curve.control_point_ids;
        result["knots"] = curve.knots;
        result["weights"] = curve.weights;
    }
    return result;
}
}
void Host::register_sketch_spline_commands() {
    add_sketch_query({"sketch.bspline.get",tr("Read spline properties, stable control points and exact parameterization."),
        {{"geometry",true},{"limit",false,Type::Integer}}},[](const Sketch& sketch,const Json& args) {
        return data(sketch,spline(sketch,args["geometry"].get<std::string>()),integer(args,"limit",256,1,4096));
    });
    add_sketch_command({"sketch.bspline.set",tr("Edit spline properties while preserving identity, exact geometry and constraints."),
        {{"geometry",true},{"degree",false,Type::Integer},{"closed",false,Type::Boolean},{"points",false,Type::Array}}},[](Sketch& sketch,const Json& args) {
        const auto id=args["geometry"].get<std::string>();const auto& curve=spline(sketch,id);
        const auto degree=integer(args,"degree",curve.degree,1,25);
        auto values=positions(sketch,curve);
        if(args.contains("points")) {
            if(args["points"].size()!=values.size() || values.size()>4096)
                invalid("B-spline editing must preserve the number of control points.");
            values.clear();
            for(const auto& entry:args["points"])values.push_back(point(Json{{"point",entry}},"point"));
        }
        sketch.edit_bspline_properties(id,static_cast<unsigned>(degree),args.value("closed",curve.closed),values);
        return data(sketch,spline(sketch,id),256);
    });
}
}
