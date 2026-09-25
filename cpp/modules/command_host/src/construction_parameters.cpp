#include "construction_parameters.hpp"
#include <zima/workspace/placement_edit.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace zima::command_host {
using Object = document::ConstructionObject;
using commands::Json;
const char* construction_tangent_name(document::Curve3DTangentMode value) {
    using Mode = document::Curve3DTangentMode;
    switch (value) {
    case Mode::Automatic: return "automatic";
    case Mode::PositiveX: return "+x";
    case Mode::NegativeX: return "-x";
    case Mode::PositiveY: return "+y";
    case Mode::NegativeY: return "-y";
    case Mode::PositiveZ: return "+z";
    case Mode::NegativeZ: return "-z";
    }
    throw std::logic_error("Unknown curve tangent mode");
}
void apply_construction_properties(Object& value, const Json& args, const kernel::ViewerReferenceGeometry& geometry, const Object* parent) {
    bool specified = false;
    if (args.contains("name")) {
        specified = true;
        const auto text = args.at("name").get<std::string>();
        const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
        const auto first = std::find_if_not(text.begin(), text.end(), space);
        if (first == text.end()) throw ConstructionParameterError("invalid_arguments", "Specify a nonempty object name.");
        value.name = {first, std::find_if_not(text.rbegin(), text.rend(), space).base()};
    }
    const auto field = [&](const char* key, document::ConstructionKind required) {
        if (!args.contains(key)) return false;
        specified = true;
        if (value.kind != required)
            throw ConstructionParameterError("invalid_arguments", "This property is unavailable for the construction kind.");
        return true;
    };
    const auto number = [&](const char* key, const char* lock, double minimum, double maximum) {
        const double result = args.at(key).get<double>();
        if (!std::isfinite(result) || result < minimum || result > maximum)
            throw ConstructionParameterError("invalid_arguments", "Construction dimension is outside the supported range.");
        if (value.value_locks.contains(lock)) throw ConstructionParameterError("value_locked", "The construction dimension is locked.");
        return result;
    };
    if (field("display_size_mm", document::ConstructionKind::Axis))
        value.display_size = number("display_size_mm", "length", 0.001, 1000000);
    if (field("reverse_length_mm", document::ConstructionKind::Axis))
        value.axis_reverse_length = number("reverse_length_mm", "reverse_length", 0.001, 1000000);
    if (field("extent_mode", document::ConstructionKind::Axis)) {
        const auto mode = args.at("extent_mode").get<std::string>();
        if (mode != "one_side" && mode != "two_sides" && mode != "symmetric")
            throw ConstructionParameterError("invalid_arguments", "Invalid construction axis extent mode");
        value.axis_extent_mode = mode == "two_sides" ? document::AxisExtentMode::TwoSides
            : mode == "symmetric" ? document::AxisExtentMode::Symmetric : document::AxisExtentMode::OneSide;
    }
    for (std::size_t i=0;i<2;++i) {
        const char* key=i?"reverse_end":"forward_end";
        if(!field(key,document::ConstructionKind::Axis))continue;
        const auto& end=args.at(key);const auto mode=end.at("condition").get<std::string>();
        if(mode!="length"&&mode!="up_to")throw ConstructionParameterError("invalid_arguments","Invalid construction axis extent mode");
        value.axis_ends[i].up_to=mode=="up_to";
        if(value.axis_ends[i].up_to)value.axis_ends[i].target={end.value("instance_path",std::string{}),end.at("owner_id").get<std::string>(),end.at("semantic_key").get<std::string>()};
    }
    if (field("offset_mm", document::ConstructionKind::Plane))
        value.offset = number("offset_mm", "offset", -1000000, 1000000);
    if (field("direction_axis", document::ConstructionKind::Axis)) {
        const auto axis = args.at("direction_axis").get<std::string>();
        if (axis != "x" && axis != "y" && axis != "z")
            throw ConstructionParameterError("invalid_arguments", "Construction axis must be x, y or z.");
        value.direction_axis = axis;
    }
    if (field("base_plane", document::ConstructionKind::Plane)) {
        const auto plane = args.at("base_plane").get<std::string>();
        if (plane != "auto" && plane != "xy" && plane != "xz" && plane != "yz")
            throw ConstructionParameterError("invalid_arguments", "Construction plane must be auto, xy, xz or yz.");
        value.base_plane_auto = plane == "auto";
        if (!value.base_plane_auto) value.base_plane = plane == "xy" ? document::LocalDatumPlane::XY
            : plane == "xz" ? document::LocalDatumPlane::XZ : document::LocalDatumPlane::YZ;
    }
    if (field("curve_type", document::ConstructionKind::Curve3D)) {
        const auto type = args.at("curve_type").get<std::string>();
        if (type != "polyline" && type != "interpolating_spline")
            throw ConstructionParameterError("invalid_arguments", "Curve type must be polyline or interpolating_spline.");
        value.curve_type = type == "polyline" ? document::Curve3DType::Polyline : document::Curve3DType::InterpolatingSpline;
    }
    if (field("rounding_enabled", document::ConstructionKind::Curve3D)) {
        if (value.curve_type != document::Curve3DType::Polyline)
            throw ConstructionParameterError("parameter_not_editable", "Rounding is editable only for a polyline.");
        value.curve_rounding_enabled = args.at("rounding_enabled").get<bool>();
    }
    for (const auto* key : {"radius_mm", "tangent", "tangent_enabled"}) if (args.contains(key)) {
        specified = true;
        if (value.kind != document::ConstructionKind::Point || !parent || parent->kind != document::ConstructionKind::Curve3D)
            throw ConstructionParameterError("invalid_arguments", "Curve point properties require a point owned by a 3D curve.");
    }
    if (args.contains("radius_mm")) {
        const auto index = std::ranges::find_if(parent->curve_points, [&](const auto& point) { return point.id == value.id; });
        if (parent->curve_type != document::Curve3DType::Polyline || !parent->curve_rounding_enabled ||
            index == parent->curve_points.begin() || index == parent->curve_points.end() || index + 1 == parent->curve_points.end())
            throw ConstructionParameterError("parameter_not_editable", "Radius is editable only at an interior point of a rounded polyline.");
        value.curve_radius = number("radius_mm", "radius", 0, 1000000000);
    }
    if (args.contains("tangent")) {
        using Mode = document::Curve3DTangentMode;
        const auto mode = args.at("tangent").get<std::string>();
        const auto modes = {Mode::Automatic, Mode::PositiveX, Mode::NegativeX, Mode::PositiveY,
            Mode::NegativeY, Mode::PositiveZ, Mode::NegativeZ};
        const auto selected = std::ranges::find_if(modes, [&](auto candidate) { return mode == construction_tangent_name(candidate); });
        if (selected == modes.end()) throw ConstructionParameterError("invalid_arguments", "Tangent must be automatic, +x, -x, +y, -y, +z or -z.");
        value.curve_tangent = *selected;
        value.curve_tangent_enabled = *selected != Mode::Automatic;
    }
    if (args.contains("tangent_enabled")) {
        value.curve_tangent_enabled = args.at("tangent_enabled").get<bool>();
        if (value.curve_tangent_enabled && value.curve_tangent == document::Curve3DTangentMode::Automatic)
            value.curve_tangent = document::Curve3DTangentMode::PositiveX;
    }
    if (args.contains("values")) {
        specified = true;
        if (args.at("values").empty()) throw ConstructionParameterError("invalid_arguments", "Specify at least one placement parameter.");
        for (const auto& [key, number] : args.at("values").items()) {
            if (!number.is_number() || !std::isfinite(number.get<double>()))
                throw ConstructionParameterError("invalid_arguments", "Placement parameters must be finite JSON numbers.");
            if (!workspace::assign_placement_dimension(value, geometry, key, number.get<double>()))
                throw ConstructionParameterError("parameter_not_editable", "The placement parameter is unknown, constrained or locked.");
        }
    }
    if (!specified && !args.contains("points")) throw ConstructionParameterError("invalid_arguments", "Specify at least one construction property.");
}
// Point IDs select existing children; entries without an ID allocate native Points.
// This is the complete ordered list from the Properties dialog, not a merge by name.
void apply_curve_points(Object& value, const Json& args, const CurvePointGeometry& reference_geometry) {
    if (!args.contains("points")) return;
    if (value.kind != document::ConstructionKind::Curve3D)
        throw ConstructionParameterError("invalid_arguments", "A point list requires a 3D curve.");
    const auto& entries = args.at("points");
    if (entries.size() < 2 || entries.size() > 5000)
        throw ConstructionParameterError("invalid_arguments", "A 3D curve requires between 2 and 5000 points.");
    const auto old = value.curve_points;
    std::vector<Object> next; next.reserve(entries.size()); std::set<std::string> identities;
    for (const auto& entry : entries) {
        if (!entry.is_object()) throw ConstructionParameterError("invalid_arguments", "Each curve point must be a property object.");
        for (const auto& [key, field] : entry.items()) {
            const bool valid = (key == "construction" || key == "name" || key == "tangent") ? field.is_string()
                : key == "values" ? field.is_object()
                : key == "radius_mm" ? field.is_number()
                : key == "tangent_enabled" ? field.is_boolean() : false;
            if (!valid) throw ConstructionParameterError("invalid_arguments", "Unknown or incorrectly typed curve point property.");
        }
        Object point;
        if (entry.contains("construction")) {
            const auto id = entry.at("construction").get<std::string>();
            const auto found = std::ranges::find_if(old, [&](const auto& child) { return child.id == id; });
            if (found == old.end()) throw ConstructionParameterError("construction_not_found", "The selected point does not belong to this 3D curve.");
            if (!identities.insert(id).second) throw ConstructionParameterError("invalid_arguments", "A curve point cannot appear twice in the same list.");
            point = *found;
        } else {
            point = document::PartDocument::create_construction(document::ConstructionKind::Point);
            point.parent_construction_id = value.id;
        }
        next.push_back(std::move(point));
    }
    value.curve_points = std::move(next);
    kernel::ViewerReferenceGeometry point_geometry;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].contains("values") || value.curve_points[i].references.empty()) continue;
        point_geometry = reference_geometry(value, i);
        break;
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (entry.empty() || (entry.size() == 1 && entry.contains("construction"))) continue;
        apply_construction_properties(value.curve_points[i], entry, point_geometry, &value);
    }
}
} // namespace zima::command_host
