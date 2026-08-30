#include <zima/sketcher/sketch.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace zima::sketcher {

DimensionKind classify_linear_dimension(
    const std::array<double, 2>& first,
    const std::array<double, 2>& second,
    const std::array<double, 2>& cursor) {
    const double min_x = std::min(first[0], second[0]);
    const double max_x = std::max(first[0], second[0]);
    const double min_y = std::min(first[1], second[1]);
    const double max_y = std::max(first[1], second[1]);
    const double tolerance = std::max(1.0e-6,
        std::hypot(max_x - min_x, max_y - min_y) * 0.05);
    const bool outside_x = cursor[0] < min_x || cursor[0] > max_x;
    const bool outside_y = cursor[1] < min_y || cursor[1] > max_y;
    if (!outside_x && !outside_y) return DimensionKind::Distance;
    if (outside_y && cursor[0] >= min_x - tolerance &&
        cursor[0] <= max_x + tolerance) return DimensionKind::DistanceX;
    if (outside_x && cursor[1] >= min_y - tolerance &&
        cursor[1] <= max_y + tolerance) return DimensionKind::DistanceY;
    const double horizontal_gap = std::min(
        std::abs(cursor[0] - min_x), std::abs(cursor[0] - max_x));
    const double vertical_gap = std::min(
        std::abs(cursor[1] - min_y), std::abs(cursor[1] - max_y));
    return horizontal_gap < vertical_gap
        ? DimensionKind::DistanceY : DimensionKind::DistanceX;
}

double plane_offset_delta_for_normal_displacement(
    SketchPlane plane, double normal_displacement) noexcept {
    static_cast<void>(plane);
    return normal_displacement;
}

namespace {

std::string make_id() {
    static const auto process_nonce = [] {
        std::random_device source;
        std::seed_seq seed{
            source(), source(), source(), source(),
            static_cast<unsigned int>(
                std::chrono::steady_clock::now().time_since_epoch().count())};
        std::mt19937_64 generator(seed);
        return generator();
    }();
    static std::atomic<std::uint64_t> sequence{0};
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << process_nonce << std::setw(16) << serial;
    return stream.str();
}

std::optional<std::array<double, 2>> external_point_position(
    const Sketch& sketch, const std::string& reference_id) {
    if (reference_id == "sketch_origin") {
        return std::array{0.0, 0.0};
    }
    constexpr std::string_view keypoint_prefix{"sketch_keypoint:"};
    if (reference_id.starts_with(keypoint_prefix)) {
        const auto payload = std::string_view(reference_id).substr(
            keypoint_prefix.size());
        const auto kind_separator = payload.find(':');
        const auto quarter_separator = payload.rfind(':');
        if (kind_separator == std::string_view::npos ||
            quarter_separator == kind_separator) return std::nullopt;
        const auto kind = payload.substr(0, kind_separator);
        const std::string geometry_id(payload.substr(
            kind_separator + 1, quarter_separator - kind_separator - 1));
        int quarter{};
        const auto quarter_text = payload.substr(quarter_separator + 1);
        const auto parsed = std::from_chars(quarter_text.data(),
            quarter_text.data() + quarter_text.size(), quarter);
        if (parsed.ec != std::errc{} || parsed.ptr !=
                quarter_text.data() + quarter_text.size() ||
            quarter < 0 || quarter > 3) return std::nullopt;
        constexpr double half_pi = 1.57079632679489661923;
        constexpr double two_pi = 6.28318530717958647692;
        const double parameter = half_pi * static_cast<double>(quarter);
        if (kind == "circle" || kind == "arc") {
            std::string center_id;
            double radius{};
            if (kind == "circle") {
                const auto curve = std::find_if(sketch.circles.begin(),
                    sketch.circles.end(), [&](const auto& value) {
                        return value.id == geometry_id;
                    });
                if (curve == sketch.circles.end()) return std::nullopt;
                center_id = curve->center_point_id;
                radius = curve->radius;
            } else {
                const auto curve = std::find_if(sketch.arcs.begin(),
                    sketch.arcs.end(), [&](const auto& value) {
                        return value.id == geometry_id;
                    });
                if (curve == sketch.arcs.end()) return std::nullopt;
                double in_domain = parameter;
                while (in_domain < curve->start_angle) in_domain += two_pi;
                if (in_domain > curve->end_angle + 1.0e-12)
                    return std::nullopt;
                center_id = curve->center_point_id;
                radius = curve->radius;
            }
            const auto* center = sketch.find_point(center_id);
            if (center == nullptr) return std::nullopt;
            return std::array{center->x + radius * std::cos(parameter),
                              center->y + radius * std::sin(parameter)};
        }
        std::string center_id;
        std::string major_id;
        std::string minor_id;
        if (kind == "ellipse") {
            const auto curve = std::find_if(sketch.ellipses.begin(),
                sketch.ellipses.end(), [&](const auto& value) {
                    return value.id == geometry_id;
                });
            if (curve == sketch.ellipses.end()) return std::nullopt;
            center_id = curve->center_point_id;
            major_id = curve->major_point_id;
            minor_id = curve->minor_point_id;
        } else if (kind == "elliptical_arc") {
            const auto curve = std::find_if(sketch.elliptical_arcs.begin(),
                sketch.elliptical_arcs.end(), [&](const auto& value) {
                    return value.id == geometry_id;
                });
            if (curve == sketch.elliptical_arcs.end()) return std::nullopt;
            double in_domain = parameter;
            while (in_domain < curve->start_parameter) in_domain += two_pi;
            if (in_domain > curve->end_parameter + 1.0e-12)
                return std::nullopt;
            center_id = curve->center_point_id;
            major_id = curve->major_point_id;
            minor_id = curve->minor_point_id;
        } else {
            return std::nullopt;
        }
        const auto* center = sketch.find_point(center_id);
        const auto* major = sketch.find_point(major_id);
        const auto* minor = sketch.find_point(minor_id);
        if (center == nullptr || major == nullptr || minor == nullptr)
            return std::nullopt;
        return std::array{
            center->x + (major->x - center->x) * std::cos(parameter) +
                (minor->x - center->x) * std::sin(parameter),
            center->y + (major->y - center->y) * std::cos(parameter) +
                (minor->y - center->y) * std::sin(parameter)};
    }
    const auto found = std::find_if(sketch.external_references.begin(),
        sketch.external_references.end(), [&](const auto& reference) {
            return reference.id == reference_id &&
                reference.kind == ExternalReferenceKind::Point &&
                reference.cached_points.size() == 1;
        });
    if (found == sketch.external_references.end()) return std::nullopt;
    return found->cached_points.front();
}

bool has_coordinate_axis_reference(const SketchDimension& dimension) noexcept {
    return (dimension.kind == DimensionKind::DistanceX &&
            dimension.geometry_id == "sketch_axis:y") ||
        (dimension.kind == DimensionKind::DistanceY &&
         dimension.geometry_id == "sketch_axis:x");
}
std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>>
external_reference_line(const Sketch& sketch, const std::string& reference_id) {
    const auto found = std::find_if(sketch.external_references.begin(),
        sketch.external_references.end(), [&](const auto& reference) {
            return reference.id == reference_id &&
                (reference.kind == ExternalReferenceKind::Edge ||
                 reference.kind == ExternalReferenceKind::Axis) &&
                reference.cached_points.size() >= 2;
        });
    if (found == sketch.external_references.end()) return std::nullopt;
    const auto& first = found->cached_points.front();
    const auto& second = found->cached_points.back();
    if (std::hypot(second[0] - first[0], second[1] - first[1]) <= 1.0e-12) {
        return std::nullopt;
    }
    return std::pair{first,
        std::array{second[0] - first[0], second[1] - first[1]}};
}

std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>>
segment_or_external_line(const Sketch& sketch, const std::string& geometry_id) {
    if (geometry_id == "sketch_axis:x") {
        return std::pair{std::array{0.0, 0.0}, std::array{1.0, 0.0}};
    }
    if (geometry_id == "sketch_axis:y") {
        return std::pair{std::array{0.0, 0.0}, std::array{0.0, 1.0}};
    }
    const auto segment = std::find_if(sketch.segments.begin(), sketch.segments.end(),
        [&](const auto& value) { return value.id == geometry_id; });
    if (segment != sketch.segments.end()) {
        const auto* first = sketch.find_point(segment->first_point_id);
        const auto* second = sketch.find_point(segment->second_point_id);
        if (first == nullptr || second == nullptr) return std::nullopt;
        return std::pair{std::array{first->x, first->y},
            std::array{second->x - first->x, second->y - first->y}};
    }
    return external_reference_line(sketch, geometry_id);
}

std::optional<std::array<double, 2>> point_on_line_target(
    const Sketch& sketch, const std::string& geometry_id,
    double point_x, double point_y) {
    const auto project = [&](const std::array<double, 2>& first,
            const std::array<double, 2>& second, bool finite)
            -> std::optional<std::array<double, 2>> {
        const double dx = second[0] - first[0];
        const double dy = second[1] - first[1];
        const double length_squared = dx * dx + dy * dy;
        if (length_squared <= 1.0e-18) return std::nullopt;
        double parameter =
            ((point_x - first[0]) * dx + (point_y - first[1]) * dy) /
            length_squared;
        if (finite) parameter = std::clamp(parameter, 0.0, 1.0);
        return std::array{first[0] + parameter * dx,
                          first[1] + parameter * dy};
    };
    if (geometry_id == "sketch_axis:x") {
        return std::array{point_x, 0.0};
    }
    if (geometry_id == "sketch_axis:y") {
        return std::array{0.0, point_y};
    }
    if (const auto segment = std::find_if(
            sketch.segments.begin(), sketch.segments.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        segment != sketch.segments.end()) {
        const auto* first = sketch.find_point(segment->first_point_id);
        const auto* second = sketch.find_point(segment->second_point_id);
        if (first == nullptr || second == nullptr) return std::nullopt;
        return project({first->x, first->y}, {second->x, second->y}, true);
    }
    const auto reference = std::find_if(
        sketch.external_references.begin(), sketch.external_references.end(),
        [&](const auto& value) {
            return value.id == geometry_id &&
                (value.kind == ExternalReferenceKind::Edge ||
                 value.kind == ExternalReferenceKind::Axis ||
                 value.kind == ExternalReferenceKind::Face) &&
                (value.cached_points.size() >= 2 ||
                 !value.cached_paths.empty());
        });
    if (reference == sketch.external_references.end()) return std::nullopt;
    if (reference->kind == ExternalReferenceKind::Axis ||
        reference->kind == ExternalReferenceKind::Face) {
        return project(reference->cached_points.front(),
            reference->cached_points.back(), false);
    }
    std::optional<std::array<double, 2>> closest;
    double closest_distance = std::numeric_limits<double>::infinity();
    const auto consider_path = [&](const auto& path) {
        for (std::size_t index = 1; index < path.size(); ++index) {
            const auto candidate = project(path[index - 1], path[index], true);
            if (!candidate) continue;
            const double distance = std::hypot(
                point_x - (*candidate)[0], point_y - (*candidate)[1]);
            if (distance < closest_distance) {
                closest_distance = distance;
                closest = candidate;
            }
        }
    };
    consider_path(reference->cached_points);
    return closest;
}

std::set<std::string> externally_linked_point_ids(const Sketch& sketch) {
    std::set<std::string> result;
    for (const auto& block : sketch.import_blocks) {
        if (!block.source_path.starts_with("external-reference:")) continue;
        result.insert(block.point_ids.begin(), block.point_ids.end());
    }
    return result;
}

std::optional<std::pair<std::string, std::array<double, 2>>>
external_snap_point(const Sketch& sketch, double x, double y, double tolerance) {
    std::optional<std::pair<std::string, std::array<double, 2>>> result;
    double best = tolerance;
    for (const auto& reference : sketch.external_references) {
        if (reference.kind != ExternalReferenceKind::Point ||
            reference.cached_points.size() != 1) continue;
        const auto& position = reference.cached_points.front();
        const double distance = std::hypot(position[0] - x, position[1] - y);
        if (distance <= best) {
            best = distance;
            result = std::pair{reference.id, position};
        }
    }
    return result;
}

void bind_matching_external_points(Sketch& sketch, double tolerance) {
    for (auto& point : sketch.points) {
        const auto external = external_snap_point(
            sketch, point.x, point.y, tolerance);
        if (!external) continue;
        point.x = external->second[0];
        point.y = external->second[1];
        const bool exists = std::any_of(sketch.constraints.begin(),
            sketch.constraints.end(), [&](const auto& constraint) {
                return constraint.kind == ConstraintKind::Coincident &&
                    ((constraint.first_point_id == point.id &&
                      constraint.second_point_id == external->first) ||
                     (constraint.second_point_id == point.id &&
                      constraint.first_point_id == external->first));
            });
        if (!exists) {
            sketch.constraints.push_back({make_id(), ConstraintKind::Coincident,
                point.id, external->first});
        }
    }
}

const char* plane_name(SketchPlane plane) {
    switch (plane) {
    case SketchPlane::XY: return "xy";
    case SketchPlane::XZ: return "xz";
    case SketchPlane::YZ: return "yz";
    }
    throw std::invalid_argument("Unknown sketch plane");
}

SketchPlane plane_from_name(const std::string& name) {
    if (name == "xy") return SketchPlane::XY;
    if (name == "xz") return SketchPlane::XZ;
    if (name == "yz") return SketchPlane::YZ;
    throw std::runtime_error("Unknown sketch plane");
}

struct SketchFrame {
    zima::kernel::Vec3 origin;
    zima::kernel::Vec3 x_axis;
    zima::kernel::Vec3 y_axis;
    zima::kernel::Vec3 normal;
};

SketchFrame default_sketch_frame(SketchPlane plane, double plane_offset) {
    if (plane == SketchPlane::XY) {
        return {{0.0, 0.0, plane_offset}, {1.0, 0.0, 0.0},
                {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    }
    if (plane == SketchPlane::XZ) {
        // Match Construction Plane XZ: positive offset and extrusion follow
        // +Y. The in-plane Y axis is -Z so X cross Y remains the normal.
        return {{0.0, plane_offset, 0.0}, {1.0, 0.0, 0.0},
                {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}};
    }
    return {{plane_offset, 0.0, 0.0}, {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};
}

const char* constraint_name(ConstraintKind kind) {
    switch (kind) {
    case ConstraintKind::Horizontal: return "horizontal";
    case ConstraintKind::Vertical: return "vertical";
    case ConstraintKind::Coincident: return "coincident";
    case ConstraintKind::Parallel: return "parallel";
    case ConstraintKind::Perpendicular: return "perpendicular";
    case ConstraintKind::EqualLength: return "equal_length";
    case ConstraintKind::EqualRadius: return "equal_radius";
    case ConstraintKind::PointOnCircle: return "point_on_circle";
    case ConstraintKind::PointOnLine: return "point_on_line";
    case ConstraintKind::MidpointOnLine: return "midpoint_on_line";
    case ConstraintKind::Symmetric: return "symmetric";
    case ConstraintKind::Midpoint: return "midpoint";
    case ConstraintKind::Concentric: return "concentric";
    case ConstraintKind::Tangent: return "tangent";
    }
    throw std::invalid_argument("Unknown sketch constraint");
}

ConstraintKind constraint_from_name(const std::string& name) {
    if (name == "horizontal") return ConstraintKind::Horizontal;
    if (name == "vertical") return ConstraintKind::Vertical;
    if (name == "coincident") return ConstraintKind::Coincident;
    if (name == "parallel") return ConstraintKind::Parallel;
    if (name == "perpendicular") return ConstraintKind::Perpendicular;
    if (name == "equal_length") return ConstraintKind::EqualLength;
    if (name == "equal_radius") return ConstraintKind::EqualRadius;
    if (name == "point_on_circle") return ConstraintKind::PointOnCircle;
    if (name == "point_on_line") return ConstraintKind::PointOnLine;
    if (name == "midpoint_on_line") return ConstraintKind::MidpointOnLine;
    if (name == "symmetric") return ConstraintKind::Symmetric;
    if (name == "midpoint") return ConstraintKind::Midpoint;
    if (name == "concentric") return ConstraintKind::Concentric;
    if (name == "tangent") return ConstraintKind::Tangent;
    throw std::runtime_error("Unknown sketch constraint");
}

const char* dimension_name(DimensionKind kind) {
    switch (kind) {
    case DimensionKind::Distance: return "distance";
    case DimensionKind::DistanceX: return "distance_x";
    case DimensionKind::DistanceY: return "distance_y";
    case DimensionKind::DistancePointLine: return "distance_point_line";
    case DimensionKind::DistanceSymmetric: return "distance_symmetric";
    case DimensionKind::DistanceLine: return "distance_line";
    case DimensionKind::Radius: return "radius";
    case DimensionKind::Diameter: return "diameter";
    case DimensionKind::Angle: return "angle";
    case DimensionKind::AngleThreePoint: return "angle_three_point";
    case DimensionKind::AngleBetween: return "angle_between";
    case DimensionKind::EllipseMajorRadius: return "ellipse_major_radius";
    case DimensionKind::EllipseMinorRadius: return "ellipse_minor_radius";
    case DimensionKind::EllipseRotation: return "ellipse_rotation";
    }
    throw std::invalid_argument("Unknown sketch dimension");
}

DimensionKind dimension_from_name(const std::string& name) {
    if (name == "distance") return DimensionKind::Distance;
    if (name == "distance_x") return DimensionKind::DistanceX;
    if (name == "distance_y") return DimensionKind::DistanceY;
    if (name == "distance_point_line") return DimensionKind::DistancePointLine;
    if (name == "distance_symmetric") return DimensionKind::DistanceSymmetric;
    if (name == "distance_line") return DimensionKind::DistanceLine;
    if (name == "radius") return DimensionKind::Radius;
    if (name == "diameter") return DimensionKind::Diameter;
    if (name == "angle") return DimensionKind::Angle;
    if (name == "angle_three_point") return DimensionKind::AngleThreePoint;
    if (name == "angle_between") return DimensionKind::AngleBetween;
    if (name == "ellipse_major_radius") return DimensionKind::EllipseMajorRadius;
    if (name == "ellipse_minor_radius") return DimensionKind::EllipseMinorRadius;
    if (name == "ellipse_rotation") return DimensionKind::EllipseRotation;
    throw std::runtime_error("Unknown sketch dimension");
}

const char* text_horizontal_name(TextHorizontalAlignment alignment) {
    switch (alignment) {
    case TextHorizontalAlignment::Left: return "left";
    case TextHorizontalAlignment::Center: return "center";
    case TextHorizontalAlignment::Right: return "right";
    }
    throw std::invalid_argument("Unknown horizontal text alignment");
}

TextHorizontalAlignment text_horizontal_from_name(const std::string& name) {
    if (name == "left") return TextHorizontalAlignment::Left;
    if (name == "center") return TextHorizontalAlignment::Center;
    if (name == "right") return TextHorizontalAlignment::Right;
    throw std::runtime_error("Unknown horizontal text alignment");
}

const char* text_vertical_name(TextVerticalAlignment alignment) {
    switch (alignment) {
    case TextVerticalAlignment::Bottom: return "bottom";
    case TextVerticalAlignment::Middle: return "middle";
    case TextVerticalAlignment::Top: return "top";
    }
    throw std::invalid_argument("Unknown vertical text alignment");
}

TextVerticalAlignment text_vertical_from_name(const std::string& name) {
    if (name == "bottom") return TextVerticalAlignment::Bottom;
    if (name == "middle") return TextVerticalAlignment::Middle;
    if (name == "top") return TextVerticalAlignment::Top;
    throw std::runtime_error("Unknown vertical text alignment");
}

const char* text_color_name(SketchTextColor color) {
    switch (color) {
    case SketchTextColor::Green: return "green";
    case SketchTextColor::White: return "white";
    case SketchTextColor::Yellow: return "yellow";
    }
    throw std::invalid_argument("Unknown sketch text color");
}

SketchTextColor text_color_from_name(const std::string& name) {
    if (name == "green") return SketchTextColor::Green;
    if (name == "white") return SketchTextColor::White;
    if (name == "yellow") return SketchTextColor::Yellow;
    throw std::runtime_error("Unknown sketch text color");
}

const char* external_reference_kind_name(ExternalReferenceKind kind) {
    switch (kind) {
    case ExternalReferenceKind::Edge: return "edge";
    case ExternalReferenceKind::Point: return "point";
    case ExternalReferenceKind::Axis: return "axis";
    case ExternalReferenceKind::Face: return "face";
    }
    throw std::invalid_argument("Unknown Sketch external reference kind");
}

ExternalReferenceKind external_reference_kind_from_name(
    const std::string& name) {
    if (name == "edge") return ExternalReferenceKind::Edge;
    if (name == "point") return ExternalReferenceKind::Point;
    if (name == "axis") return ExternalReferenceKind::Axis;
    if (name == "face") return ExternalReferenceKind::Face;
    throw std::runtime_error("Unknown Sketch external reference kind");
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) throw std::runtime_error(std::string(field) + " must be finite");
}

double wrapped_degrees(double value) {
    while (value > 180.0) value -= 360.0;
    while (value < -180.0) value += 360.0;
    return value;
}

double geometric_angle_degrees(double value) {
    return std::acos(std::cos(value * 3.14159265358979323846 / 180.0)) *
        180.0 / 3.14159265358979323846;
}

bool is_segment_pair_constraint(ConstraintKind kind) {
    return kind == ConstraintKind::Parallel ||
        kind == ConstraintKind::Perpendicular ||
        kind == ConstraintKind::EqualLength;
}

std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>>
sketch_axis_line(const Sketch& sketch, const std::string& axis_id) {
    if (axis_id == "sketch_axis:x") {
        return std::pair{std::array{0.0, 0.0}, std::array{1.0, 0.0}};
    }
    if (axis_id == "sketch_axis:y") {
        return std::pair{std::array{0.0, 0.0}, std::array{0.0, 1.0}};
    }
    const auto axis = std::find_if(
        sketch.segments.begin(), sketch.segments.end(),
        [&](const auto& value) { return value.id == axis_id; });
    if (axis == sketch.segments.end()) return std::nullopt;
    const auto* first = sketch.find_point(axis->first_point_id);
    const auto* second = sketch.find_point(axis->second_point_id);
    if (first == nullptr || second == nullptr) return std::nullopt;
    return std::pair{
        std::array{first->x, first->y},
        std::array{second->x - first->x, second->y - first->y}};
}

bool is_base_sketch_axis(const std::string& id) {
    return id == "sketch_axis:x" || id == "sketch_axis:y";
}

using SketchLine2 = std::pair<std::array<double, 2>, std::array<double, 2>>;

std::optional<std::pair<SketchLine2, SketchLine2>> angle_dimension_lines(
    const Sketch& sketch, const SketchDimension& dimension) {
    if (dimension.kind != DimensionKind::AngleBetween) return std::nullopt;
    const auto point_line = [&](const std::string& first_id,
            const std::string& second_id) -> std::optional<SketchLine2> {
        const auto* first = sketch.find_point(first_id);
        const auto* second = sketch.find_point(second_id);
        if (first == nullptr || second == nullptr) return std::nullopt;
        return SketchLine2{{first->x, first->y},
            {second->x - first->x, second->y - first->y}};
    };
    if (const auto first = point_line(
            dimension.first_point_id, dimension.second_point_id)) {
        if (const auto second = point_line(
                dimension.geometry_id, dimension.second_geometry_id)) {
            return std::pair{*first, *second};
        }
        const auto reference = sketch_axis_line(sketch, dimension.geometry_id)
            ? sketch_axis_line(sketch, dimension.geometry_id)
            : segment_or_external_line(sketch, dimension.geometry_id);
        if (reference && dimension.second_geometry_id.empty()) {
            return std::pair{*reference, *first};
        }
    }
    const auto reference = sketch_axis_line(sketch, dimension.geometry_id)
        ? sketch_axis_line(sketch, dimension.geometry_id)
        : segment_or_external_line(sketch, dimension.geometry_id);
    const auto driven = segment_or_external_line(
        sketch, dimension.second_geometry_id);
    if (!reference || !driven) return std::nullopt;
    return std::pair{*reference, *driven};
}


std::optional<double> measured_dimension_value(
    const Sketch& sketch, const SketchDimension& dimension) {
    const auto point_position = [&](const std::string& point_id)
        -> std::optional<std::array<double, 2>> {
        if (const auto* point = sketch.find_point(point_id)) {
            return std::array{point->x, point->y};
        }
        return external_point_position(sketch, point_id);
    };
    if (dimension.kind == DimensionKind::EllipseMajorRadius ||
        dimension.kind == DimensionKind::EllipseMinorRadius ||
        dimension.kind == DimensionKind::EllipseRotation) {
        const auto ellipse = std::find_if(sketch.ellipses.begin(), sketch.ellipses.end(),
            [&](const auto& value) { return value.id == dimension.geometry_id; });
        if (ellipse == sketch.ellipses.end()) return std::nullopt;
        return dimension.kind == DimensionKind::EllipseMajorRadius
            ? ellipse->major_radius
            : dimension.kind == DimensionKind::EllipseMinorRadius
                ? ellipse->minor_radius
                : ellipse->rotation * 180.0 / 3.14159265358979323846;
    }
    if (dimension.kind == DimensionKind::Radius ||
        dimension.kind == DimensionKind::Diameter) {
        const auto circle = std::find_if(sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == dimension.geometry_id; });
        if (circle != sketch.circles.end()) {
            return dimension.kind == DimensionKind::Diameter
                ? circle->radius * 2.0 : circle->radius;
        }
        const auto arc = std::find_if(sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == dimension.geometry_id; });
        if (arc != sketch.arcs.end()) {
            return dimension.kind == DimensionKind::Diameter
                ? arc->radius * 2.0 : arc->radius;
        }
        const auto corner = std::find_if(
            sketch.corner_radii.begin(), sketch.corner_radii.end(),
            [&](const auto& value) { return value.id == dimension.geometry_id; });
        return corner == sketch.corner_radii.end() ? std::nullopt
            : std::optional<double>{dimension.kind == DimensionKind::Diameter
                  ? corner->radius * 2.0 : corner->radius};
    }
    if (dimension.kind == DimensionKind::DistancePointLine ||
        dimension.kind == DimensionKind::DistanceSymmetric) {
        const auto reference = sketch_axis_line(sketch, dimension.geometry_id)
            ? sketch_axis_line(sketch, dimension.geometry_id)
            : segment_or_external_line(sketch, dimension.geometry_id);
        const auto first = point_position(dimension.first_point_id);
        if (!reference || !first) return std::nullopt;
        const double length = std::hypot(
            reference->second[0], reference->second[1]);
        if (length <= 1.0e-12) return std::nullopt;
        const auto distance = [&](const std::array<double, 2>& point) {
            return std::abs(
                reference->second[0] * (point[1] - reference->first[1]) -
                reference->second[1] * (point[0] - reference->first[0])) / length;
        };
        double measured = distance(*first);
        if (dimension.kind == DimensionKind::DistanceSymmetric) {
            if (!dimension.second_point_id.empty()) {
                const auto second = point_position(dimension.second_point_id);
                if (!second) return std::nullopt;
                measured = 0.5 * (measured + distance(*second));
            }
            measured *= 2.0;
        }
        return measured;
    }
    if (dimension.kind == DimensionKind::DistanceLine ||
        dimension.kind == DimensionKind::AngleBetween) {
        if (dimension.kind == DimensionKind::AngleBetween) {
            const auto lines = angle_dimension_lines(sketch, dimension);
            if (!lines) return std::nullopt;
            const auto& rv = lines->first.second;
            const auto& dv = lines->second.second;
            const double scale = std::hypot(rv[0], rv[1]) *
                std::hypot(dv[0], dv[1]);
            if (scale <= 1.0e-12) return std::nullopt;
            return std::acos(std::clamp(
                (rv[0] * dv[0] + rv[1] * dv[1]) / scale, -1.0, 1.0)) *
                180.0 / 3.14159265358979323846;
        }
        const auto reference = sketch_axis_line(sketch, dimension.geometry_id)
            ? sketch_axis_line(sketch, dimension.geometry_id)
            : segment_or_external_line(sketch, dimension.geometry_id);
        auto driven = segment_or_external_line(
            sketch, dimension.second_geometry_id);
        if (dimension.kind == DimensionKind::AngleBetween &&
            dimension.second_geometry_id.empty()) {
            const auto first = point_position(dimension.first_point_id);
            const auto second = point_position(dimension.second_point_id);
            if (first && second) {
                driven = std::pair{*first,
                    std::array{(*second)[0] - (*first)[0],
                               (*second)[1] - (*first)[1]}};
            }
        }
        if (!reference || !driven) return std::nullopt;
        const double reference_length = std::hypot(
            reference->second[0], reference->second[1]);
        const double driven_length = std::hypot(
            driven->second[0], driven->second[1]);
        if (reference_length <= 1.0e-12 || driven_length <= 1.0e-12) {
            return std::nullopt;
        }
        if (dimension.kind == DimensionKind::DistanceLine) {
            return std::abs(
                reference->second[0] *
                    (driven->first[1] - reference->first[1]) -
                reference->second[1] *
                    (driven->first[0] - reference->first[0])) /
                reference_length;
        }
        return std::acos(std::clamp(
            (reference->second[0] * driven->second[0] +
             reference->second[1] * driven->second[1]) /
                (reference_length * driven_length), -1.0, 1.0)) *
            180.0 / 3.14159265358979323846;
    }
    if (dimension.kind == DimensionKind::AngleThreePoint) {
        const auto first = point_position(dimension.first_point_id);
        const auto vertex = point_position(dimension.second_point_id);
        const auto second = point_position(dimension.geometry_id);
        if (!first || !vertex || !second) return std::nullopt;
        const double ax = (*first)[0] - (*vertex)[0];
        const double ay = (*first)[1] - (*vertex)[1];
        const double bx = (*second)[0] - (*vertex)[0];
        const double by = (*second)[1] - (*vertex)[1];
        const double scale = std::hypot(ax, ay) * std::hypot(bx, by);
        if (scale <= 1.0e-12) return std::nullopt;
        return std::acos(std::clamp((ax * bx + ay * by) / scale, -1.0, 1.0)) *
            180.0 / 3.14159265358979323846;
    }
    if (has_coordinate_axis_reference(dimension)) {
        const auto point = point_position(dimension.first_point_id);
        if (!point || !dimension.second_point_id.empty()) return std::nullopt;
        return dimension.kind == DimensionKind::DistanceX
            ? (*point)[0] : (*point)[1];
    }
    const auto first = point_position(dimension.first_point_id);
    const auto second = point_position(dimension.second_point_id);
    if (!first || !second) return std::nullopt;
    const double dx = (*second)[0] - (*first)[0];
    const double dy = (*second)[1] - (*first)[1];
    return dimension.kind == DimensionKind::DistanceX ? dx
        : dimension.kind == DimensionKind::DistanceY ? dy
        : dimension.kind == DimensionKind::Angle
            ? std::atan2(dy, dx) * 180.0 / 3.14159265358979323846
            : std::hypot(dx, dy);
}

bool refresh_reference_dimensions(Sketch& sketch) {
    for (auto& dimension : sketch.dimensions) {
        if (dimension.suppressed || dimension.driving) continue;
        const auto measured = measured_dimension_value(sketch, dimension);
        if (!measured ||
            (dimension.lower_limit && *measured < *dimension.lower_limit) ||
            (dimension.upper_limit && *measured > *dimension.upper_limit)) {
            return false;
        }
        dimension.value = *measured;
    }
    return true;
}

std::optional<std::string> center_curve_point_id(
    const Sketch& sketch, const std::string& geometry_id) {
    if (const auto circle = std::find_if(
            sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        circle != sketch.circles.end()) return circle->center_point_id;
    if (const auto arc = std::find_if(
            sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.arcs.end()) return arc->center_point_id;
    if (const auto ellipse = std::find_if(
            sketch.ellipses.begin(), sketch.ellipses.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        ellipse != sketch.ellipses.end()) return ellipse->center_point_id;
    if (const auto arc = std::find_if(
            sketch.elliptical_arcs.begin(), sketch.elliptical_arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.elliptical_arcs.end()) return arc->center_point_id;
    return std::nullopt;
}

std::set<std::string> point_translation_closure(
    const Sketch& sketch, const std::string& point_id) {
    if (sketch.find_point(point_id) == nullptr) return {};
    std::set<std::string> result{point_id};
    bool changed = true;
    while (changed) {
        changed = false;
        const auto insert = [&](const std::string& point_id) {
            if (result.insert(point_id).second) changed = true;
        };
        for (const auto& circle : sketch.circles) {
            if (!result.contains(circle.center_point_id)) continue;
            for (const auto& constraint : sketch.constraints) {
                if (!constraint.suppressed &&
                    constraint.kind == ConstraintKind::PointOnCircle &&
                    constraint.geometry_id == circle.id) {
                    insert(constraint.first_point_id);
                }
            }
        }
        for (const auto& arc : sketch.arcs) {
            if (!result.contains(arc.center_point_id)) continue;
            insert(arc.start_point_id);
            insert(arc.end_point_id);
            for (const auto& constraint : sketch.constraints) {
                if (!constraint.suppressed &&
                    constraint.kind == ConstraintKind::PointOnCircle &&
                    constraint.geometry_id == arc.id) {
                    insert(constraint.first_point_id);
                }
            }
        }
        for (const auto& ellipse : sketch.ellipses) {
            if (!result.contains(ellipse.center_point_id)) continue;
            insert(ellipse.major_point_id);
            insert(ellipse.minor_point_id);
            for (const auto& constraint : sketch.constraints) {
                if (!constraint.suppressed &&
                    constraint.kind == ConstraintKind::PointOnCircle &&
                    constraint.geometry_id == ellipse.id) {
                    insert(constraint.first_point_id);
                }
            }
        }
        for (const auto& arc : sketch.elliptical_arcs) {
            if (!result.contains(arc.center_point_id)) continue;
            insert(arc.major_point_id);
            insert(arc.minor_point_id);
            insert(arc.start_point_id);
            insert(arc.end_point_id);
            for (const auto& constraint : sketch.constraints) {
                if (!constraint.suppressed &&
                    constraint.kind == ConstraintKind::PointOnCircle &&
                    constraint.geometry_id == arc.id) {
                    insert(constraint.first_point_id);
                }
            }
        }
    }
    return result;
}

std::set<std::string> center_curve_translation_points(
    const Sketch& sketch, const std::string& geometry_id) {
    const auto center_id = center_curve_point_id(sketch, geometry_id);
    return center_id ? point_translation_closure(sketch, *center_id)
                     : std::set<std::string>{};
}

std::optional<double> circular_curve_radius(
    const Sketch& sketch, const std::string& geometry_id) {
    if (const auto circle = std::find_if(
            sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        circle != sketch.circles.end()) return circle->radius;
    if (const auto arc = std::find_if(
            sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.arcs.end()) return arc->radius;
    return std::nullopt;
}

std::set<std::string> circular_curve_radial_points(
    const Sketch& sketch, const std::string& geometry_id) {
    if (const auto circle = std::find_if(
            sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        circle != sketch.circles.end()) {
        std::set<std::string> result;
        for (const auto& constraint : sketch.constraints) {
            if (!constraint.suppressed &&
                constraint.kind == ConstraintKind::PointOnCircle &&
                constraint.geometry_id == geometry_id) {
                result.insert(constraint.first_point_id);
            }
        }
        return result;
    }
    if (const auto arc = std::find_if(
            sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.arcs.end()) {
        std::set<std::string> result{arc->start_point_id, arc->end_point_id};
        for (const auto& constraint : sketch.constraints) {
            if (!constraint.suppressed &&
                constraint.kind == ConstraintKind::PointOnCircle &&
                constraint.geometry_id == geometry_id) {
                result.insert(constraint.first_point_id);
            }
        }
        return result;
    }
    return {};
}

struct TangentCurveData {
    std::string center_point_id;
    double major_x{};
    double major_y{};
    double minor_x{};
    double minor_y{};
    std::optional<std::pair<double, double>> parameter_domain;
    std::optional<double> circular_radius;
};

std::optional<TangentCurveData> tangent_curve_data(
    const Sketch& sketch, const std::string& geometry_id) {
    if (const auto circle = std::find_if(
            sketch.circles.begin(), sketch.circles.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        circle != sketch.circles.end()) {
        TangentCurveData result{
            circle->center_point_id, circle->radius, 0.0, 0.0, circle->radius};
        result.circular_radius = circle->radius;
        return result;
    }
    if (const auto arc = std::find_if(
            sketch.arcs.begin(), sketch.arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.arcs.end()) {
        TangentCurveData result{
            arc->center_point_id, arc->radius, 0.0, 0.0, arc->radius,
            std::pair{arc->start_angle, arc->end_angle}};
        result.circular_radius = arc->radius;
        return result;
    }
    if (const auto ellipse = std::find_if(
            sketch.ellipses.begin(), sketch.ellipses.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        ellipse != sketch.ellipses.end()) {
        const auto* center = sketch.find_point(ellipse->center_point_id);
        const auto* major = sketch.find_point(ellipse->major_point_id);
        const auto* minor = sketch.find_point(ellipse->minor_point_id);
        if (center == nullptr || major == nullptr || minor == nullptr) {
            return std::nullopt;
        }
        return TangentCurveData{
            ellipse->center_point_id,
            major->x - center->x, major->y - center->y,
            minor->x - center->x, minor->y - center->y};
    }
    if (const auto arc = std::find_if(
            sketch.elliptical_arcs.begin(), sketch.elliptical_arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        arc != sketch.elliptical_arcs.end()) {
        const auto* center = sketch.find_point(arc->center_point_id);
        const auto* major = sketch.find_point(arc->major_point_id);
        const auto* minor = sketch.find_point(arc->minor_point_id);
        if (center == nullptr || major == nullptr || minor == nullptr) {
            return std::nullopt;
        }
        return TangentCurveData{
            arc->center_point_id,
            major->x - center->x, major->y - center->y,
            minor->x - center->x, minor->y - center->y,
            std::pair{arc->start_parameter, arc->end_parameter}};
    }
    return std::nullopt;
}

struct CircularConstraintTarget {
    std::array<double, 2> position;
    double residual{};
};

std::vector<std::array<double, 2>> sampled_bspline_points(
    const Sketch& sketch, const SketchBSpline& spline,
    std::size_t samples = 256) {
    const std::size_t count = spline.control_point_ids.size();
    const std::size_t degree = spline.degree;
    std::vector<std::array<double, 2>> result;
    if (count < degree + 1 || samples < 2) return result;
    result.reserve(samples + 1);
    if (spline.interpolating) {
        std::vector<std::array<double, 2>> points;
        points.reserve(count);
        for (const auto& point_id : spline.control_point_ids) {
            const auto* point = sketch.find_point(point_id);
            if (point == nullptr) return {};
            points.push_back({point->x, point->y});
        }
        const std::size_t segment_count = spline.closed ? count : count - 1;
        const auto at = [&](std::ptrdiff_t index) -> const std::array<double, 2>& {
            if (spline.closed) {
                const auto wrapped = (index % static_cast<std::ptrdiff_t>(count) +
                    static_cast<std::ptrdiff_t>(count)) %
                    static_cast<std::ptrdiff_t>(count);
                return points[static_cast<std::size_t>(wrapped)];
            }
            return points[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                index, 0, static_cast<std::ptrdiff_t>(count - 1)))];
        };
        const std::size_t samples_per_segment =
            std::max<std::size_t>(2, samples / segment_count);
        result.reserve(segment_count * samples_per_segment + 1);
        for (std::size_t segment = 0; segment < segment_count; ++segment) {
            const auto& p0 = at(static_cast<std::ptrdiff_t>(segment) - 1);
            const auto& p1 = at(static_cast<std::ptrdiff_t>(segment));
            const auto& p2 = at(static_cast<std::ptrdiff_t>(segment) + 1);
            const auto& p3 = at(static_cast<std::ptrdiff_t>(segment) + 2);
            for (std::size_t sample = 0; sample < samples_per_segment; ++sample) {
                const double u = static_cast<double>(sample) /
                    static_cast<double>(samples_per_segment);
                const double u2 = u * u;
                const double u3 = u2 * u;
                result.push_back({
                    0.5 * (2.0 * p1[0] + (-p0[0] + p2[0]) * u +
                        (2.0 * p0[0] - 5.0 * p1[0] + 4.0 * p2[0] - p3[0]) * u2 +
                        (-p0[0] + 3.0 * p1[0] - 3.0 * p2[0] + p3[0]) * u3),
                    0.5 * (2.0 * p1[1] + (-p0[1] + p2[1]) * u +
                        (2.0 * p0[1] - 5.0 * p1[1] + 4.0 * p2[1] - p3[1]) * u2 +
                        (-p0[1] + 3.0 * p1[1] - 3.0 * p2[1] + p3[1]) * u3)});
            }
        }
        result.push_back(spline.closed ? points.front() : points.back());
        return result;
    }
    if (spline.closed) {
        std::vector<std::array<double, 2>> controls;
        controls.reserve(count + degree);
        for (std::size_t index = 0; index < count + degree; ++index) {
            const auto* point = sketch.find_point(
                spline.control_point_ids[index % count]);
            if (point == nullptr) return {};
            controls.push_back({point->x, point->y});
        }
        for (std::size_t sample = 0; sample <= samples; ++sample) {
            const double parameter = static_cast<double>(degree) +
                static_cast<double>(count) * static_cast<double>(sample) /
                    static_cast<double>(samples);
            const std::size_t span = sample == samples
                ? count + degree - 1
                : static_cast<std::size_t>(std::floor(parameter));
            std::vector<std::array<double, 2>> values(degree + 1);
            for (std::size_t index = 0; index <= degree; ++index) {
                values[index] = controls[span - degree + index];
            }
            for (std::size_t level = 1; level <= degree; ++level) {
                for (std::size_t index = degree; index >= level; --index) {
                    const auto knot_index = span - degree + index;
                    const double weight = (parameter -
                        static_cast<double>(knot_index)) /
                        static_cast<double>(degree - level + 1);
                    values[index][0] = (1.0 - weight) * values[index - 1][0] +
                        weight * values[index][0];
                    values[index][1] = (1.0 - weight) * values[index - 1][1] +
                        weight * values[index][1];
                    if (index == level) break;
                }
            }
            result.push_back(values[degree]);
        }
        result.back() = result.front();
        return result;
    }
    std::vector<double> knots(count + degree + 1, 1.0);
    for (std::size_t index = 0; index <= degree; ++index) knots[index] = 0.0;
    const std::size_t spans = count - degree;
    for (std::size_t index = degree + 1; index < count; ++index) {
        knots[index] = static_cast<double>(index - degree) /
            static_cast<double>(spans);
    }
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double parameter = static_cast<double>(sample) /
            static_cast<double>(samples);
        std::size_t span = count - 1;
        if (parameter < 1.0) {
            for (std::size_t candidate = degree; candidate < count; ++candidate) {
                if (parameter >= knots[candidate] &&
                    parameter < knots[candidate + 1]) {
                    span = candidate;
                    break;
                }
            }
        }
        std::vector<std::array<double, 2>> values(degree + 1);
        for (std::size_t index = 0; index <= degree; ++index) {
            const auto* point = sketch.find_point(
                spline.control_point_ids[span - degree + index]);
            if (point == nullptr) return {};
            values[index] = {point->x, point->y};
        }
        for (std::size_t level = 1; level <= degree; ++level) {
            for (std::size_t index = degree; index >= level; --index) {
                const std::size_t knot_index = span - degree + index;
                const double denominator =
                    knots[knot_index + degree - level + 1] - knots[knot_index];
                const double weight = denominator <= 1.0e-18 ? 0.0
                    : (parameter - knots[knot_index]) / denominator;
                values[index][0] = (1.0 - weight) * values[index - 1][0] +
                    weight * values[index][0];
                values[index][1] = (1.0 - weight) * values[index - 1][1] +
                    weight * values[index][1];
                if (index == level) break;
            }
        }
        result.push_back(values[degree]);
    }
    return result;
}

std::optional<CircularConstraintTarget> sampled_curve_constraint_target(
    const std::vector<std::array<double, 2>>& path,
    double point_x, double point_y) {
    if (path.size() < 2) return std::nullopt;
    std::optional<CircularConstraintTarget> best;
    for (std::size_t index = 1; index < path.size(); ++index) {
        const auto& first = path[index - 1];
        const auto& second = path[index];
        const double dx = second[0] - first[0];
        const double dy = second[1] - first[1];
        const double squared = dx * dx + dy * dy;
        if (squared <= 1.0e-24) continue;
        const double parameter = std::clamp(
            ((point_x - first[0]) * dx + (point_y - first[1]) * dy) /
                squared, 0.0, 1.0);
        const std::array position{
            first[0] + parameter * dx, first[1] + parameter * dy};
        const double residual = std::hypot(
            point_x - position[0], point_y - position[1]);
        if (!best || residual < best->residual) {
            best = CircularConstraintTarget{position, residual};
        }
    }
    return best;
}

std::optional<CircularConstraintTarget> circular_constraint_target(
    const Sketch& sketch, const std::string& geometry_id,
    double point_x, double point_y) {
    const auto curve = tangent_curve_data(sketch, geometry_id);
    if (!curve) {
        const auto spline = std::find_if(
            sketch.bsplines.begin(), sketch.bsplines.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        return spline == sketch.bsplines.end() ? std::nullopt
            : sampled_curve_constraint_target(
                  sampled_bspline_points(sketch, *spline), point_x, point_y);
    }
    const auto* center = sketch.find_point(curve->center_point_id);
    if (center == nullptr) return std::nullopt;
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    const double relative_x = point_x - center->x;
    const double relative_y = point_y - center->y;
    const double determinant =
        curve->major_x * curve->minor_y -
        curve->major_y * curve->minor_x;
    if (std::abs(determinant) <= 1.0e-18) return std::nullopt;
    const double local_cosine =
        (relative_x * curve->minor_y - relative_y * curve->minor_x) /
        determinant;
    const double local_sine =
        (curve->major_x * relative_y - curve->major_y * relative_x) /
        determinant;
    double parameter = std::atan2(local_sine, local_cosine);
    // Newton minimises the true Euclidean point-to-ellipse distance.  Using
    // atan2 in the ellipse basis alone is only a radial projection and is
    // visibly wrong for eccentric or rotated ellipses.
    for (int iteration = 0; iteration < 16; ++iteration) {
        const double cosine = std::cos(parameter);
        const double sine = std::sin(parameter);
        const double curve_x = curve->major_x * cosine + curve->minor_x * sine;
        const double curve_y = curve->major_y * cosine + curve->minor_y * sine;
        const double tangent_x = -curve->major_x * sine + curve->minor_x * cosine;
        const double tangent_y = -curve->major_y * sine + curve->minor_y * cosine;
        const double acceleration_x = -curve_x;
        const double acceleration_y = -curve_y;
        const double error_x = curve_x - relative_x;
        const double error_y = curve_y - relative_y;
        const double function = error_x * tangent_x + error_y * tangent_y;
        const double derivative = tangent_x * tangent_x + tangent_y * tangent_y +
            error_x * acceleration_x + error_y * acceleration_y;
        if (std::abs(derivative) <= 1.0e-18) break;
        const double step = function / derivative;
        parameter -= step;
        if (std::abs(step) <= 1.0e-13) break;
    }
    const auto position_at = [&](double value) {
        return std::array{
            center->x + curve->major_x * std::cos(value) +
                curve->minor_x * std::sin(value),
            center->y + curve->major_y * std::cos(value) +
                curve->minor_y * std::sin(value)};
    };
    if (curve->parameter_domain) {
        const auto [start, end] = *curve->parameter_domain;
        while (parameter < start) parameter += full_turn;
        while (parameter >= start + full_turn) parameter -= full_turn;
        if (parameter > end) {
            const auto start_position = position_at(start);
            const auto end_position = position_at(end);
            const double start_distance = std::hypot(
                point_x - start_position[0], point_y - start_position[1]);
            const double end_distance = std::hypot(
                point_x - end_position[0], point_y - end_position[1]);
            parameter = start_distance <= end_distance ? start : end;
        }
    }
    const auto target = position_at(parameter);
    return CircularConstraintTarget{target,
        std::hypot(point_x - target[0], point_y - target[1])};
}

struct SegmentCurveTangentState {
    std::string first_point_id;
    std::string second_point_id;
    std::string center_point_id;
    double normal_x{};
    double normal_y{};
    double signed_distance{};
    double target_distance{};
    double contact_x{};
    double contact_y{};
    bool contact_on_segment{};
    bool contact_on_curve{};
};

std::optional<SegmentCurveTangentState> segment_curve_tangent_state(
    const Sketch& sketch, const std::string& segment_id,
    const std::string& curve_id) {
    const auto segment = std::find_if(
        sketch.segments.begin(), sketch.segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    const auto axis = segment == sketch.segments.end() &&
            is_base_sketch_axis(segment_id)
        ? sketch_axis_line(sketch, segment_id) : std::nullopt;
    const auto curve = tangent_curve_data(sketch, curve_id);
    if ((segment == sketch.segments.end() && !axis) || !curve)
        return std::nullopt;
    const auto* first = segment == sketch.segments.end()
        ? nullptr : sketch.find_point(segment->first_point_id);
    const auto* second = segment == sketch.segments.end()
        ? nullptr : sketch.find_point(segment->second_point_id);
    const auto* center = sketch.find_point(curve->center_point_id);
    if ((!axis && (first == nullptr || second == nullptr)) || center == nullptr)
        return std::nullopt;
    const double first_x = axis ? axis->first[0] : first->x;
    const double first_y = axis ? axis->first[1] : first->y;
    const double dx = axis ? axis->second[0] : second->x - first->x;
    const double dy = axis ? axis->second[1] : second->y - first->y;
    const double length = std::hypot(dx, dy);
    if (length <= 1.0e-12) return std::nullopt;
    const double tangent_x = dx / length;
    const double tangent_y = dy / length;
    const double normal_x = -tangent_y;
    const double normal_y = tangent_x;
    const double center_x = center->x - first_x;
    const double center_y = center->y - first_y;
    const double signed_distance = center_x * normal_x + center_y * normal_y;
    const double normal_major =
        normal_x * curve->major_x + normal_y * curve->major_y;
    const double normal_minor =
        normal_x * curve->minor_x + normal_y * curve->minor_y;
    const double support = std::hypot(normal_major, normal_minor);
    const double determinant =
        curve->major_x * curve->minor_y - curve->major_y * curve->minor_x;
    if (support <= 1.0e-12 || std::abs(determinant) <= 1.0e-12) {
        return std::nullopt;
    }
    const double side = signed_distance >= 0.0 ? 1.0 : -1.0;
    const double contact_offset_x = -side * (
        normal_major * curve->major_x + normal_minor * curve->minor_x) / support;
    const double contact_offset_y = -side * (
        normal_major * curve->major_y + normal_minor * curve->minor_y) / support;
    const double along =
        (center_x + contact_offset_x) * tangent_x +
        (center_y + contact_offset_y) * tangent_y;
    const double target_distance = signed_distance >= 0.0
        ? support : -support;
    constexpr double linear_tolerance = 1.0e-8;
    const bool contact_on_segment = axis ||
        (along >= -linear_tolerance && along <= length + linear_tolerance);
    bool contact_on_curve = true;
    if (curve->parameter_domain) {
        constexpr double circle_turn = 2.0 * 3.14159265358979323846;
        const double cosine =
            (contact_offset_x * curve->minor_y -
             contact_offset_y * curve->minor_x) / determinant;
        const double sine =
            (curve->major_x * contact_offset_y -
             curve->major_y * contact_offset_x) / determinant;
        const double parameter = std::atan2(sine, cosine);
        double relative = std::fmod(
            parameter - curve->parameter_domain->first, circle_turn);
        if (relative < 0.0) relative += circle_turn;
        constexpr double angular_tolerance = 1.0e-8;
        contact_on_curve = relative <=
                curve->parameter_domain->second -
                    curve->parameter_domain->first + angular_tolerance ||
            // A contact infinitesimally below the Arc start wraps to almost
            // 2*pi. It is the same persisted boundary point, not a contact on
            // the removed side of the source circle.
            relative >= circle_turn - angular_tolerance;
    }
    return SegmentCurveTangentState{
        axis ? std::string{} : segment->first_point_id,
        axis ? std::string{} : segment->second_point_id,
        curve->center_point_id, normal_x, normal_y,
        signed_distance, target_distance,
        center->x + contact_offset_x, center->y + contact_offset_y,
        contact_on_segment, contact_on_curve};
}

std::optional<double> segment_curve_endpoint_tangent_residual(
    const Sketch& sketch, const std::string& segment_id,
    const std::string& curve_id) {
    const auto segment = std::ranges::find_if(
        sketch.segments, [&](const auto& value) {
            return value.id == segment_id;
        });
    if (segment == sketch.segments.end()) return std::nullopt;
    std::string shared_id;
    const auto offer_shared = [&](const std::string& id) {
        if (id == segment->first_point_id || id == segment->second_point_id)
            shared_id = id;
    };
    if (const auto arc = std::ranges::find_if(sketch.arcs,
            [&](const auto& value) { return value.id == curve_id; });
        arc != sketch.arcs.end()) {
        offer_shared(arc->start_point_id);
        offer_shared(arc->end_point_id);
    }
    if (const auto arc = std::ranges::find_if(sketch.elliptical_arcs,
            [&](const auto& value) { return value.id == curve_id; });
        arc != sketch.elliptical_arcs.end()) {
        offer_shared(arc->start_point_id);
        offer_shared(arc->end_point_id);
    }
    if (shared_id.empty()) return std::nullopt;
    const auto* contact = sketch.find_point(shared_id);
    const auto* other = sketch.find_point(
        segment->first_point_id == shared_id
            ? segment->second_point_id : segment->first_point_id);
    if (contact == nullptr || other == nullptr) return std::nullopt;
    const auto tangent = sketch.curve_tangent_at_point(
        curve_id, contact->x, contact->y);
    const double length = std::hypot(
        other->x - contact->x, other->y - contact->y);
    if (!tangent || length <= 1.0e-12) return std::nullopt;
    const double segment_x = (other->x - contact->x) / length;
    const double segment_y = (other->y - contact->y) / length;
    return segment_x * (*tangent)[1] - segment_y * (*tangent)[0];
}

struct SegmentSplineTangentState {
    std::string contact_point_id;
    std::string other_point_id;
    std::string spline_tangent_point_id;
    double tangent_x{};
    double tangent_y{};
    double segment_length{};
    double residual{};
};

std::optional<SegmentSplineTangentState> segment_spline_tangent_state(
    const Sketch& sketch, const std::string& segment_id,
    const std::string& spline_id) {
    const auto segment = std::find_if(
        sketch.segments.begin(), sketch.segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    const auto spline = std::find_if(
        sketch.bsplines.begin(), sketch.bsplines.end(),
        [&](const auto& value) { return value.id == spline_id; });
    if (segment == sketch.segments.end() || spline == sketch.bsplines.end())
        return std::nullopt;
    const auto path = sampled_bspline_points(sketch, *spline);
    if (path.size() < 2) return std::nullopt;
    const auto* first = sketch.find_point(segment->first_point_id);
    const auto* second = sketch.find_point(segment->second_point_id);
    if (first == nullptr || second == nullptr) return std::nullopt;
    const double segment_length = std::hypot(
        second->x - first->x, second->y - first->y);
    if (segment_length <= 1.0e-12) return std::nullopt;
    if (spline->control_point_ids.size() >= 2) {
        for (const auto [line_point, line_other] : {
                std::pair{first, second}, std::pair{second, first}}) {
            for (const auto [endpoint_index, handle_index] : {
                    std::pair<std::size_t, std::size_t>{0, 1},
                    {spline->control_point_ids.size() - 1,
                     spline->control_point_ids.size() - 2}}) {
                const auto* endpoint = sketch.find_point(
                    spline->control_point_ids[endpoint_index]);
                const auto* handle = sketch.find_point(
                    spline->control_point_ids[handle_index]);
                if (endpoint == nullptr || handle == nullptr || std::hypot(
                        line_point->x - endpoint->x,
                        line_point->y - endpoint->y) > 1.0e-6) {
                    continue;
                }
                const double handle_length = std::hypot(
                    handle->x - endpoint->x, handle->y - endpoint->y);
                if (handle_length <= 1.0e-12) continue;
                const double tangent_x =
                    (handle->x - endpoint->x) / handle_length;
                const double tangent_y =
                    (handle->y - endpoint->y) / handle_length;
                const double segment_x =
                    (line_other->x - line_point->x) / segment_length;
                const double segment_y =
                    (line_other->y - line_point->y) / segment_length;
                return SegmentSplineTangentState{
                    line_point->id, line_other->id, handle->id,
                    tangent_x, tangent_y, segment_length,
                    std::abs(segment_x * tangent_y -
                        segment_y * tangent_x)};
            }
        }
    }
    struct Contact {
        const SketchPoint* point{};
        const SketchPoint* other{};
        double distance{std::numeric_limits<double>::infinity()};
        double tangent_x{};
        double tangent_y{};
    };
    Contact best;
    for (const auto [point, other] : {
            std::pair{first, second}, std::pair{second, first}}) {
        for (std::size_t index = 1; index < path.size(); ++index) {
            const double dx = path[index][0] - path[index - 1][0];
            const double dy = path[index][1] - path[index - 1][1];
            const double squared = dx * dx + dy * dy;
            if (squared <= 1.0e-24) continue;
            const double parameter = std::clamp(
                ((point->x - path[index - 1][0]) * dx +
                 (point->y - path[index - 1][1]) * dy) / squared,
                0.0, 1.0);
            const std::array projected{
                path[index - 1][0] + parameter * dx,
                path[index - 1][1] + parameter * dy};
            const double distance = std::hypot(
                point->x - projected[0], point->y - projected[1]);
            if (distance < best.distance) {
                const double length = std::sqrt(squared);
                best = {point, other, distance, dx / length, dy / length};
            }
        }
    }
    if (best.point == nullptr || best.distance > 1.0e-6) return std::nullopt;
    std::string spline_tangent_point_id;
    if (spline->control_point_ids.size() >= 2) {
        const auto* spline_first = sketch.find_point(
            spline->control_point_ids.front());
        const auto* spline_last = sketch.find_point(
            spline->control_point_ids.back());
        if (spline_first != nullptr && std::hypot(
                best.point->x - spline_first->x,
                best.point->y - spline_first->y) <= 1.0e-6) {
            spline_tangent_point_id = spline->control_point_ids[1];
        } else if (spline_last != nullptr && std::hypot(
                best.point->x - spline_last->x,
                best.point->y - spline_last->y) <= 1.0e-6) {
            spline_tangent_point_id =
                spline->control_point_ids[spline->control_point_ids.size() - 2];
        }
    }
    const double segment_x = (best.other->x - best.point->x) / segment_length;
    const double segment_y = (best.other->y - best.point->y) / segment_length;
    return SegmentSplineTangentState{
        best.point->id, best.other->id, std::move(spline_tangent_point_id),
        best.tangent_x, best.tangent_y,
        segment_length,
        std::abs(segment_x * best.tangent_y - segment_y * best.tangent_x)};
}

bool parameter_in_curve_domain(
    const TangentCurveData& curve, double parameter) {
    if (!curve.parameter_domain) return true;
    constexpr double circle_turn = 2.0 * 3.14159265358979323846;
    double relative = std::fmod(
        parameter - curve.parameter_domain->first, circle_turn);
    if (relative < 0.0) relative += circle_turn;
    return relative <=
        curve.parameter_domain->second - curve.parameter_domain->first + 1.0e-8;
}

struct CurvePairTangentState {
    std::string reference_center_point_id;
    std::string driven_center_point_id;
    double direction_x{};
    double direction_y{};
    double center_distance{};
    double target_distance{};
    bool contact_on_reference{};
    bool contact_on_driven{};
};

struct GeneralCurvePairTangentState {
    std::set<std::string> driven_point_ids;
    double correction_x{};
    double correction_y{};
    double distance{};
    bool tangents_parallel{};
};

std::array<double, 2> tangent_curve_point(
    const Sketch& sketch, const TangentCurveData& curve, double parameter) {
    const auto* center = sketch.find_point(curve.center_point_id);
    return {
        center->x + curve.major_x * std::cos(parameter) +
            curve.minor_x * std::sin(parameter),
        center->y + curve.major_y * std::cos(parameter) +
            curve.minor_y * std::sin(parameter)};
}

std::array<double, 2> tangent_curve_derivative(
    const TangentCurveData& curve, double parameter) {
    return {
        -curve.major_x * std::sin(parameter) +
            curve.minor_x * std::cos(parameter),
        -curve.major_y * std::sin(parameter) +
            curve.minor_y * std::cos(parameter)};
}

std::pair<double, double> tangent_curve_domain(
    const TangentCurveData& curve) {
    return curve.parameter_domain.value_or(
        std::pair{0.0, 2.0 * 3.14159265358979323846});
}

std::optional<GeneralCurvePairTangentState> general_curve_pair_tangent_state(
    const Sketch& sketch, const std::string& reference_geometry_id,
    const std::string& driven_geometry_id) {
    const auto reference = tangent_curve_data(sketch, reference_geometry_id);
    const auto driven = tangent_curve_data(sketch, driven_geometry_id);
    if (!reference || !driven) return std::nullopt;
    const auto reference_domain = tangent_curve_domain(*reference);
    const auto driven_domain = tangent_curve_domain(*driven);
    if (reference_domain.second - reference_domain.first <= 1.0e-12 ||
        driven_domain.second - driven_domain.first <= 1.0e-12) {
        return std::nullopt;
    }
    constexpr std::size_t samples = 96;
    double reference_parameter = reference_domain.first;
    double driven_parameter = driven_domain.first;
    double best_squared = std::numeric_limits<double>::infinity();
    for (std::size_t first_index = 0; first_index <= samples; ++first_index) {
        const double first_parameter = reference_domain.first +
            (reference_domain.second - reference_domain.first) *
                static_cast<double>(first_index) / samples;
        const auto first_point = tangent_curve_point(
            sketch, *reference, first_parameter);
        for (std::size_t second_index = 0; second_index <= samples; ++second_index) {
            const double second_parameter = driven_domain.first +
                (driven_domain.second - driven_domain.first) *
                    static_cast<double>(second_index) / samples;
            const auto second_point = tangent_curve_point(
                sketch, *driven, second_parameter);
            const double dx = first_point[0] - second_point[0];
            const double dy = first_point[1] - second_point[1];
            const double squared = dx * dx + dy * dy;
            if (squared < best_squared) {
                best_squared = squared;
                reference_parameter = first_parameter;
                driven_parameter = second_parameter;
            }
        }
    }
    // Refine the sampled closest pair by minimizing the squared separation
    // in both curve parameters. Numerical derivatives keep this common for
    // circles, arcs, ellipses and elliptical arcs without consulting OCCT.
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
        const auto first_point = tangent_curve_point(
            sketch, *reference, reference_parameter);
        const auto second_point = tangent_curve_point(
            sketch, *driven, driven_parameter);
        const auto first_tangent = tangent_curve_derivative(
            *reference, reference_parameter);
        const auto second_tangent = tangent_curve_derivative(
            *driven, driven_parameter);
        const std::array difference{
            first_point[0] - second_point[0],
            first_point[1] - second_point[1]};
        const std::array residual{
            difference[0] * first_tangent[0] +
                difference[1] * first_tangent[1],
            difference[0] * second_tangent[0] +
                difference[1] * second_tangent[1]};
        if (std::hypot(residual[0], residual[1]) <= 1.0e-12) break;
        const double first_step = std::max(
            1.0e-7, (reference_domain.second - reference_domain.first) * 1.0e-6);
        const double second_step = std::max(
            1.0e-7, (driven_domain.second - driven_domain.first) * 1.0e-6);
        const auto evaluated_residual = [&](double first_parameter,
                                             double second_parameter) {
            const auto a = tangent_curve_point(
                sketch, *reference, first_parameter);
            const auto b = tangent_curve_point(
                sketch, *driven, second_parameter);
            const auto ta = tangent_curve_derivative(*reference, first_parameter);
            const auto tb = tangent_curve_derivative(*driven, second_parameter);
            const std::array delta{a[0] - b[0], a[1] - b[1]};
            return std::array{
                delta[0] * ta[0] + delta[1] * ta[1],
                delta[0] * tb[0] + delta[1] * tb[1]};
        };
        const auto first_shift = evaluated_residual(
            std::clamp(reference_parameter + first_step,
                reference_domain.first, reference_domain.second),
            driven_parameter);
        const auto second_shift = evaluated_residual(
            reference_parameter,
            std::clamp(driven_parameter + second_step,
                driven_domain.first, driven_domain.second));
        const double a = (first_shift[0] - residual[0]) / first_step;
        const double c = (first_shift[1] - residual[1]) / first_step;
        const double b = (second_shift[0] - residual[0]) / second_step;
        const double d = (second_shift[1] - residual[1]) / second_step;
        const double determinant = a * d - b * c;
        if (std::abs(determinant) <= 1.0e-15) break;
        const double delta_first = std::clamp(
            (-residual[0] * d + b * residual[1]) / determinant,
            -0.25, 0.25);
        const double delta_second = std::clamp(
            (-a * residual[1] + c * residual[0]) / determinant,
            -0.25, 0.25);
        reference_parameter = std::clamp(
            reference_parameter + delta_first,
            reference_domain.first, reference_domain.second);
        driven_parameter = std::clamp(
            driven_parameter + delta_second,
            driven_domain.first, driven_domain.second);
    }
    const auto reference_point = tangent_curve_point(
        sketch, *reference, reference_parameter);
    const auto driven_point = tangent_curve_point(
        sketch, *driven, driven_parameter);
    const auto reference_tangent = tangent_curve_derivative(
        *reference, reference_parameter);
    const auto driven_tangent = tangent_curve_derivative(
        *driven, driven_parameter);
    const double reference_length = std::hypot(
        reference_tangent[0], reference_tangent[1]);
    const double driven_length = std::hypot(
        driven_tangent[0], driven_tangent[1]);
    if (reference_length <= 1.0e-12 || driven_length <= 1.0e-12) {
        return std::nullopt;
    }
    const double tangent_cross = std::abs(
        reference_tangent[0] * driven_tangent[1] -
        reference_tangent[1] * driven_tangent[0]) /
        (reference_length * driven_length);
    return GeneralCurvePairTangentState{
        center_curve_translation_points(sketch, driven_geometry_id),
        reference_point[0] - driven_point[0],
        reference_point[1] - driven_point[1],
        std::hypot(reference_point[0] - driven_point[0],
                   reference_point[1] - driven_point[1]),
        tangent_cross <= 1.0e-5};
}

std::optional<CurvePairTangentState> curve_pair_tangent_state(
    const Sketch& sketch, const std::string& reference_geometry_id,
    const std::string& driven_geometry_id, bool internal) {
    const auto reference = tangent_curve_data(sketch, reference_geometry_id);
    const auto driven = tangent_curve_data(sketch, driven_geometry_id);
    if (!reference || !driven || !reference->circular_radius ||
        !driven->circular_radius) {
        return std::nullopt;
    }
    const auto* reference_center = sketch.find_point(reference->center_point_id);
    const auto* driven_center = sketch.find_point(driven->center_point_id);
    if (reference_center == nullptr || driven_center == nullptr) return std::nullopt;
    const double dx = driven_center->x - reference_center->x;
    const double dy = driven_center->y - reference_center->y;
    const double distance = std::hypot(dx, dy);
    if (distance <= 1.0e-12) return std::nullopt;
    const double reference_radius = *reference->circular_radius;
    const double driven_radius = *driven->circular_radius;
    const double target_distance = internal
        ? std::abs(reference_radius - driven_radius)
        : reference_radius + driven_radius;
    if (target_distance <= 1.0e-12) return std::nullopt;
    const double direction_x = dx / distance;
    const double direction_y = dy / distance;
    double reference_contact_sign = 1.0;
    double driven_contact_sign = -1.0;
    if (internal) {
        reference_contact_sign = reference_radius > driven_radius ? 1.0 : -1.0;
        driven_contact_sign = reference_contact_sign;
    }
    const bool contact_on_reference = parameter_in_curve_domain(
        *reference, std::atan2(
            reference_contact_sign * direction_y,
            reference_contact_sign * direction_x));
    const bool contact_on_driven = parameter_in_curve_domain(
        *driven, std::atan2(
            driven_contact_sign * direction_y,
            driven_contact_sign * direction_x));
    return CurvePairTangentState{
        reference->center_point_id, driven->center_point_id,
        direction_x, direction_y, distance, target_distance,
        contact_on_reference, contact_on_driven};
}

std::array<double, 2> reflected_position(
    const std::array<double, 2>& position,
    const std::array<double, 2>& axis_origin,
    const std::array<double, 2>& axis_direction) {
    const double length_squared =
        axis_direction[0] * axis_direction[0] +
        axis_direction[1] * axis_direction[1];
    if (length_squared <= 1.0e-24) {
        throw std::invalid_argument("Sketch mirror axis has zero length");
    }
    const double parameter =
        ((position[0] - axis_origin[0]) * axis_direction[0] +
         (position[1] - axis_origin[1]) * axis_direction[1]) /
        length_squared;
    const double projection_x = axis_origin[0] + parameter * axis_direction[0];
    const double projection_y = axis_origin[1] + parameter * axis_direction[1];
    return {2.0 * projection_x - position[0],
            2.0 * projection_y - position[1]};
}

constexpr double pi = 3.14159265358979323846;
constexpr double full_turn = 2.0 * pi;

std::array<double, 2> ellipse_position(
    double center_x, double center_y, double major_radius,
    double minor_radius, double rotation, bool reversed,
    double parameter) {
    const double orientation = reversed ? -1.0 : 1.0;
    const double local_x = major_radius * std::cos(parameter);
    const double local_y = orientation * minor_radius * std::sin(parameter);
    return {
        center_x + local_x * std::cos(rotation) - local_y * std::sin(rotation),
        center_y + local_x * std::sin(rotation) + local_y * std::cos(rotation)};
}

double ellipse_parameter(
    double center_x, double center_y, double major_radius,
    double minor_radius, double rotation, bool reversed,
    double x, double y) {
    const double dx = x - center_x;
    const double dy = y - center_y;
    const double local_x =
        (dx * std::cos(rotation) + dy * std::sin(rotation)) / major_radius;
    const double local_y =
        (-dx * std::sin(rotation) + dy * std::cos(rotation)) / minor_radius;
    const double orientation = reversed ? -1.0 : 1.0;
    return std::atan2(orientation * local_y, local_x);
}

double unwrap_near(double value, double reference) {
    while (value - reference > pi) value -= full_turn;
    while (value - reference < -pi) value += full_turn;
    return value;
}

}  // namespace

Sketch Sketch::create_default() {
    Sketch sketch;
    sketch.id = make_id();
    sketch.refresh_default_frame();
    return sketch;
}

void Sketch::refresh_default_frame() {
    const auto frame = default_sketch_frame(plane, plane_offset);
    resolved_origin = frame.origin;
    resolved_x_axis = frame.x_axis;
    resolved_y_axis = frame.y_axis;
    resolved_normal = frame.normal;
}

SketchPoint Sketch::create_point(double x, double y) {
    require_finite(x, "point x");
    require_finite(y, "point y");
    return {make_id(), x, y};
}

SketchSegment Sketch::create_segment(
    std::string first_point_id, std::string second_point_id, bool construction) {
    if (first_point_id.empty() || second_point_id.empty() ||
        first_point_id == second_point_id) {
        throw std::invalid_argument("Sketch segment endpoints are invalid");
    }
    return {make_id(), std::move(first_point_id), std::move(second_point_id), construction};
}

SketchPoint* Sketch::find_point(const std::string& point_id) {
    if (point_lookup_active_) {
        const auto indexed = point_lookup_indices_.find(point_id);
        if (indexed == point_lookup_indices_.end()) return nullptr;
        if (indexed->second < points.size() &&
            points[indexed->second].id == point_id) {
            return &points[indexed->second];
        }
        // A caller modified the public point vector while an indexed solver
        // transaction was active. Fall back safely rather than dereferencing
        // a stale position; normal solver code never takes this path.
    }
    const auto found = std::find_if(points.begin(), points.end(),
        [&](const auto& point) { return point.id == point_id; });
    return found == points.end() ? nullptr : &*found;
}

const SketchPoint* Sketch::find_point(const std::string& point_id) const {
    return const_cast<Sketch*>(this)->find_point(point_id);
}

void Sketch::validate() const {
    const bool owns_point_lookup = !point_lookup_active_;
    if (owns_point_lookup) {
        point_lookup_indices_.clear();
        point_lookup_indices_.reserve(points.size());
        for (std::size_t index = 0; index < points.size(); ++index) {
            point_lookup_indices_.emplace(points[index].id, index);
        }
        point_lookup_active_ = true;
    }
    struct ValidationLookupGuard {
        bool owns;
        bool* active;
        std::unordered_map<std::string, std::size_t>* indices;
        ~ValidationLookupGuard() {
            if (!owns) return;
            *active = false;
            indices->clear();
        }
    } validation_lookup_guard{owns_point_lookup, &point_lookup_active_,
                              &point_lookup_indices_};
    if (id.empty() || name.empty()) throw std::runtime_error("Sketch identity is invalid");
    require_finite(plane_offset, "plane offset");
    std::unordered_set<std::string> ids;
    for (const auto& point : points) {
        if (point.id.empty() || !ids.insert(point.id).second) {
            throw std::runtime_error("Sketch point IDs must be non-empty and unique");
        }
        require_finite(point.x, "point x");
        require_finite(point.y, "point y");
    }
    ids.clear();
    for (const auto& segment : segments) {
        if (segment.id.empty())
            throw std::runtime_error("Sketch segment has an empty ID");
        if (!ids.insert(segment.id).second)
            throw std::runtime_error("Sketch segment has a duplicate ID: " + segment.id);
        if (segment.first_point_id == segment.second_point_id)
            throw std::runtime_error(
                "Sketch segment has identical endpoints: " + segment.id);
        if (find_point(segment.first_point_id) == nullptr)
            throw std::runtime_error("Sketch segment " + segment.id +
                " references missing first point " + segment.first_point_id);
        if (find_point(segment.second_point_id) == nullptr)
            throw std::runtime_error("Sketch segment " + segment.id +
                " references missing second point " + segment.second_point_id);
    }
    std::unordered_map<std::string, const SketchSegment*> segments_by_id;
    segments_by_id.reserve(segments.size());
    for (const auto& segment : segments) {
        segments_by_id.emplace(segment.id, &segment);
    }
    const auto segment_by_id = [&](const std::string& segment_id)
        -> const SketchSegment* {
        const auto found = segments_by_id.find(segment_id);
        return found == segments_by_id.end() ? nullptr : found->second;
    };
    ids.clear();
    for (const auto& radius : corner_radii) {
        const auto first = std::find_if(segments.begin(), segments.end(),
            [&](const auto& value) { return value.id == radius.first_segment_id; });
        const auto second = std::find_if(segments.begin(), segments.end(),
            [&](const auto& value) { return value.id == radius.second_segment_id; });
        const bool first_owns_vertex = first != segments.end() &&
            (first->first_point_id == radius.vertex_id ||
             first->second_point_id == radius.vertex_id);
        const bool second_owns_vertex = second != segments.end() &&
            (second->first_point_id == radius.vertex_id ||
             second->second_point_id == radius.vertex_id);
        if (radius.id.empty() || !ids.insert(radius.id).second ||
            radius.vertex_id.empty() || find_point(radius.vertex_id) == nullptr ||
            first == segments.end() || second == segments.end() ||
            first == second || !first_owns_vertex || !second_owns_vertex ||
            !std::isfinite(radius.radius) || radius.radius < 0.0) {
            throw std::runtime_error("Sketch corner radius is invalid");
        }
        if (radius.dimension_placement) {
            require_finite((*radius.dimension_placement)[0],
                "corner radius dimension placement x");
            require_finite((*radius.dimension_placement)[1],
                "corner radius dimension placement y");
        }
    }
    ids.clear();
    for (const auto& circle : circles) {
        if (circle.id.empty() || !ids.insert(circle.id).second ||
            find_point(circle.center_point_id) == nullptr ||
            !std::isfinite(circle.radius) || circle.radius <= 0.0) {
            throw std::runtime_error("Sketch circle is invalid");
        }
    }
    ids.clear();
    for (const auto& arc : arcs) {
        const auto* center = find_point(arc.center_point_id);
        const auto* start = find_point(arc.start_point_id);
        const auto* end = find_point(arc.end_point_id);
        if (arc.id.empty() || !ids.insert(arc.id).second ||
            center == nullptr || start == nullptr || end == nullptr ||
            arc.center_point_id == arc.start_point_id ||
            arc.center_point_id == arc.end_point_id ||
            arc.start_point_id == arc.end_point_id ||
            !std::isfinite(arc.radius) || arc.radius <= 0.0 ||
            !std::isfinite(arc.start_angle) || !std::isfinite(arc.end_angle) ||
            arc.end_angle <= arc.start_angle ||
            arc.end_angle - arc.start_angle >= 2.0 * 3.14159265358979323846) {
            throw std::runtime_error("Sketch arc is invalid");
        }
        const auto point_matches = [&](const SketchPoint* point, double angle) {
            return std::hypot(
                point->x - (center->x + arc.radius * std::cos(angle)),
                point->y - (center->y + arc.radius * std::sin(angle))) <= 1.0e-7;
        };
        if (!point_matches(start, arc.start_angle) ||
            !point_matches(end, arc.end_angle)) {
            throw std::runtime_error("Sketch Arc endpoint references are inconsistent");
        }
    }
    ids.clear();
    for (const auto& ellipse : ellipses) {
        const auto* center = find_point(ellipse.center_point_id);
        const auto* major = find_point(ellipse.major_point_id);
        const auto* minor = find_point(ellipse.minor_point_id);
        const double orientation = ellipse.reversed ? -1.0 : 1.0;
        if (ellipse.id.empty() || !ids.insert(ellipse.id).second ||
            center == nullptr || major == nullptr || minor == nullptr ||
            ellipse.center_point_id == ellipse.major_point_id ||
            ellipse.center_point_id == ellipse.minor_point_id ||
            ellipse.major_point_id == ellipse.minor_point_id ||
            !std::isfinite(ellipse.major_radius) || ellipse.major_radius <= 0.0 ||
            !std::isfinite(ellipse.minor_radius) || ellipse.minor_radius <= 0.0 ||
            !std::isfinite(ellipse.rotation)) {
            throw std::runtime_error("Sketch ellipse is invalid");
        }
        if (std::hypot(
                major->x - (center->x + ellipse.major_radius * std::cos(ellipse.rotation)),
                major->y - (center->y + ellipse.major_radius * std::sin(ellipse.rotation))) >
                1.0e-7 ||
            std::hypot(
                minor->x - (center->x - orientation * ellipse.minor_radius *
                    std::sin(ellipse.rotation)),
                minor->y - (center->y + orientation * ellipse.minor_radius *
                    std::cos(ellipse.rotation))) >
                1.0e-7) {
            throw std::runtime_error("Sketch ellipse axis references are inconsistent");
        }
    }
    ids.clear();
    for (const auto& arc : elliptical_arcs) {
        const auto* center = find_point(arc.center_point_id);
        const auto* major = find_point(arc.major_point_id);
        const auto* minor = find_point(arc.minor_point_id);
        const auto* start = find_point(arc.start_point_id);
        const auto* end = find_point(arc.end_point_id);
        const double orientation = arc.reversed ? -1.0 : 1.0;
        if (arc.id.empty() || !ids.insert(arc.id).second ||
            center == nullptr || major == nullptr || minor == nullptr ||
            start == nullptr || end == nullptr ||
            arc.center_point_id == arc.major_point_id ||
            arc.center_point_id == arc.minor_point_id ||
            arc.center_point_id == arc.start_point_id ||
            arc.center_point_id == arc.end_point_id ||
            arc.major_point_id == arc.minor_point_id ||
            arc.start_point_id == arc.end_point_id ||
            !std::isfinite(arc.major_radius) || arc.major_radius <= 0.0 ||
            !std::isfinite(arc.minor_radius) || arc.minor_radius <= 0.0 ||
            !std::isfinite(arc.rotation) ||
            !std::isfinite(arc.start_parameter) ||
            !std::isfinite(arc.end_parameter) ||
            arc.end_parameter <= arc.start_parameter ||
            arc.end_parameter - arc.start_parameter >= full_turn) {
            throw std::runtime_error("Sketch elliptical arc is invalid");
        }
        const auto start_position = ellipse_position(
            center->x, center->y, arc.major_radius, arc.minor_radius,
            arc.rotation, arc.reversed, arc.start_parameter);
        const auto end_position = ellipse_position(
            center->x, center->y, arc.major_radius, arc.minor_radius,
            arc.rotation, arc.reversed, arc.end_parameter);
        if (std::hypot(
                major->x - (center->x + arc.major_radius * std::cos(arc.rotation)),
                major->y - (center->y + arc.major_radius * std::sin(arc.rotation))) >
                1.0e-7 ||
            std::hypot(
                minor->x - (center->x - orientation * arc.minor_radius *
                    std::sin(arc.rotation)),
                minor->y - (center->y + orientation * arc.minor_radius *
                    std::cos(arc.rotation))) > 1.0e-7 ||
            std::hypot(start->x - start_position[0], start->y - start_position[1]) >
                1.0e-7 ||
            std::hypot(end->x - end_position[0], end->y - end_position[1]) >
                1.0e-7) {
            throw std::runtime_error(
                "Sketch elliptical arc references are inconsistent");
        }
    }
    ids.clear();
    for (const auto& spline : bsplines) {
        if (spline.id.empty() || !ids.insert(spline.id).second || spline.degree < 1 ||
            spline.control_point_ids.size() <
                static_cast<std::size_t>(spline.degree) + 1) {
            throw std::runtime_error("Sketch B-spline is invalid");
        }
        for (std::size_t index = 0; index < spline.control_point_ids.size(); ++index) {
            if (find_point(spline.control_point_ids[index]) == nullptr ||
                (index > 0 && spline.control_point_ids[index] ==
                    spline.control_point_ids[index - 1])) {
                throw std::runtime_error("Sketch B-spline control points are invalid");
            }
        }
    }
    ids.clear();
    for (const auto& text : texts) {
        if (text.id.empty() || !ids.insert(text.id).second || text.value.empty() ||
            text.font.empty() || !std::isfinite(text.anchor_x) ||
            !std::isfinite(text.anchor_y) || !std::isfinite(text.height) ||
            text.height <= 0.0 || !std::isfinite(text.angle_degrees) ||
            text.contours.empty()) {
            throw std::runtime_error("Sketch text is invalid");
        }
        static_cast<void>(text_horizontal_name(text.horizontal));
        static_cast<void>(text_vertical_name(text.vertical));
        static_cast<void>(text_color_name(text.color));
        for (const auto& contour : text.contours) {
            if (contour.size() < 3) {
                throw std::runtime_error("Sketch text contour is invalid");
            }
            double signed_area{};
            for (std::size_t index = 0; index < contour.size(); ++index) {
                const auto& point = contour[index];
                const auto& next = contour[(index + 1) % contour.size()];
                if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
                    std::hypot(next[0] - point[0], next[1] - point[1]) <= 1.0e-12) {
                    throw std::runtime_error("Sketch text contour is invalid");
                }
                signed_area += point[0] * next[1] - next[0] * point[1];
            }
            if (std::abs(signed_area) <= 1.0e-12) {
                throw std::runtime_error("Sketch text contour has zero area");
            }
        }
    }
    ids.clear();
    std::vector<std::tuple<ExternalReferenceKind, std::string, std::string,
                           std::string, std::string, std::string, std::string>>
        external_sources;
    for (const auto& reference : external_references) {
        static_cast<void>(external_reference_kind_name(reference.kind));
        if (reference.id.empty() || !ids.insert(reference.id).second ||
            reference.source_document_id.empty() ||
            reference.source_owner_id.empty() ||
            reference.source_semantic_key.empty() ||
            reference.source_owner_id == id ||
            (reference.source_instance_path.empty() !=
                reference.context_instance_path.empty()) ||
            (reference.context_instance_path.empty() !=
                reference.context_assembly_document_id.empty())) {
            throw std::runtime_error("Sketch external reference is invalid");
        }
        const auto source = std::tuple{
            reference.kind, reference.source_document_id,
            reference.source_owner_id, reference.source_semantic_key,
            reference.source_instance_path,
            reference.context_assembly_document_id,
            reference.context_instance_path};
        if (std::ranges::find(external_sources, source) !=
            external_sources.end()) {
            throw std::runtime_error("Sketch external reference is duplicated");
        }
        external_sources.push_back(source);
        if ((reference.kind == ExternalReferenceKind::Point &&
             (reference.cached_points.size() != 1 || !reference.cached_paths.empty())) ||
            ((reference.kind == ExternalReferenceKind::Edge ||
              reference.kind == ExternalReferenceKind::Axis) &&
             (reference.cached_points.size() < 2 || !reference.cached_paths.empty())) ||
            (reference.kind == ExternalReferenceKind::Face &&
             (reference.cached_points.size() != 2 ||
              !reference.cached_paths.empty() || !reference.infinite))) {
            throw std::runtime_error("Sketch external reference geometry is invalid");
        }
        for (std::size_t index = 0; index < reference.cached_points.size(); ++index) {
            const auto& point = reference.cached_points[index];
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
                (index > 0 && std::hypot(
                    point[0] - reference.cached_points[index - 1][0],
                    point[1] - reference.cached_points[index - 1][1]) <= 1.0e-12)) {
                throw std::runtime_error(
                    "Sketch external reference geometry is invalid");
            }
        }
        for (const auto& path : reference.cached_paths) {
            if (path.size() < 4 || path.front() != path.back()) {
                throw std::runtime_error("Sketch external face path is invalid");
            }
            double signed_area{};
            for (std::size_t index = 0; index < path.size(); ++index) {
                const auto& point = path[index];
                if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
                    (index > 0 && std::hypot(
                        point[0] - path[index - 1][0],
                        point[1] - path[index - 1][1]) <= 1.0e-12)) {
                    throw std::runtime_error("Sketch external face path is invalid");
                }
                if (index > 0) {
                    signed_area += path[index - 1][0] * point[1] -
                        point[0] * path[index - 1][1];
                }
            }
            if (std::abs(signed_area) <= 1.0e-12) {
                throw std::runtime_error("Sketch external face path has zero area");
            }
        }
    }
    ids.clear();
    for (const auto& constraint : constraints) {
        const auto* owned_segment = segment_by_id(constraint.geometry_id);
        const auto* second_owned_segment =
            segment_by_id(constraint.second_geometry_id);
        const auto owned_point_curve = tangent_curve_data(
            *this, constraint.geometry_id);
        const auto owned_point_spline = std::find_if(
            bsplines.begin(), bsplines.end(), [&](const auto& spline) {
                return spline.id == constraint.geometry_id;
            });
        const bool pair_constraint = is_segment_pair_constraint(constraint.kind);
        const bool segment_constraint = constraint.kind == ConstraintKind::Horizontal ||
            constraint.kind == ConstraintKind::Vertical;
        const bool point_on_circle =
            constraint.kind == ConstraintKind::PointOnCircle;
        const bool point_on_line = constraint.kind == ConstraintKind::PointOnLine;
        const bool midpoint_on_line =
            constraint.kind == ConstraintKind::MidpointOnLine;
        const auto* constrained_native_point =
            find_point(constraint.first_point_id);
        const bool symmetric = constraint.kind == ConstraintKind::Symmetric;
        const bool midpoint = constraint.kind == ConstraintKind::Midpoint;
        const bool concentric = constraint.kind == ConstraintKind::Concentric;
        const bool tangent = constraint.kind == ConstraintKind::Tangent;
        const bool equal_radius = constraint.kind == ConstraintKind::EqualRadius;
        const auto first_curve_center = concentric
            ? center_curve_point_id(*this, constraint.geometry_id) : std::nullopt;
        const auto second_curve_center = concentric
            ? center_curve_point_id(*this, constraint.second_geometry_id) : std::nullopt;
        const auto first_tangent_curve = tangent
            ? tangent_curve_data(*this, constraint.geometry_id) : std::nullopt;
        const auto second_tangent_curve = tangent
            ? tangent_curve_data(*this, constraint.second_geometry_id) : std::nullopt;
        const bool first_tangent_spline = tangent && std::ranges::any_of(
            bsplines, [&](const auto& value) {
                return value.id == constraint.geometry_id;
            });
        const bool second_tangent_spline = tangent && std::ranges::any_of(
            bsplines, [&](const auto& value) {
                return value.id == constraint.second_geometry_id;
            });
        const auto first_equal_radius = equal_radius
            ? circular_curve_radius(*this, constraint.geometry_id) : std::nullopt;
        const auto second_equal_radius = equal_radius
            ? circular_curve_radius(*this, constraint.second_geometry_id) : std::nullopt;
        const bool first_is_segment = owned_segment != nullptr;
        const bool second_is_segment = second_owned_segment != nullptr;
        const bool first_is_axis = tangent &&
            is_base_sketch_axis(constraint.geometry_id);
        const bool second_is_axis = tangent &&
            is_base_sketch_axis(constraint.second_geometry_id);
        const bool first_is_line = first_is_segment || first_is_axis;
        const bool second_is_line = second_is_segment || second_is_axis;
        const bool external_direction_pair = pair_constraint &&
            constraint.kind != ConstraintKind::EqualLength &&
            segment_or_external_line(*this, constraint.geometry_id).has_value() &&
            second_is_segment;
        const bool tangent_line_curve_valid =
            first_is_line != second_is_line &&
            (first_is_line
                ? static_cast<bool>(second_tangent_curve) || second_tangent_spline
                : static_cast<bool>(first_tangent_curve) || first_tangent_spline);
        const bool tangent_curve_pair_valid =
            !first_is_line && !second_is_line &&
            first_tangent_curve && second_tangent_curve;
        const auto symmetry_axis = symmetric
            ? sketch_axis_line(*this, constraint.geometry_id) : std::nullopt;
        const bool symmetry_axis_valid = symmetry_axis &&
            std::hypot(symmetry_axis->second[0], symmetry_axis->second[1]) > 1.0e-12;
        const auto point_exists = [&](const std::string& point_id) {
            return find_point(point_id) != nullptr ||
                external_point_position(*this, point_id).has_value();
        };
        const bool points_valid = tangent
            ? constraint.second_point_id.empty() &&
              (constraint.first_point_id.empty() ||
               find_point(constraint.first_point_id) != nullptr)
            : pair_constraint || midpoint_on_line || concentric || equal_radius
            ? constraint.first_point_id.empty() && constraint.second_point_id.empty()
            : point_on_circle || point_on_line || midpoint
                ? find_point(constraint.first_point_id) != nullptr &&
                  constraint.second_point_id.empty()
            : point_exists(constraint.first_point_id) &&
              point_exists(constraint.second_point_id) &&
              (find_point(constraint.first_point_id) != nullptr ||
               find_point(constraint.second_point_id) != nullptr);
        if (constraint.id.empty() || !ids.insert(constraint.id).second || !points_valid ||
            (segment_constraint && !constraint.geometry_id.empty() &&
             (owned_segment == nullptr ||
              owned_segment->first_point_id != constraint.first_point_id ||
              owned_segment->second_point_id != constraint.second_point_id)) ||
            (segment_constraint && !constraint.second_geometry_id.empty()) ||
            (constraint.kind == ConstraintKind::Coincident &&
             (!constraint.geometry_id.empty() || !constraint.second_geometry_id.empty())) ||
            (point_on_circle && ((!owned_point_curve &&
                                  owned_point_spline == bsplines.end()) ||
             (owned_point_curve &&
              owned_point_curve->center_point_id == constraint.first_point_id) ||
             (owned_point_spline != bsplines.end() &&
              std::ranges::find(owned_point_spline->control_point_ids,
                  constraint.first_point_id) !=
                  owned_point_spline->control_point_ids.end()) ||
             !constraint.second_geometry_id.empty())) ||
            (point_on_line && (constrained_native_point == nullptr ||
             !point_on_line_target(*this, constraint.geometry_id,
                 constrained_native_point->x, constrained_native_point->y) ||
             !constraint.second_geometry_id.empty())) ||
            (midpoint_on_line &&
             (owned_segment == nullptr ||
              !segment_or_external_line(
                  *this, constraint.second_geometry_id).has_value() ||
              constraint.geometry_id == constraint.second_geometry_id)) ||
            (symmetric && (constraint.first_point_id == constraint.second_point_id ||
             !symmetry_axis_valid || !constraint.second_geometry_id.empty())) ||
            (midpoint && (owned_segment == nullptr ||
             owned_segment->first_point_id == constraint.first_point_id ||
             owned_segment->second_point_id == constraint.first_point_id ||
             !constraint.second_geometry_id.empty())) ||
            (concentric && (!first_curve_center || !second_curve_center ||
             constraint.geometry_id == constraint.second_geometry_id ||
             *first_curve_center == *second_curve_center)) ||
            (equal_radius && (!first_equal_radius || !second_equal_radius ||
             constraint.geometry_id == constraint.second_geometry_id)) ||
            (tangent && (
             constraint.geometry_id == constraint.second_geometry_id ||
             (!tangent_line_curve_valid && !tangent_curve_pair_valid) ||
             (tangent_line_curve_valid && constraint.tangent_internal))) ||
            (!tangent && constraint.tangent_internal) ||
            (segment_constraint && !constraint.second_geometry_id.empty()) ||
            (pair_constraint && !external_direction_pair &&
             (owned_segment == nullptr ||
              second_owned_segment == nullptr ||
              constraint.geometry_id == constraint.second_geometry_id))) {
            throw std::runtime_error("Sketch constraint is invalid");
        }
    }
    ids.clear();
    for (const auto& dimension : dimensions) {
        const bool radial_dimension = dimension.kind == DimensionKind::Radius ||
            dimension.kind == DimensionKind::Diameter;
        const bool line_pair_dimension =
            dimension.kind == DimensionKind::DistanceLine ||
            dimension.kind == DimensionKind::AngleBetween;
        const bool point_line_angle =
            dimension.kind == DimensionKind::AngleBetween &&
            dimension.second_geometry_id.empty() &&
            find_point(dimension.first_point_id) != nullptr &&
            find_point(dimension.second_point_id) != nullptr &&
            dimension.first_point_id != dimension.second_point_id;
        const bool four_point_angle =
            dimension.kind == DimensionKind::AngleBetween &&
            find_point(dimension.first_point_id) != nullptr &&
            find_point(dimension.second_point_id) != nullptr &&
            find_point(dimension.geometry_id) != nullptr &&
            find_point(dimension.second_geometry_id) != nullptr &&
            dimension.first_point_id != dimension.second_point_id &&
            dimension.geometry_id != dimension.second_geometry_id;
        const bool point_line_dimension =
            dimension.kind == DimensionKind::DistancePointLine ||
            dimension.kind == DimensionKind::DistanceSymmetric;
        const bool ellipse_dimension =
            dimension.kind == DimensionKind::EllipseMajorRadius ||
            dimension.kind == DimensionKind::EllipseMinorRadius ||
            dimension.kind == DimensionKind::EllipseRotation;
        const bool three_point_angle =
            dimension.kind == DimensionKind::AngleThreePoint;
        const bool coordinate_axis_dimension =
            has_coordinate_axis_reference(dimension) &&
            dimension.second_point_id.empty() &&
            dimension.second_geometry_id.empty() &&
            find_point(dimension.first_point_id) != nullptr;
        const bool circle_geometry = radial_dimension && std::any_of(
            circles.begin(), circles.end(), [&](const auto& circle) {
                return circle.id == dimension.geometry_id;
            });
        const bool geometry_valid = radial_dimension &&
            (circle_geometry || std::any_of(arcs.begin(), arcs.end(), [&](const auto& arc) {
                return arc.id == dimension.geometry_id;
             }) || radial_dimension && std::any_of(
                 corner_radii.begin(), corner_radii.end(), [&](const auto& radius) {
                     return radius.id == dimension.geometry_id;
                 }));
        const bool ellipse_geometry_valid = ellipse_dimension && std::any_of(
            ellipses.begin(), ellipses.end(), [&](const auto& ellipse) {
                return ellipse.id == dimension.geometry_id;
            });
        const auto* owned_segment = segment_by_id(dimension.geometry_id);
        const bool segment_geometry_valid = !radial_dimension && !ellipse_dimension &&
            !three_point_angle &&
            !point_line_dimension &&
            owned_segment != nullptr &&
            owned_segment->first_point_id == dimension.first_point_id &&
            owned_segment->second_point_id == dimension.second_point_id;
        const bool point_pair_geometry_valid = !radial_dimension &&
            !ellipse_dimension && !three_point_angle && dimension.geometry_id.empty() &&
            (dimension.kind == DimensionKind::Distance ||
             dimension.kind == DimensionKind::DistanceX ||
             dimension.kind == DimensionKind::DistanceY) &&
            (find_point(dimension.first_point_id) != nullptr ||
             external_point_position(*this, dimension.first_point_id)) &&
            (find_point(dimension.second_point_id) != nullptr ||
             external_point_position(*this, dimension.second_point_id)) &&
            (find_point(dimension.first_point_id) != nullptr ||
             find_point(dimension.second_point_id) != nullptr);
        const auto first_dimension_line = line_pair_dimension
            ? (sketch_axis_line(*this, dimension.geometry_id)
                ? sketch_axis_line(*this, dimension.geometry_id)
                : segment_or_external_line(*this, dimension.geometry_id))
            : std::nullopt;
        auto second_dimension_line = line_pair_dimension && !point_line_angle
            ? segment_or_external_line(*this, dimension.second_geometry_id)
            : std::optional<std::pair<std::array<double, 2>,
                  std::array<double, 2>>>{};
        if (point_line_angle) {
            const auto* first = find_point(dimension.first_point_id);
            const auto* second = find_point(dimension.second_point_id);
            second_dimension_line = std::pair{
                std::array{first->x, first->y},
                std::array{second->x - first->x, second->y - first->y}};
        }
        const bool line_pair_geometry_valid = line_pair_dimension &&
            ((four_point_angle && angle_dimension_lines(*this, dimension)) ||
             (first_dimension_line && second_dimension_line &&
             (point_line_angle ||
             (dimension.geometry_id != dimension.second_geometry_id &&
              std::any_of(segments.begin(), segments.end(), [&](const auto& segment) {
                  return segment.id == dimension.second_geometry_id;
              })))));
        const auto point_dimension_line = point_line_dimension
            ? (sketch_axis_line(*this, dimension.geometry_id)
                ? sketch_axis_line(*this, dimension.geometry_id)
                : segment_or_external_line(*this, dimension.geometry_id))
            : std::nullopt;
        const bool point_line_geometry_valid = point_line_dimension &&
            find_point(dimension.first_point_id) != nullptr &&
            (dimension.kind == DimensionKind::DistancePointLine
                ? dimension.second_point_id.empty()
                : (dimension.second_point_id.empty() ||
                   (dimension.second_point_id != dimension.first_point_id &&
                    find_point(dimension.second_point_id) != nullptr))) &&
            dimension.second_geometry_id.empty() && point_dimension_line;
        const bool three_point_angle_valid = three_point_angle &&
            dimension.second_geometry_id.empty() &&
            dimension.first_point_id != dimension.second_point_id &&
            dimension.geometry_id != dimension.first_point_id &&
            dimension.geometry_id != dimension.second_point_id &&
            find_point(dimension.first_point_id) != nullptr &&
            find_point(dimension.second_point_id) != nullptr &&
            find_point(dimension.geometry_id) != nullptr;
        if (dimension.id.empty() || !ids.insert(dimension.id).second ||
            (radial_dimension ? !geometry_valid
                : ellipse_dimension ? !ellipse_geometry_valid
                : line_pair_dimension ? !line_pair_geometry_valid
                : point_line_dimension ? !point_line_geometry_valid
                : three_point_angle ? !three_point_angle_valid
                : !segment_geometry_valid && !point_pair_geometry_valid &&
                  !coordinate_axis_dimension)) {
            throw std::runtime_error("Sketch dimension is invalid");
        }
        if (dimension.locked && !dimension.driving) {
            throw std::runtime_error(
                "A reference Sketch dimension cannot lock its measured value");
        }
        require_finite(dimension.value, "dimension value");
        if ((dimension.kind == DimensionKind::Distance ||
             dimension.kind == DimensionKind::DistancePointLine ||
             dimension.kind == DimensionKind::DistanceSymmetric ||
             dimension.kind == DimensionKind::DistanceLine || radial_dimension ||
             dimension.kind == DimensionKind::EllipseMajorRadius ||
             dimension.kind == DimensionKind::EllipseMinorRadius) &&
            dimension.value < 0.0) {
            throw std::runtime_error("Distance must not be negative");
        }
        if ((dimension.kind == DimensionKind::AngleBetween &&
             (dimension.value < -180.0 || dimension.value > 180.0)) ||
            ((dimension.kind == DimensionKind::Angle ||
             dimension.kind == DimensionKind::EllipseRotation) &&
             (dimension.value < -180.0 || dimension.value > 180.0))) {
            throw std::runtime_error("Angle must lie between -180 and 180 degrees");
        }
        if ((dimension.kind == DimensionKind::Angle ||
             dimension.kind == DimensionKind::AngleThreePoint ||
             dimension.kind == DimensionKind::AngleBetween ||
             dimension.kind == DimensionKind::EllipseRotation) &&
            ((dimension.lower_limit && (*dimension.lower_limit < -180.0 ||
                                        *dimension.lower_limit > 180.0)) ||
             (dimension.upper_limit && (*dimension.upper_limit < -180.0 ||
                                        *dimension.upper_limit > 180.0)))) {
            throw std::runtime_error("Angle limits must lie between -180 and 180 degrees");
        }
        if (dimension.lower_limit) require_finite(*dimension.lower_limit, "lower limit");
        if (dimension.upper_limit) require_finite(*dimension.upper_limit, "upper limit");
        if (dimension.placement) {
            require_finite((*dimension.placement)[0], "dimension placement x");
            require_finite((*dimension.placement)[1], "dimension placement y");
        }
        if (dimension.lower_limit && dimension.upper_limit &&
            *dimension.lower_limit > *dimension.upper_limit) {
            throw std::runtime_error("Dimension limits are reversed");
        }
        if ((dimension.lower_limit && dimension.value < *dimension.lower_limit) ||
            (dimension.upper_limit && dimension.value > *dimension.upper_limit)) {
            throw std::runtime_error("Dimension value lies outside its absolute limits");
        }
    }
    std::unordered_set<std::string> block_ids;
    std::unordered_set<std::string> grouped_geometry;
    const auto geometry_exists = [&](const std::string& geometry_id) {
        return std::ranges::any_of(segments, [&](const auto& value) { return value.id == geometry_id; }) ||
            std::ranges::any_of(circles, [&](const auto& value) { return value.id == geometry_id; }) ||
            std::ranges::any_of(arcs, [&](const auto& value) { return value.id == geometry_id; }) ||
            std::ranges::any_of(ellipses, [&](const auto& value) { return value.id == geometry_id; }) ||
            std::ranges::any_of(elliptical_arcs, [&](const auto& value) { return value.id == geometry_id; }) ||
            std::ranges::any_of(bsplines, [&](const auto& value) { return value.id == geometry_id; });
    };
    for (const auto& block : import_blocks) {
        if (block.id.empty() || block.name.empty() ||
            !block_ids.insert(block.id).second || block.geometry_ids.empty() ||
            block.point_ids.empty() || !std::isfinite(block.translation_x) ||
            !std::isfinite(block.translation_y) || !std::isfinite(block.rotation)) {
            throw std::runtime_error("Sketch import block is invalid");
        }
        for (const auto& geometry_id : block.geometry_ids) {
            if (!geometry_exists(geometry_id) ||
                !grouped_geometry.insert(geometry_id).second) {
                throw std::runtime_error("Import block geometry is missing or shared");
            }
        }
        for (const auto& point_id : block.point_ids) {
            if (find_point(point_id) == nullptr) {
                throw std::runtime_error("Import block point is missing");
            }
        }
    }
}

bool Sketch::set_dimension_value(const std::string& dimension_id, double value) {
    if (!std::isfinite(value)) return false;
    const auto found = std::find_if(dimensions.begin(), dimensions.end(),
        [&](const auto& dimension) { return dimension.id == dimension_id; });
    if (found == dimensions.end() ||
        ((found->kind == DimensionKind::Distance ||
          found->kind == DimensionKind::DistancePointLine ||
          found->kind == DimensionKind::DistanceSymmetric ||
          found->kind == DimensionKind::DistanceLine ||
          found->kind == DimensionKind::Radius ||
          found->kind == DimensionKind::Diameter ||
          found->kind == DimensionKind::EllipseMajorRadius ||
          found->kind == DimensionKind::EllipseMinorRadius) && value < 0.0) ||
        ((found->kind == DimensionKind::Angle ||
          found->kind == DimensionKind::AngleBetween ||
          found->kind == DimensionKind::EllipseRotation) &&
         (value < -180.0 || value > 180.0)) ||
        (found->lower_limit && value < *found->lower_limit) ||
        (found->upper_limit && value > *found->upper_limit)) return false;
    auto updated = *found;
    updated.value = value;
    try {
        apply_dimension(std::move(updated));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Sketch::set_dimension_placement(
    const std::string& dimension_id, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const auto found = std::find_if(dimensions.begin(), dimensions.end(),
        [&](const auto& value) { return value.id == dimension_id; });
    if (found == dimensions.end()) return false;
    found->placement = std::array{x, y};
    return true;
}

void Sketch::set_point_fixed(const std::string& point_id, bool fixed) {
    auto next = *this;
    auto* point = next.find_point(point_id);
    if (point == nullptr) throw std::invalid_argument("Sketch point does not exist");
    if (externally_linked_point_ids(next).contains(point_id)) {
        throw std::invalid_argument(
            "Externally linked profile points are read-only");
    }
    point->fixed = fixed;
    next.validate();
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Point fixation conflicts with existing geometry");
    }
    *this = std::move(next);
}

void Sketch::set_geometry_construction(
    const std::string& geometry_id, bool construction) {
    if (geometry_id.empty()) {
        throw std::invalid_argument("Geometry ID is required");
    }
    bool found = false;
    const auto update = [&](auto& values) {
        const auto item = std::find_if(values.begin(), values.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        if (item == values.end()) return;
        item->construction = construction;
        found = true;
    };
    update(points);
    update(segments);
    update(circles);
    update(arcs);
    update(ellipses);
    update(elliptical_arcs);
    update(bsplines);
    if (!construction) {
        const auto segment = std::find_if(segments.begin(), segments.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        if (segment != segments.end()) segment->centerline = false;
    }
    if (!found) {
        throw std::invalid_argument(
            "Sketch geometry does not support a construction role");
    }
    validate();
}

void Sketch::set_segment_centerline(
    const std::string& segment_id, bool centerline) {
    const auto found = std::find_if(segments.begin(), segments.end(),
        [&](const auto& segment) { return segment.id == segment_id; });
    if (found == segments.end()) {
        throw std::invalid_argument("Sketch centerline segment does not exist");
    }
    found->construction = centerline || found->construction;
    found->centerline = centerline;
}

bool Sketch::move_point(const std::string& point_id, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    auto next = *this;
    auto* point = next.find_point(point_id);
    const auto externally_linked = externally_linked_point_ids(next);
    if (point == nullptr || point->fixed ||
        externally_linked.contains(point_id)) return false;
    const double original_x = point->x;
    const double original_y = point->y;
    // A mouse ray almost never lands numerically exactly on an axis. If the
    // dragged point's X or Y coordinate is already tied through a directional
    // relation to an anchored point, project that coordinate before making
    // the dragged point the solver root. Otherwise the root and the axis
    // relation demand two incompatible values and an otherwise valid slide
    // along the axis is rejected.
    const auto anchored_coordinate = [&](bool x_coordinate)
        -> std::optional<double> {
        std::vector<std::string> pending{point_id};
        std::unordered_set<std::string> visited;
        while (!pending.empty()) {
            const auto current_id = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(current_id).second) continue;
            const auto* current = next.find_point(current_id);
            if (current == nullptr) continue;
            if (current_id != point_id &&
                (current->fixed || externally_linked.contains(current_id))) {
                return x_coordinate ? current->x : current->y;
            }
            for (const auto& constraint : next.constraints) {
                if (constraint.suppressed) continue;
                if (constraint.kind == ConstraintKind::Coincident &&
                    constraint.first_point_id == current_id &&
                    constraint.second_point_id == "sketch_origin") {
                    return 0.0;
                }
                if (constraint.kind == ConstraintKind::PointOnLine &&
                    constraint.first_point_id == current_id &&
                    ((x_coordinate && constraint.geometry_id == "sketch_axis:y") ||
                     (!x_coordinate && constraint.geometry_id == "sketch_axis:x"))) {
                    return 0.0;
                }
                const bool transfers_coordinate =
                    constraint.kind == ConstraintKind::Coincident ||
                    (x_coordinate && constraint.kind == ConstraintKind::Vertical) ||
                    (!x_coordinate && constraint.kind == ConstraintKind::Horizontal);
                if (!transfers_coordinate) continue;
                if (constraint.first_point_id == current_id &&
                    !constraint.second_point_id.empty()) {
                    pending.push_back(constraint.second_point_id);
                } else if (constraint.second_point_id == current_id &&
                           !constraint.first_point_id.empty()) {
                    pending.push_back(constraint.first_point_id);
                }
            }
        }
        return std::nullopt;
    };
    if (const auto anchored_x = anchored_coordinate(true)) x = *anchored_x;
    if (const auto anchored_y = anchored_coordinate(false)) y = *anchored_y;
    const double requested_translation_x = x - original_x;
    const double requested_translation_y = y - original_y;
    const bool dragging_curve_center = std::any_of(
            next.circles.begin(), next.circles.end(), [&](const auto& value) {
                return value.center_point_id == point_id;
            }) || std::any_of(next.arcs.begin(), next.arcs.end(),
            [&](const auto& value) { return value.center_point_id == point_id; }) ||
        std::any_of(next.ellipses.begin(), next.ellipses.end(),
            [&](const auto& value) { return value.center_point_id == point_id; }) ||
        std::any_of(next.elliptical_arcs.begin(), next.elliptical_arcs.end(),
            [&](const auto& value) { return value.center_point_id == point_id; });
    const bool dragging_tangent_contact = std::any_of(
        next.constraints.begin(), next.constraints.end(),
        [&](const auto& support) {
            if (support.suppressed ||
                support.kind != ConstraintKind::PointOnCircle ||
                support.first_point_id != point_id) return false;
            return std::any_of(next.constraints.begin(), next.constraints.end(),
                [&](const auto& tangent) {
                    if (tangent.suppressed ||
                        tangent.kind != ConstraintKind::Tangent ||
                        (tangent.geometry_id != support.geometry_id &&
                         tangent.second_geometry_id != support.geometry_id)) {
                        return false;
                    }
                    const auto segment_id = tangent.geometry_id == support.geometry_id
                        ? tangent.second_geometry_id : tangent.geometry_id;
                    const auto segment = std::find_if(next.segments.begin(),
                        next.segments.end(), [&](const auto& value) {
                            return value.id == segment_id;
                        });
                    return segment != next.segments.end() &&
                        (segment->first_point_id == point_id ||
                         segment->second_point_id == point_id);
                });
        });
    bool translated_drag_component = false;
    if ((dragging_curve_center || dragging_tangent_contact) &&
        std::hypot(requested_translation_x, requested_translation_y) > 1.0e-12) {
        auto component = point_translation_closure(next, point_id);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            const auto insert = [&](const std::string& id) {
                if (component.insert(id).second) expanded = true;
            };
            for (const auto& segment : next.segments) {
                if (!component.contains(segment.first_point_id) &&
                    !component.contains(segment.second_point_id)) continue;
                insert(segment.first_point_id);
                insert(segment.second_point_id);
            }
            for (const auto& arc : next.arcs) {
                bool connected = component.contains(arc.center_point_id) ||
                    component.contains(arc.start_point_id) ||
                    component.contains(arc.end_point_id);
                for (const auto& constraint : next.constraints) {
                    if (!constraint.suppressed &&
                        constraint.kind == ConstraintKind::PointOnCircle &&
                        constraint.geometry_id == arc.id &&
                        component.contains(constraint.first_point_id)) {
                        connected = true;
                    }
                }
                if (!connected) continue;
                insert(arc.center_point_id);
                insert(arc.start_point_id);
                insert(arc.end_point_id);
                for (const auto& constraint : next.constraints) {
                    if (!constraint.suppressed &&
                        constraint.kind == ConstraintKind::PointOnCircle &&
                        constraint.geometry_id == arc.id) {
                        insert(constraint.first_point_id);
                    }
                }
            }
            for (const auto& circle : next.circles) {
                bool connected = component.contains(circle.center_point_id);
                for (const auto& constraint : next.constraints) {
                    if (!constraint.suppressed &&
                        constraint.kind == ConstraintKind::PointOnCircle &&
                        constraint.geometry_id == circle.id &&
                        component.contains(constraint.first_point_id)) {
                        connected = true;
                    }
                }
                if (!connected) continue;
                insert(circle.center_point_id);
                for (const auto& constraint : next.constraints) {
                    if (!constraint.suppressed &&
                        constraint.kind == ConstraintKind::PointOnCircle &&
                        constraint.geometry_id == circle.id) {
                        insert(constraint.first_point_id);
                    }
                }
            }
        }
        for (const auto& dependent_id : component) {
            auto* dependent = next.find_point(dependent_id);
            if (dependent == nullptr || dependent->fixed ||
                externally_linked.contains(dependent_id)) return false;
        }
        for (const auto& dependent_id : component) {
            if (dependent_id == point_id) continue;
            auto* dependent = next.find_point(dependent_id);
            dependent->x += requested_translation_x;
            dependent->y += requested_translation_y;
        }
        translated_drag_component = true;
    }
    // A regular polygon is persisted as a construction support circle, a
    // closed ring of Segments, PointOnCircle relations for every vertex and
    // EqualLength relations from one reference side. Dragging one rim vertex
    // expresses polygon rotation, not an independent angular move that leaves
    // the nonlinear solver free to select a folded equal-chord branch.
    for (const auto& support : next.constraints) {
        if (support.suppressed ||
            support.kind != ConstraintKind::PointOnCircle ||
            support.first_point_id != point_id) continue;
        const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) {
                return value.id == support.geometry_id && value.construction;
            });
        if (circle == next.circles.end()) continue;
        std::set<std::string> vertex_ids;
        for (const auto& candidate : next.constraints) {
            if (!candidate.suppressed &&
                candidate.kind == ConstraintKind::PointOnCircle &&
                candidate.geometry_id == circle->id) {
                vertex_ids.insert(candidate.first_point_id);
            }
        }
        if (vertex_ids.size() < 4) continue;
        std::set<std::string> ring_segment_ids;
        std::map<std::string, unsigned> vertex_degree;
        for (const auto& segment : next.segments) {
            if (!vertex_ids.contains(segment.first_point_id) ||
                !vertex_ids.contains(segment.second_point_id)) continue;
            ring_segment_ids.insert(segment.id);
            ++vertex_degree[segment.first_point_id];
            ++vertex_degree[segment.second_point_id];
        }
        const auto equal_count = std::count_if(next.constraints.begin(),
            next.constraints.end(), [&](const auto& candidate) {
                return !candidate.suppressed &&
                    candidate.kind == ConstraintKind::EqualLength &&
                    ring_segment_ids.contains(candidate.geometry_id) &&
                    ring_segment_ids.contains(candidate.second_geometry_id);
            });
        const bool closed_ring = ring_segment_ids.size() == vertex_ids.size() &&
            std::ranges::all_of(vertex_ids, [&](const auto& vertex_id) {
                return vertex_degree[vertex_id] == 2;
            });
        if (!closed_ring ||
            equal_count + 1 < static_cast<std::ptrdiff_t>(vertex_ids.size())) {
            continue;
        }
        const auto* center = next.find_point(circle->center_point_id);
        if (center == nullptr) return false;
        const double requested_radius = std::hypot(x - center->x, y - center->y);
        if (requested_radius <= 1.0e-12) return false;
        const double original_angle = std::atan2(
            original_y - center->y, original_x - center->x);
        const double requested_angle = std::atan2(y - center->y, x - center->x);
        const double rotation = requested_angle - original_angle;
        const double cosine = std::cos(rotation);
        const double sine = std::sin(rotation);
        for (const auto& vertex_id : vertex_ids) {
            if (vertex_id == point_id) continue;
            auto* vertex = next.find_point(vertex_id);
            if (vertex == nullptr || vertex->fixed ||
                externally_linked.contains(vertex_id)) return false;
            const double relative_x = vertex->x - center->x;
            const double relative_y = vertex->y - center->y;
            vertex->x = center->x + relative_x * cosine - relative_y * sine;
            vertex->y = center->y + relative_x * sine + relative_y * cosine;
        }
        x = center->x + circle->radius * std::cos(requested_angle);
        y = center->y + circle->radius * std::sin(requested_angle);
        break;
    }
    point->x = x;
    point->y = y;
    const double translation_x = x - original_x;
    const double translation_y = y - original_y;
    std::set<std::string> translated_circle_points;
    for (const auto& circle : next.circles) {
        if (circle.center_point_id != point_id) continue;
        if (translated_drag_component) continue;
        for (const auto& constraint : next.constraints) {
            if (constraint.suppressed ||
                constraint.kind != ConstraintKind::PointOnCircle ||
                constraint.geometry_id != circle.id ||
                !translated_circle_points.insert(constraint.first_point_id).second) {
                continue;
            }
            auto* attached = next.find_point(constraint.first_point_id);
            if (attached == nullptr || (attached->fixed &&
                (std::abs(translation_x) > 1.0e-12 ||
                 std::abs(translation_y) > 1.0e-12))) {
                return false;
            }
            attached->x += translation_x;
            attached->y += translation_y;
        }
    }
    for (const auto& constraint : next.constraints) {
        if (constraint.suppressed ||
            constraint.kind != ConstraintKind::PointOnCircle ||
            constraint.first_point_id != point_id) continue;
        auto circle = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) { return value.id == constraint.geometry_id; });
        if (circle == next.circles.end()) {
            const auto target = circular_constraint_target(
                next, constraint.geometry_id, x, y);
            auto* attached = next.find_point(point_id);
            if (!target || attached == nullptr) return false;
            attached->x = target->position[0];
            attached->y = target->position[1];
            continue;
        }
        const auto* center = next.find_point(circle->center_point_id);
        const double cursor_radius = std::hypot(x - center->x, y - center->y);
        if (cursor_radius <= 1.0e-12 || circle->radius <= 1.0e-12) return false;
        // Dragging an attached point changes its angular position only. A
        // circle radius is changed exclusively by its radius/diameter
        // parameter operation, never as a side effect of point movement.
        point->x = center->x + (x - center->x) * circle->radius / cursor_radius;
        point->y = center->y + (y - center->y) * circle->radius / cursor_radius;
    }
    for (const auto& constraint : next.constraints) {
        if (constraint.suppressed ||
            constraint.kind != ConstraintKind::PointOnLine ||
            constraint.first_point_id != point_id) continue;
        auto* attached = next.find_point(point_id);
        const auto target = point_on_line_target(
            next, constraint.geometry_id, attached->x, attached->y);
        if (!target) return false;
        attached->x = (*target)[0];
        attached->y = (*target)[1];
    }
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    const auto propagate_connected_segment_endpoint = [&]
            (const std::string& endpoint_id,
             const std::array<double, 2>& original,
             const SketchPoint* current,
             const std::string& curve_id) {
        if (current == nullptr) return true;
        const double dx = current->x - original[0];
        const double dy = current->y - original[1];
        if (std::hypot(dx, dy) <= 1.0e-12) return true;
        std::set<std::string> translated;
        for (const auto& segment : next.segments) {
            if (segment.first_point_id != endpoint_id &&
                segment.second_point_id != endpoint_id) continue;
            const auto& other_id = segment.first_point_id == endpoint_id
                ? segment.second_point_id : segment.first_point_id;
            if (!translated.insert(other_id).second) continue;
            auto* other = next.find_point(other_id);
            if (other == nullptr || other->fixed ||
                externally_linked.contains(other_id)) return false;
            const bool tangent_connected = std::ranges::any_of(
                next.constraints, [&](const auto& constraint) {
                    return !constraint.suppressed &&
                        constraint.kind == ConstraintKind::Tangent &&
                        ((constraint.geometry_id == segment.id &&
                          constraint.second_geometry_id == curve_id) ||
                         (constraint.second_geometry_id == segment.id &&
                          constraint.geometry_id == curve_id));
                });
            const auto* old_endpoint = find_point(endpoint_id);
            const auto* old_other = find_point(other_id);
            const auto old_tangent = tangent_connected && old_endpoint != nullptr
                ? curve_tangent_at_point(
                      curve_id, old_endpoint->x, old_endpoint->y)
                : std::nullopt;
            const auto new_tangent = tangent_connected
                ? next.curve_tangent_at_point(
                      curve_id, current->x, current->y)
                : std::nullopt;
            if (old_tangent && new_tangent && old_other != nullptr) {
                const double length = std::hypot(
                    old_other->x - old_endpoint->x,
                    old_other->y - old_endpoint->y);
                const double orientation_sign =
                    (old_other->x - old_endpoint->x) * (*old_tangent)[0] +
                        (old_other->y - old_endpoint->y) * (*old_tangent)[1] < 0.0
                    ? -1.0 : 1.0;
                other->x = current->x +
                    orientation_sign * length * (*new_tangent)[0];
                other->y = current->y +
                    orientation_sign * length * (*new_tangent)[1];
            } else {
                other->x += dx;
                other->y += dy;
            }
        }
        return true;
    };
    for (auto& arc : next.arcs) {
        auto* center = next.find_point(arc.center_point_id);
        auto* start = next.find_point(arc.start_point_id);
        auto* end = next.find_point(arc.end_point_id);
        if (arc.center_point_id == point_id) {
            if (translated_drag_component) continue;
            const double dx = x - original_x;
            const double dy = y - original_y;
            if ((start->fixed && (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12)) ||
                (end->fixed && (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12))) {
                return false;
            }
            start->x += dx;
            start->y += dy;
            end->x += dx;
            end->y += dy;
            for (const auto& constraint : next.constraints) {
                if (constraint.suppressed ||
                    constraint.kind != ConstraintKind::PointOnCircle ||
                    constraint.geometry_id != arc.id ||
                    constraint.first_point_id == arc.start_point_id ||
                    constraint.first_point_id == arc.end_point_id) continue;
                auto* attached = next.find_point(constraint.first_point_id);
                if (attached == nullptr || (attached->fixed &&
                    (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12))) {
                    return false;
                }
                attached->x += dx;
                attached->y += dy;
            }
            continue;
        }
        const bool moving_start = arc.start_point_id == point_id;
        const bool moving_end = arc.end_point_id == point_id;
        if (!moving_start && !moving_end) continue;
        const std::array original_start = arc.start_point_id == point_id
            ? std::array{original_x, original_y}
            : std::array{start->x, start->y};
        const std::array original_end = arc.end_point_id == point_id
            ? std::array{original_x, original_y}
            : std::array{end->x, end->y};
        double angle = std::atan2(y - center->y, x - center->x);
        const double requested_radius = std::hypot(x - center->x, y - center->y);
        if (requested_radius <= 1.0e-12) return false;
        const bool radius_locked = std::ranges::any_of(next.dimensions,
            [&](const auto& dimension) {
                return !dimension.suppressed && dimension.driving &&
                    dimension.locked && dimension.geometry_id == arc.id &&
                    (dimension.kind == DimensionKind::Radius ||
                     dimension.kind == DimensionKind::Diameter);
            });
        // An Arc endpoint is a radial handle: its distance from the center
        // edits radius and its direction edits sweep. A locked radius keeps
        // the old distance and turns the gesture into an angular slide.
        const double radius = radius_locked ? arc.radius : requested_radius;
        if (moving_start) {
            while (angle > arc.start_angle + 3.14159265358979323846) angle -= full_turn;
            while (angle < arc.start_angle - 3.14159265358979323846) angle += full_turn;
            while (angle >= arc.end_angle) angle -= full_turn;
            if (arc.end_angle - angle >= full_turn - 1.0e-12 ||
                (end->fixed && std::abs(radius - arc.radius) > 1.0e-12)) return false;
            arc.start_angle = angle;
            arc.radius = radius;
            start->x = center->x + radius * std::cos(angle);
            start->y = center->y + radius * std::sin(angle);
            end->x = center->x + radius * std::cos(arc.end_angle);
            end->y = center->y + radius * std::sin(arc.end_angle);
        } else {
            while (angle <= arc.start_angle) angle += full_turn;
            while (angle > arc.start_angle + full_turn) angle -= full_turn;
            if (angle - arc.start_angle >= full_turn - 1.0e-12 ||
                (start->fixed && std::abs(radius - arc.radius) > 1.0e-12)) return false;
            arc.end_angle = angle;
            arc.radius = radius;
            start->x = center->x + radius * std::cos(arc.start_angle);
            start->y = center->y + radius * std::sin(arc.start_angle);
            end->x = center->x + radius * std::cos(angle);
            end->y = center->y + radius * std::sin(angle);
        }
        // Editing one Arc endpoint may change the radius and therefore move
        // the opposite endpoint as a dependent handle. If that endpoint is
        // shared with a connected segment, translate the segment's other end
        // by the same delta before solving. This preserves its length and
        // direction and prevents a forward/back radius gesture from
        // accumulating branch drift in a tangent polyline chain.
        if (!propagate_connected_segment_endpoint(
                arc.start_point_id, original_start, start, arc.id) ||
            !propagate_connected_segment_endpoint(
                arc.end_point_id, original_end, end, arc.id)) return false;
    }
    for (auto& ellipse : next.ellipses) {
        auto* center = next.find_point(ellipse.center_point_id);
        auto* major = next.find_point(ellipse.major_point_id);
        auto* minor = next.find_point(ellipse.minor_point_id);
        const double orientation = ellipse.reversed ? -1.0 : 1.0;
        if (ellipse.center_point_id == point_id) {
            if (translated_drag_component) continue;
            const double dx = x - original_x;
            const double dy = y - original_y;
            if ((major->fixed || minor->fixed) &&
                (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12)) return false;
            major->x += dx;
            major->y += dy;
            minor->x += dx;
            minor->y += dy;
            for (const auto& constraint : next.constraints) {
                if (constraint.suppressed ||
                    constraint.kind != ConstraintKind::PointOnCircle ||
                    constraint.geometry_id != ellipse.id) continue;
                auto* attached = next.find_point(constraint.first_point_id);
                if (attached == nullptr || (attached->fixed &&
                    (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12))) {
                    return false;
                }
                attached->x += dx;
                attached->y += dy;
            }
        } else if (ellipse.major_point_id == point_id) {
            const double radius = std::hypot(x - center->x, y - center->y);
            if (radius <= 1.0e-12 || minor->fixed) return false;
            ellipse.major_radius = radius;
            ellipse.rotation = std::atan2(y - center->y, x - center->x);
            minor->x = center->x - orientation * ellipse.minor_radius *
                std::sin(ellipse.rotation);
            minor->y = center->y + orientation * ellipse.minor_radius *
                std::cos(ellipse.rotation);
        } else if (ellipse.minor_point_id == point_id) {
            const double projected = orientation * (
                -(x - center->x) * std::sin(ellipse.rotation) +
                 (y - center->y) * std::cos(ellipse.rotation));
            if (std::abs(projected) <= 1.0e-12) return false;
            ellipse.minor_radius = std::abs(projected);
            minor->x = center->x - orientation * ellipse.minor_radius *
                std::sin(ellipse.rotation);
            minor->y = center->y + orientation * ellipse.minor_radius *
                std::cos(ellipse.rotation);
        }
    }
    for (auto& arc : next.elliptical_arcs) {
        auto* center = next.find_point(arc.center_point_id);
        auto* major = next.find_point(arc.major_point_id);
        auto* minor = next.find_point(arc.minor_point_id);
        auto* start = next.find_point(arc.start_point_id);
        auto* end = next.find_point(arc.end_point_id);
        const std::array original_start = arc.start_point_id == point_id
            ? std::array{original_x, original_y}
            : std::array{start->x, start->y};
        const std::array original_end = arc.end_point_id == point_id
            ? std::array{original_x, original_y}
            : std::array{end->x, end->y};
        const double orientation = arc.reversed ? -1.0 : 1.0;
        const auto assign_position = [&](SketchPoint* target,
                                         const std::array<double, 2>& position) {
            if (target->fixed &&
                std::hypot(target->x - position[0], target->y - position[1]) >
                    1.0e-12) {
                return false;
            }
            target->x = position[0];
            target->y = position[1];
            return true;
        };
        if (arc.center_point_id == point_id) {
            if (translated_drag_component) continue;
            std::set<std::string> translated;
            for (const auto& dependent_id : {
                    arc.major_point_id, arc.minor_point_id,
                    arc.start_point_id, arc.end_point_id}) {
                if (!translated.insert(dependent_id).second) continue;
                auto* dependent = next.find_point(dependent_id);
                if (dependent->fixed &&
                    (std::abs(translation_x) > 1.0e-12 ||
                     std::abs(translation_y) > 1.0e-12)) {
                    return false;
                }
                dependent->x += translation_x;
                dependent->y += translation_y;
            }
            for (const auto& constraint : next.constraints) {
                if (constraint.suppressed ||
                    constraint.kind != ConstraintKind::PointOnCircle ||
                    constraint.geometry_id != arc.id ||
                    translated.contains(constraint.first_point_id)) continue;
                auto* attached = next.find_point(constraint.first_point_id);
                if (attached == nullptr || (attached->fixed &&
                    (std::abs(translation_x) > 1.0e-12 ||
                     std::abs(translation_y) > 1.0e-12))) return false;
                attached->x += translation_x;
                attached->y += translation_y;
            }
            continue;
        }
        if (arc.major_point_id == point_id) {
            const double radius = std::hypot(x - center->x, y - center->y);
            if (radius <= 1.0e-12) return false;
            const double rotation = std::atan2(y - center->y, x - center->x);
            const std::array<double, 2> minor_position{
                center->x - orientation * arc.minor_radius * std::sin(rotation),
                center->y + orientation * arc.minor_radius * std::cos(rotation)};
            const auto start_position = ellipse_position(
                center->x, center->y, radius, arc.minor_radius,
                rotation, arc.reversed, arc.start_parameter);
            const auto end_position = ellipse_position(
                center->x, center->y, radius, arc.minor_radius,
                rotation, arc.reversed, arc.end_parameter);
            if (!assign_position(minor, minor_position) ||
                !assign_position(start, start_position) ||
                !assign_position(end, end_position)) return false;
            arc.major_radius = radius;
            arc.rotation = rotation;
            if (!propagate_connected_segment_endpoint(
                    arc.start_point_id, original_start, start, arc.id) ||
                !propagate_connected_segment_endpoint(
                    arc.end_point_id, original_end, end, arc.id)) return false;
            continue;
        }
        if (arc.minor_point_id == point_id) {
            const double projected = orientation * (
                -(x - center->x) * std::sin(arc.rotation) +
                 (y - center->y) * std::cos(arc.rotation));
            const double radius = std::abs(projected);
            if (radius <= 1.0e-12) return false;
            const std::array<double, 2> minor_position{
                center->x - orientation * radius * std::sin(arc.rotation),
                center->y + orientation * radius * std::cos(arc.rotation)};
            const auto start_position = ellipse_position(
                center->x, center->y, arc.major_radius, radius,
                arc.rotation, arc.reversed, arc.start_parameter);
            const auto end_position = ellipse_position(
                center->x, center->y, arc.major_radius, radius,
                arc.rotation, arc.reversed, arc.end_parameter);
            if (!assign_position(minor, minor_position) ||
                !assign_position(start, start_position) ||
                !assign_position(end, end_position)) return false;
            arc.minor_radius = radius;
            if (!propagate_connected_segment_endpoint(
                    arc.start_point_id, original_start, start, arc.id) ||
                !propagate_connected_segment_endpoint(
                    arc.end_point_id, original_end, end, arc.id)) return false;
            continue;
        }
        const bool moving_start = arc.start_point_id == point_id;
        const bool moving_end = arc.end_point_id == point_id;
        if (!moving_start && !moving_end) continue;
        double parameter = ellipse_parameter(
            center->x, center->y, arc.major_radius, arc.minor_radius,
            arc.rotation, arc.reversed, x, y);
        if (moving_start) {
            parameter = unwrap_near(parameter, arc.start_parameter);
            while (parameter >= arc.end_parameter) parameter -= full_turn;
            if (arc.end_parameter - parameter >= full_turn - 1.0e-12) return false;
            arc.start_parameter = parameter;
            const auto position = ellipse_position(
                center->x, center->y, arc.major_radius, arc.minor_radius,
                arc.rotation, arc.reversed, parameter);
            start->x = position[0];
            start->y = position[1];
        } else {
            parameter = unwrap_near(parameter, arc.end_parameter);
            while (parameter <= arc.start_parameter) parameter += full_turn;
            if (parameter - arc.start_parameter >= full_turn - 1.0e-12) return false;
            arc.end_parameter = parameter;
            const auto position = ellipse_position(
                center->x, center->y, arc.major_radius, arc.minor_radius,
                arc.rotation, arc.reversed, parameter);
            end->x = position[0];
            end->y = position[1];
        }
    }
    // During direct manipulation an unlocked driving dimension is an editable
    // parameter, not a hidden immovable wall. Temporarily measure it like a
    // reference dimension, then restore its driving role with the value
    // reached by the drag. Locked dimensions remain hard equations.
    std::unordered_set<std::string> relaxed_dimension_ids;
    for (auto& dimension : next.dimensions) {
        if (dimension.suppressed || !dimension.driving || dimension.locked) continue;
        relaxed_dimension_ids.insert(dimension.id);
        dimension.driving = false;
    }
    // Point coordinates do not change the constraint graph or its rank.
    // Interactive dragging therefore needs equation convergence and final
    // residual verification, but not a fresh numerical DOF analysis.
    const auto solved = next.solve_impl(100, false, {point_id});
    if (solved.status == SolveStatus::Conflicting || solved.status == SolveStatus::Invalid) {
        return false;
    }
    if (!refresh_reference_dimensions(next)) return false;
    for (auto& dimension : next.dimensions) {
        if (relaxed_dimension_ids.contains(dimension.id)) dimension.driving = true;
    }
    for (auto& dimension : next.dimensions) {
        if (dimension.suppressed || dimension.driving) continue;
        double measured{};
        if (dimension.kind == DimensionKind::EllipseMajorRadius ||
            dimension.kind == DimensionKind::EllipseMinorRadius ||
            dimension.kind == DimensionKind::EllipseRotation) {
            const auto ellipse = std::find_if(next.ellipses.begin(), next.ellipses.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            measured = dimension.kind == DimensionKind::EllipseMajorRadius
                ? ellipse->major_radius
                : dimension.kind == DimensionKind::EllipseMinorRadius
                    ? ellipse->minor_radius
                    : ellipse->rotation * 180.0 / 3.14159265358979323846;
        } else if (dimension.kind == DimensionKind::Radius ||
                   dimension.kind == DimensionKind::Diameter) {
            const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (circle != next.circles.end()) {
                measured = dimension.kind == DimensionKind::Diameter
                    ? circle->radius * 2.0 : circle->radius;
            } else {
                const auto arc = std::find_if(next.arcs.begin(), next.arcs.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                if (arc != next.arcs.end()) {
                    measured = dimension.kind == DimensionKind::Diameter
                        ? arc->radius * 2.0 : arc->radius;
                } else {
                    const auto corner = std::find_if(
                        next.corner_radii.begin(), next.corner_radii.end(),
                        [&](const auto& value) {
                            return value.id == dimension.geometry_id;
                        });
                    if (corner == next.corner_radii.end()) return false;
                    measured = dimension.kind == DimensionKind::Diameter
                        ? corner->radius * 2.0 : corner->radius;
                }
            }
        } else if (dimension.kind == DimensionKind::DistancePointLine ||
                   dimension.kind == DimensionKind::DistanceSymmetric) {
            const auto reference = sketch_axis_line(next, dimension.geometry_id)
                ? sketch_axis_line(next, dimension.geometry_id)
                : segment_or_external_line(next, dimension.geometry_id);
            const auto* point = next.find_point(dimension.first_point_id);
            if (!reference || point == nullptr) return false;
            const double length = std::hypot(
                reference->second[0], reference->second[1]);
            measured = std::abs(
                reference->second[0] * (point->y - reference->first[1]) -
                reference->second[1] * (point->x - reference->first[0])) /
                length;
            if (dimension.kind == DimensionKind::DistanceSymmetric) {
                if (!dimension.second_point_id.empty()) {
                    const auto* second = next.find_point(dimension.second_point_id);
                    measured = 0.5 * (measured + std::abs(
                        reference->second[0] *
                            (second->y - reference->first[1]) -
                        reference->second[1] *
                            (second->x - reference->first[0])) / length);
                }
                measured *= 2.0;
            }
        } else if (dimension.kind == DimensionKind::AngleThreePoint) {
            const auto* first = next.find_point(dimension.first_point_id);
            const auto* vertex = next.find_point(dimension.second_point_id);
            const auto* second = next.find_point(dimension.geometry_id);
            const double ax = first->x - vertex->x;
            const double ay = first->y - vertex->y;
            const double bx = second->x - vertex->x;
            const double by = second->y - vertex->y;
            measured = std::acos(std::clamp(
                (ax * bx + ay * by) /
                    (std::hypot(ax, ay) * std::hypot(bx, by)), -1.0, 1.0)) *
                180.0 / 3.14159265358979323846;
        } else if (dimension.kind == DimensionKind::DistanceLine ||
                   dimension.kind == DimensionKind::AngleBetween) {
            const auto reference = sketch_axis_line(next, dimension.geometry_id)
                ? sketch_axis_line(next, dimension.geometry_id)
                : segment_or_external_line(next, dimension.geometry_id);
            const auto driven = segment_or_external_line(
                next, dimension.second_geometry_id);
            if (!reference || !driven) return false;
            const double rx = reference->second[0];
            const double ry = reference->second[1];
            const double dx = driven->second[0];
            const double dy = driven->second[1];
            if (dimension.kind == DimensionKind::DistanceLine) {
                measured = std::abs(
                    rx * (driven->first[1] - reference->first[1]) -
                    ry * (driven->first[0] - reference->first[0])) /
                    std::hypot(rx, ry);
            } else {
                measured = std::acos(std::clamp(
                    (rx * dx + ry * dy) /
                        (std::hypot(rx, ry) * std::hypot(dx, dy)), -1.0, 1.0)) *
                    180.0 / 3.14159265358979323846;
            }
        } else {
            const auto* first = next.find_point(dimension.first_point_id);
            const auto* second = next.find_point(dimension.second_point_id);
            const auto first_external = first == nullptr
                ? external_point_position(next, dimension.first_point_id)
                : std::nullopt;
            const auto second_external = second == nullptr
                ? external_point_position(next, dimension.second_point_id)
                : std::nullopt;
            if ((first == nullptr && !first_external) ||
                (second == nullptr && !second_external)) return false;
            const double first_x = first ? first->x : (*first_external)[0];
            const double first_y = first ? first->y : (*first_external)[1];
            const double second_x = second ? second->x : (*second_external)[0];
            const double second_y = second ? second->y : (*second_external)[1];
            const double dx = second_x - first_x;
            const double dy = second_y - first_y;
            measured = dimension.kind == DimensionKind::DistanceX ? dx
                : dimension.kind == DimensionKind::DistanceY ? dy
                : dimension.kind == DimensionKind::Angle
                    ? std::atan2(dy, dx) * 180.0 / 3.14159265358979323846
                : std::hypot(dx, dy);
        }
        if ((dimension.lower_limit && measured < *dimension.lower_limit) ||
            (dimension.upper_limit && measured > *dimension.upper_limit)) {
            return false;
        }
        dimension.value = measured;
    }
    try {
        next.validate();
    } catch (const std::exception&) {
        return false;
    }
    *this = std::move(next);
    return true;
}

bool Sketch::translate_selection(
    const std::vector<std::string>& point_ids,
    const std::vector<std::string>& geometry_ids,
    double translation_x, double translation_y) {
    if (!std::isfinite(translation_x) || !std::isfinite(translation_y)) return false;
    auto next = *this;
    std::unordered_set<std::string> selected_points(
        point_ids.begin(), point_ids.end());
    const std::unordered_set<std::string> selected_geometry(
        geometry_ids.begin(), geometry_ids.end());
    const auto include = [&](const std::string& point_id) {
        if (!point_id.empty()) selected_points.insert(point_id);
    };
    for (const auto& segment : next.segments) {
        if (!selected_geometry.contains(segment.id)) continue;
        include(segment.first_point_id);
        include(segment.second_point_id);
    }
    for (const auto& circle : next.circles) {
        if (selected_geometry.contains(circle.id)) include(circle.center_point_id);
    }
    for (const auto& arc : next.arcs) {
        if (!selected_geometry.contains(arc.id)) continue;
        include(arc.center_point_id);
        include(arc.start_point_id);
        include(arc.end_point_id);
    }
    for (const auto& ellipse : next.ellipses) {
        if (!selected_geometry.contains(ellipse.id)) continue;
        include(ellipse.center_point_id);
        include(ellipse.major_point_id);
        include(ellipse.minor_point_id);
    }
    for (const auto& arc : next.elliptical_arcs) {
        if (!selected_geometry.contains(arc.id)) continue;
        include(arc.center_point_id);
        include(arc.major_point_id);
        include(arc.minor_point_id);
        include(arc.start_point_id);
        include(arc.end_point_id);
    }
    for (const auto& spline : next.bsplines) {
        if (!selected_geometry.contains(spline.id)) continue;
        selected_points.insert(
            spline.control_point_ids.begin(), spline.control_point_ids.end());
    }
    if (selected_points.size() < 2) return false;
    const auto linked = externally_linked_point_ids(next);
    for (const auto& point_id : selected_points) {
        auto* point = next.find_point(point_id);
        if (point == nullptr || point->fixed || linked.contains(point_id)) return false;
        point->x += translation_x;
        point->y += translation_y;
    }

    std::unordered_set<std::string> relaxed_dimension_ids;
    for (auto& dimension : next.dimensions) {
        if (dimension.suppressed || !dimension.driving || dimension.locked) continue;
        relaxed_dimension_ids.insert(dimension.id);
        dimension.driving = false;
    }
    std::vector<std::string> transient_dimension_ids;
    const auto preserve_parameter = [&](DimensionKind kind,
                                        const std::string& geometry_id,
                                        double value) {
        SketchDimension dimension;
        dimension.id = "selection-drag:" + make_id();
        dimension.kind = kind;
        dimension.geometry_id = geometry_id;
        dimension.value = value;
        dimension.driving = true;
        dimension.locked = true;
        transient_dimension_ids.push_back(dimension.id);
        next.dimensions.push_back(std::move(dimension));
    };
    for (const auto& circle : next.circles) {
        if (selected_geometry.contains(circle.id))
            preserve_parameter(DimensionKind::Radius, circle.id, circle.radius);
    }
    for (const auto& arc : next.arcs) {
        if (selected_geometry.contains(arc.id))
            preserve_parameter(DimensionKind::Radius, arc.id, arc.radius);
    }
    for (const auto& ellipse : next.ellipses) {
        if (!selected_geometry.contains(ellipse.id)) continue;
        preserve_parameter(
            DimensionKind::EllipseMajorRadius, ellipse.id, ellipse.major_radius);
        preserve_parameter(
            DimensionKind::EllipseMinorRadius, ellipse.id, ellipse.minor_radius);
        preserve_parameter(DimensionKind::EllipseRotation, ellipse.id,
            ellipse.rotation * 180.0 / 3.14159265358979323846);
    }
    std::vector<std::string> preferred_points(
        selected_points.begin(), selected_points.end());
    const auto solved = next.solve_impl(100, false, preferred_points);
    if (solved.status == SolveStatus::Conflicting ||
        solved.status == SolveStatus::Invalid) return false;
    next.dimensions.erase(std::remove_if(next.dimensions.begin(), next.dimensions.end(),
        [&](const auto& dimension) {
            return std::ranges::find(transient_dimension_ids, dimension.id) !=
                transient_dimension_ids.end();
        }), next.dimensions.end());
    if (!refresh_reference_dimensions(next)) return false;
    for (auto& dimension : next.dimensions) {
        if (relaxed_dimension_ids.contains(dimension.id)) dimension.driving = true;
    }
    try {
        next.validate();
    } catch (const std::exception&) {
        return false;
    }
    *this = std::move(next);
    return true;
}

std::string Sketch::add_point(
    double x, double y, double snap_tolerance, bool construction) {
    for (const double value : {x, y, snap_tolerance}) {
        require_finite(value, "point coordinate");
    }
    if (snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch point snap tolerance is invalid");
    }
    const auto found = std::find_if(points.begin(), points.end(),
        [&](const auto& point) {
            return std::hypot(point.x - x, point.y - y) <= snap_tolerance;
        });
    if (found != points.end()) return found->id;
    auto next = *this;
    const auto external = external_snap_point(next, x, y, snap_tolerance);
    auto point = create_point(
        external ? external->second[0] : x,
        external ? external->second[1] : y);
    point.construction = construction;
    const auto point_id = point.id;
    next.points.push_back(std::move(point));
    if (external) {
        next.constraints.push_back({make_id(), ConstraintKind::Coincident,
            point_id, external->first});
    }
    next.validate();
    *this = std::move(next);
    return point_id;
}

std::string Sketch::add_segment(
    double first_x, double first_y, double second_x, double second_y,
    double snap_tolerance, bool construction,
    bool reuse_first_point, bool reuse_second_point) {
    for (const double value : {first_x, first_y, second_x, second_y, snap_tolerance}) {
        require_finite(value, "segment coordinate");
    }
    if (snap_tolerance < 0.0 ||
        std::hypot(second_x - first_x, second_y - first_y) <= 1.0e-12) {
        throw std::invalid_argument("Sketch segment length or snap tolerance is invalid");
    }
    auto next = *this;
    const auto endpoint = [&](double x, double y, bool reuse_existing) {
        if (reuse_existing) {
            const auto found = std::find_if(next.points.begin(), next.points.end(),
                [&](const auto& point) {
                    return std::hypot(point.x - x, point.y - y) <= snap_tolerance;
                });
            if (found != next.points.end()) return found->id;
        }
        const auto external = reuse_existing
            ? external_snap_point(next, x, y, snap_tolerance) : std::nullopt;
        auto point = create_point(
            external ? external->second[0] : x,
            external ? external->second[1] : y);
        const auto id = point.id;
        next.points.push_back(std::move(point));
        if (external) {
            next.constraints.push_back({make_id(), ConstraintKind::Coincident,
                id, external->first});
        }
        return id;
    };
    const auto first_id = endpoint(first_x, first_y, reuse_first_point);
    const auto second_id = endpoint(second_x, second_y, reuse_second_point);
    if (first_id == second_id) {
        throw std::invalid_argument("Sketch segment collapses to one snapped point");
    }
    auto segment = create_segment(first_id, second_id, construction);
    const auto segment_id = segment.id;
    next.segments.push_back(std::move(segment));
    next.validate();
    *this = std::move(next);
    return segment_id;
}

namespace {
void require_constraint_dof_reduction(
    const Sketch& before, const SolveResult& after,
    const char* redundant_message) {
    auto baseline = before;
    const auto baseline_result = baseline.solve();
    if (after.remaining_degrees_of_freedom >=
        baseline_result.remaining_degrees_of_freedom) {
        throw std::invalid_argument(redundant_message);
    }
}
}  // namespace

std::string Sketch::add_segment_constraint(
    const std::string& segment_id, ConstraintKind kind) {
    if (kind != ConstraintKind::Horizontal && kind != ConstraintKind::Vertical) {
        throw std::invalid_argument("Constraint is not a single-segment constraint");
    }
    const auto segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (segment == segments.end()) throw std::invalid_argument("Sketch segment does not exist");
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed && constraint.kind == kind &&
                constraint.first_point_id == segment->first_point_id &&
                constraint.second_point_id == segment->second_point_id;
        })) {
        throw std::invalid_argument("Sketch segment already owns this constraint");
    }
    auto next = *this;
    SketchConstraint constraint{
        make_id(), kind, segment->first_point_id, segment->second_point_id,
        false, segment->id};
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Sketch constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Sketch constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_point_pair_constraint(
    const std::string& reference_point_id, const std::string& driven_point_id,
    ConstraintKind kind) {
    if ((kind != ConstraintKind::Horizontal &&
         kind != ConstraintKind::Vertical) ||
        reference_point_id.empty() || driven_point_id.empty() ||
        reference_point_id == driven_point_id ||
        find_point(reference_point_id) == nullptr ||
        find_point(driven_point_id) == nullptr) {
        throw std::invalid_argument("Point-pair directional constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed && constraint.kind == kind &&
                constraint.geometry_id.empty() &&
                ((constraint.first_point_id == reference_point_id &&
                  constraint.second_point_id == driven_point_id) ||
                 (constraint.first_point_id == driven_point_id &&
                  constraint.second_point_id == reference_point_id));
        })) {
        throw std::invalid_argument("Points already own this directional constraint");
    }
    auto next = *this;
    SketchConstraint constraint{
        make_id(), kind, reference_point_id, driven_point_id};
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting ||
        result.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Point-pair directional constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Point-pair directional constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_coincident_constraint(
    const std::string& first_point_id, const std::string& second_point_id) {
    const bool first_native = find_point(first_point_id) != nullptr;
    const bool second_native = find_point(second_point_id) != nullptr;
    const bool first_external = external_point_position(*this, first_point_id).has_value();
    const bool second_external = external_point_position(*this, second_point_id).has_value();
    if (first_point_id.empty() || second_point_id.empty() ||
        first_point_id == second_point_id || (!first_native && !first_external) ||
        (!second_native && !second_external) || (!first_native && !second_native)) {
        throw std::invalid_argument("Coincident constraint points are invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::Coincident &&
                ((constraint.first_point_id == first_point_id &&
                  constraint.second_point_id == second_point_id) ||
                 (constraint.first_point_id == second_point_id &&
                  constraint.second_point_id == first_point_id));
        })) {
        throw std::invalid_argument("Points already own a coincident constraint");
    }
    auto next = *this;
    SketchConstraint constraint{
        make_id(), ConstraintKind::Coincident, first_point_id, second_point_id};
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Coincident constraint conflicts with existing geometry");
    }
    const bool curve_keypoint =
        first_point_id.starts_with("sketch_keypoint:") ||
        second_point_id.starts_with("sketch_keypoint:");
    if (!curve_keypoint) {
        require_constraint_dof_reduction(
            *this, result, "Coincident constraint is redundant");
    }
    *this = std::move(next);
    return id;
}

std::string Sketch::add_segment_pair_constraint(
    const std::string& first_segment_id, const std::string& second_segment_id,
    ConstraintKind kind) {
    if (!is_segment_pair_constraint(kind) ||
        first_segment_id.empty() || second_segment_id.empty() ||
        first_segment_id == second_segment_id) {
        throw std::invalid_argument("Segment-pair constraint input is invalid");
    }
    const auto first = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == first_segment_id; });
    const auto second = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == second_segment_id; });
    const bool direction_reference = kind != ConstraintKind::EqualLength &&
        segment_or_external_line(*this, first_segment_id).has_value();
    if ((!direction_reference && first == segments.end()) || second == segments.end()) {
        throw std::invalid_argument("Segment-pair constraint geometry does not exist");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed && constraint.kind == kind &&
                ((constraint.geometry_id == first_segment_id &&
                  constraint.second_geometry_id == second_segment_id) ||
                 (constraint.geometry_id == second_segment_id &&
                  constraint.second_geometry_id == first_segment_id));
        })) {
        throw std::invalid_argument("Segments already own this pair constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = kind;
    constraint.geometry_id = first_segment_id;
    constraint.second_geometry_id = second_segment_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Segment-pair constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Segment-pair constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_equal_radius_constraint(
    const std::string& reference_geometry_id,
    const std::string& driven_geometry_id) {
    const auto reference_radius = circular_curve_radius(
        *this, reference_geometry_id);
    const auto driven_radius = circular_curve_radius(
        *this, driven_geometry_id);
    if (reference_geometry_id.empty() || driven_geometry_id.empty() ||
        reference_geometry_id == driven_geometry_id ||
        !reference_radius || !driven_radius) {
        throw std::invalid_argument("Equal-radius constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& value) {
            return !value.suppressed &&
                value.kind == ConstraintKind::EqualRadius &&
                ((value.geometry_id == reference_geometry_id &&
                  value.second_geometry_id == driven_geometry_id) ||
                 (value.geometry_id == driven_geometry_id &&
                  value.second_geometry_id == reference_geometry_id));
        })) {
        throw std::invalid_argument(
            "Circular curves already own this equal-radius constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::EqualRadius;
    constraint.geometry_id = reference_geometry_id;
    constraint.second_geometry_id = driven_geometry_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting ||
        result.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Equal-radius constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Equal-radius constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_point_on_circle_constraint(
    const std::string& point_id, const std::string& circle_id) {
    const auto* point = find_point(point_id);
    const auto curve = tangent_curve_data(*this, circle_id);
    const auto spline = std::find_if(
        bsplines.begin(), bsplines.end(),
        [&](const auto& value) { return value.id == circle_id; });
    if (point == nullptr || (!curve && spline == bsplines.end()) ||
        (curve && curve->center_point_id == point_id) ||
        (spline != bsplines.end() &&
         std::ranges::find(spline->control_point_ids, point_id) !=
             spline->control_point_ids.end())) {
        throw std::invalid_argument("Point-on-curve constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::PointOnCircle &&
                constraint.first_point_id == point_id &&
                constraint.geometry_id == circle_id;
        })) {
        throw std::invalid_argument("Point already lies on this curve by constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::PointOnCircle;
    constraint.first_point_id = point_id;
    constraint.geometry_id = circle_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Point-on-curve constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Point-on-curve constraint is redundant");
    *this = std::move(next);
    return id;
}

std::optional<std::array<double, 2>> Sketch::project_point_to_curve(
    const std::string& geometry_id, double x, double y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    const auto target = circular_constraint_target(*this, geometry_id, x, y);
    return target ? std::optional{target->position} : std::nullopt;
}

std::optional<std::array<double, 2>> Sketch::curve_tangent_at_point(
    const std::string& geometry_id, double x, double y) const {
    const auto curve = tangent_curve_data(*this, geometry_id);
    const auto target = circular_constraint_target(*this, geometry_id, x, y);
    if (!target) return std::nullopt;
    if (!curve) {
        const auto spline = std::find_if(
            bsplines.begin(), bsplines.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        if (spline == bsplines.end()) return std::nullopt;
        if (spline->control_point_ids.size() >= 2) {
            for (const auto [endpoint_index, handle_index] : {
                    std::pair<std::size_t, std::size_t>{0, 1},
                    {spline->control_point_ids.size() - 1,
                     spline->control_point_ids.size() - 2}}) {
                const auto* endpoint = find_point(
                    spline->control_point_ids[endpoint_index]);
                const auto* handle = find_point(
                    spline->control_point_ids[handle_index]);
                if (endpoint == nullptr || handle == nullptr || std::hypot(
                        target->position[0] - endpoint->x,
                        target->position[1] - endpoint->y) > 1.0e-7) {
                    continue;
                }
                const double dx = handle->x - endpoint->x;
                const double dy = handle->y - endpoint->y;
                const double length = std::hypot(dx, dy);
                if (length > 1.0e-12)
                    return std::array{dx / length, dy / length};
            }
        }
        const auto path = sampled_bspline_points(*this, *spline);
        double best_distance = std::numeric_limits<double>::infinity();
        std::optional<std::array<double, 2>> tangent;
        for (std::size_t index = 1; index < path.size(); ++index) {
            const double dx = path[index][0] - path[index - 1][0];
            const double dy = path[index][1] - path[index - 1][1];
            const double squared = dx * dx + dy * dy;
            if (squared <= 1.0e-24) continue;
            const double parameter = std::clamp(
                ((target->position[0] - path[index - 1][0]) * dx +
                 (target->position[1] - path[index - 1][1]) * dy) / squared,
                0.0, 1.0);
            const double distance = std::hypot(
                target->position[0] -
                    (path[index - 1][0] + parameter * dx),
                target->position[1] -
                    (path[index - 1][1] + parameter * dy));
            if (distance < best_distance) {
                const double length = std::sqrt(squared);
                best_distance = distance;
                tangent = std::array{dx / length, dy / length};
            }
        }
        return tangent;
    }
    const auto* center = find_point(curve->center_point_id);
    if (center == nullptr) return std::nullopt;
    const double determinant =
        curve->major_x * curve->minor_y -
        curve->major_y * curve->minor_x;
    if (std::abs(determinant) <= 1.0e-18) return std::nullopt;
    const double relative_x = target->position[0] - center->x;
    const double relative_y = target->position[1] - center->y;
    const double cosine =
        (relative_x * curve->minor_y - relative_y * curve->minor_x) /
        determinant;
    const double sine =
        (curve->major_x * relative_y - curve->major_y * relative_x) /
        determinant;
    const std::array tangent{
        -curve->major_x * sine + curve->minor_x * cosine,
        -curve->major_y * sine + curve->minor_y * cosine};
    const double length = std::hypot(tangent[0], tangent[1]);
    if (length <= 1.0e-12) return std::nullopt;
    return std::array{tangent[0] / length, tangent[1] / length};
}

std::string Sketch::add_common_tangent_segment(
    const std::string& first_curve_id,
    const std::array<double, 2>& first_hint,
    const std::string& second_curve_id,
    const std::array<double, 2>& second_hint) {
    if (first_curve_id.empty() || second_curve_id.empty() ||
        first_curve_id == second_curve_id ||
        !std::isfinite(first_hint[0]) || !std::isfinite(first_hint[1]) ||
        !std::isfinite(second_hint[0]) || !std::isfinite(second_hint[1])) {
        throw std::invalid_argument("Common tangent input is invalid");
    }
    auto first_contact = project_point_to_curve(
        first_curve_id, first_hint[0], first_hint[1]);
    auto second_contact = project_point_to_curve(
        second_curve_id, second_hint[0], second_hint[1]);
    if (!first_contact || !second_contact ||
        std::hypot((*second_contact)[0] - (*first_contact)[0],
                   (*second_contact)[1] - (*first_contact)[1]) <= 1.0e-8) {
        throw std::invalid_argument(
            "Selected curve locations do not define a tangent segment");
    }

    const auto residual = [&](const std::array<double, 2>& first,
                              const std::array<double, 2>& second)
        -> std::optional<std::array<double, 2>> {
        const auto first_tangent = curve_tangent_at_point(
            first_curve_id, first[0], first[1]);
        const auto second_tangent = curve_tangent_at_point(
            second_curve_id, second[0], second[1]);
        const double dx = second[0] - first[0];
        const double dy = second[1] - first[1];
        const double length = std::hypot(dx, dy);
        if (!first_tangent || !second_tangent || length <= 1.0e-12) {
            return std::nullopt;
        }
        return std::array{
            dx / length * (*first_tangent)[1] -
                dy / length * (*first_tangent)[0],
            dx / length * (*second_tangent)[1] -
                dy / length * (*second_tangent)[0]};
    };
    for (unsigned iteration = 0; iteration < 32; ++iteration) {
        const auto current = residual(*first_contact, *second_contact);
        if (!current) break;
        if (std::hypot((*current)[0], (*current)[1]) <= 1.0e-10) break;
        const auto first_tangent = curve_tangent_at_point(
            first_curve_id, (*first_contact)[0], (*first_contact)[1]);
        const auto second_tangent = curve_tangent_at_point(
            second_curve_id, (*second_contact)[0], (*second_contact)[1]);
        if (!first_tangent || !second_tangent) break;
        const double chord = std::hypot(
            (*second_contact)[0] - (*first_contact)[0],
            (*second_contact)[1] - (*first_contact)[1]);
        // B-spline projection is intentionally based on a persisted sampled
        // ZIMA curve. Use a finite step large enough to cross a sample cell;
        // an analytic-conic-sized epsilon can otherwise produce a zero
        // numerical Jacobian even though a nearby tangent exists.
        const double step = std::max(1.0e-4, chord * 1.0e-3);
        const auto shifted_first = project_point_to_curve(first_curve_id,
            (*first_contact)[0] + (*first_tangent)[0] * step,
            (*first_contact)[1] + (*first_tangent)[1] * step);
        const auto shifted_second = project_point_to_curve(second_curve_id,
            (*second_contact)[0] + (*second_tangent)[0] * step,
            (*second_contact)[1] + (*second_tangent)[1] * step);
        if (!shifted_first || !shifted_second) break;
        const auto first_shift_residual = residual(
            *shifted_first, *second_contact);
        const auto second_shift_residual = residual(
            *first_contact, *shifted_second);
        if (!first_shift_residual || !second_shift_residual) break;
        const double a = ((*first_shift_residual)[0] - (*current)[0]) / step;
        const double c = ((*first_shift_residual)[1] - (*current)[1]) / step;
        const double b = ((*second_shift_residual)[0] - (*current)[0]) / step;
        const double d = ((*second_shift_residual)[1] - (*current)[1]) / step;
        const double determinant = a * d - b * c;
        if (std::abs(determinant) <= 1.0e-15) break;
        const double limit = std::max(1.0e-5, chord * 0.25);
        const double first_delta = std::clamp(
            (-(*current)[0] * d + b * (*current)[1]) / determinant,
            -limit, limit);
        const double second_delta = std::clamp(
            (-a * (*current)[1] + c * (*current)[0]) / determinant,
            -limit, limit);
        first_contact = project_point_to_curve(first_curve_id,
            (*first_contact)[0] + (*first_tangent)[0] * first_delta,
            (*first_contact)[1] + (*first_tangent)[1] * first_delta);
        second_contact = project_point_to_curve(second_curve_id,
            (*second_contact)[0] + (*second_tangent)[0] * second_delta,
            (*second_contact)[1] + (*second_tangent)[1] * second_delta);
        if (!first_contact || !second_contact) break;
    }
    const auto final_residual = first_contact && second_contact
        ? residual(*first_contact, *second_contact) : std::nullopt;
    if (!final_residual ||
        std::hypot((*final_residual)[0], (*final_residual)[1]) > 1.0e-5) {
        throw std::invalid_argument(
            "No common tangent exists near the selected curve locations");
    }

    // Build and solve the four persistent relations on a copy. A failure in
    // any one of them therefore leaves the original Sketch unchanged.
    auto next = *this;
    const auto segment_id = next.add_segment(
        (*first_contact)[0], (*first_contact)[1],
        (*second_contact)[0], (*second_contact)[1], 1.0e-9);
    const auto segment = std::find_if(next.segments.begin(), next.segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (segment == next.segments.end()) {
        throw std::runtime_error("Common tangent segment was not created");
    }
    const auto first_point_id = segment->first_point_id;
    const auto second_point_id = segment->second_point_id;
    static_cast<void>(next.add_point_on_circle_constraint(
        first_point_id, first_curve_id));
    static_cast<void>(next.add_point_on_circle_constraint(
        second_point_id, second_curve_id));
    static_cast<void>(next.add_tangent_constraint(first_curve_id, segment_id));
    static_cast<void>(next.add_tangent_constraint(second_curve_id, segment_id));
    next.validate();
    *this = std::move(next);
    return segment_id;
}

std::vector<std::array<double, 2>> Sketch::curve_line_intersections(
    const std::string& geometry_id,
    const std::array<double, 2>& line_origin,
    const std::array<double, 2>& line_direction,
    bool line_bounded) const {
    std::vector<std::array<double, 2>> result;
    const double line_squared = line_direction[0] * line_direction[0] +
        line_direction[1] * line_direction[1];
    if (line_squared <= 1.0e-24) return result;
    const auto append_unique = [&](const std::array<double, 2>& point) {
        if (std::none_of(result.begin(), result.end(), [&](const auto& value) {
                return std::hypot(value[0] - point[0], value[1] - point[1]) <=
                    1.0e-8;
            })) result.push_back(point);
    };
    if (const auto curve = tangent_curve_data(*this, geometry_id)) {
        const auto* center = find_point(curve->center_point_id);
        if (center == nullptr) return result;
        const double determinant = curve->major_x * curve->minor_y -
            curve->major_y * curve->minor_x;
        if (std::abs(determinant) <= 1.0e-18) return result;
        const double offset_x = line_origin[0] - center->x;
        const double offset_y = line_origin[1] - center->y;
        const double local_u =
            (offset_x * curve->minor_y - offset_y * curve->minor_x) /
            determinant;
        const double local_v =
            (curve->major_x * offset_y - curve->major_y * offset_x) /
            determinant;
        const double direction_u =
            (line_direction[0] * curve->minor_y -
             line_direction[1] * curve->minor_x) / determinant;
        const double direction_v =
            (curve->major_x * line_direction[1] -
             curve->major_y * line_direction[0]) / determinant;
        const double a = direction_u * direction_u + direction_v * direction_v;
        const double b = 2.0 * (local_u * direction_u + local_v * direction_v);
        const double c = local_u * local_u + local_v * local_v - 1.0;
        const double discriminant = b * b - 4.0 * a * c;
        if (a <= 1.0e-24 || discriminant < -1.0e-12) return result;
        const double root = std::sqrt(std::max(0.0, discriminant));
        for (const double factor : {
                (-b - root) / (2.0 * a), (-b + root) / (2.0 * a)}) {
            if (line_bounded &&
                (factor < -1.0e-12 || factor > 1.0 + 1.0e-12)) continue;
            const double u = local_u + factor * direction_u;
            const double v = local_v + factor * direction_v;
            if (curve->parameter_domain) {
                constexpr double full_turn = 2.0 * 3.14159265358979323846;
                double relative = std::fmod(
                    std::atan2(v, u) - curve->parameter_domain->first,
                    full_turn);
                if (relative < 0.0) relative += full_turn;
                if (relative > curve->parameter_domain->second -
                        curve->parameter_domain->first + 1.0e-8) continue;
            }
            append_unique({line_origin[0] + factor * line_direction[0],
                           line_origin[1] + factor * line_direction[1]});
        }
        return result;
    }
    const auto spline = std::find_if(
        bsplines.begin(), bsplines.end(),
        [&](const auto& value) { return value.id == geometry_id; });
    if (spline == bsplines.end()) return result;
    const auto path = sampled_bspline_points(*this, *spline);
    for (std::size_t index = 1; index < path.size(); ++index) {
        const std::array curve_direction{
            path[index][0] - path[index - 1][0],
            path[index][1] - path[index - 1][1]};
        const double denominator =
            line_direction[0] * curve_direction[1] -
            line_direction[1] * curve_direction[0];
        if (std::abs(denominator) <= 1.0e-18) continue;
        const double offset_x = path[index - 1][0] - line_origin[0];
        const double offset_y = path[index - 1][1] - line_origin[1];
        const double line_parameter =
            (offset_x * curve_direction[1] -
             offset_y * curve_direction[0]) / denominator;
        const double curve_parameter =
            (offset_x * line_direction[1] -
             offset_y * line_direction[0]) / denominator;
        if ((line_bounded &&
                (line_parameter < -1.0e-12 || line_parameter > 1.0 + 1.0e-12)) ||
            curve_parameter < -1.0e-12 || curve_parameter > 1.0 + 1.0e-12) {
            continue;
        }
        append_unique({line_origin[0] + line_parameter * line_direction[0],
                       line_origin[1] + line_parameter * line_direction[1]});
    }
    return result;
}

std::string Sketch::add_point_on_line_constraint(
    const std::string& point_id, const std::string& line_id) {
    const auto* point = find_point(point_id);
    if (point == nullptr ||
        !point_on_line_target(*this, line_id, point->x, point->y)) {
        throw std::invalid_argument("Point-on-line constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& value) {
            return !value.suppressed && value.kind == ConstraintKind::PointOnLine &&
                value.first_point_id == point_id && value.geometry_id == line_id;
        })) {
        throw std::invalid_argument("Point already lies on this line");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::PointOnLine;
    constraint.first_point_id = point_id;
    constraint.geometry_id = line_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting ||
        result.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Point-on-line constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Point-on-line constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_midpoint_on_line_constraint(
    const std::string& segment_id, const std::string& line_id) {
    const auto segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (segment == segments.end() || segment_id == line_id ||
        !segment_or_external_line(*this, line_id)) {
        throw std::invalid_argument(
            "Midpoint-on-line constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(),
            [&](const auto& value) {
                return !value.suppressed &&
                    value.kind == ConstraintKind::MidpointOnLine &&
                    value.geometry_id == segment_id &&
                    value.second_geometry_id == line_id;
            })) {
        throw std::invalid_argument(
            "Segment midpoint already lies on this line");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::MidpointOnLine;
    constraint.geometry_id = segment_id;
    constraint.second_geometry_id = line_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting ||
        result.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Midpoint-on-line constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Midpoint-on-line constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_symmetric_constraint(
    const std::string& source_point_id,
    const std::string& mirrored_point_id,
    const std::string& axis_id) {
    if (source_point_id.empty() || mirrored_point_id.empty() ||
        source_point_id == mirrored_point_id ||
        find_point(source_point_id) == nullptr ||
        find_point(mirrored_point_id) == nullptr ||
        !sketch_axis_line(*this, axis_id)) {
        throw std::invalid_argument("Symmetric constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::Symmetric &&
                ((constraint.first_point_id == source_point_id &&
                  constraint.second_point_id == mirrored_point_id) ||
                 (constraint.first_point_id == mirrored_point_id &&
                  constraint.second_point_id == source_point_id)) &&
                constraint.geometry_id == axis_id;
        })) {
        throw std::invalid_argument("Points already own this symmetric constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::Symmetric;
    constraint.first_point_id = source_point_id;
    constraint.second_point_id = mirrored_point_id;
    constraint.geometry_id = axis_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Symmetric constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Symmetric constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_midpoint_constraint(
    const std::string& point_id, const std::string& segment_id) {
    const auto* point = find_point(point_id);
    const auto segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (point == nullptr || segment == segments.end() ||
        segment->first_point_id == point_id || segment->second_point_id == point_id) {
        throw std::invalid_argument("Midpoint constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::Midpoint &&
                constraint.first_point_id == point_id &&
                constraint.geometry_id == segment_id;
        })) {
        throw std::invalid_argument("Point already owns this midpoint constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::Midpoint;
    constraint.first_point_id = point_id;
    constraint.geometry_id = segment_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Midpoint constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Midpoint constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_concentric_constraint(
    const std::string& reference_geometry_id,
    const std::string& driven_geometry_id) {
    const auto reference_center = center_curve_point_id(*this, reference_geometry_id);
    const auto driven_center = center_curve_point_id(*this, driven_geometry_id);
    if (!reference_center || !driven_center ||
        reference_geometry_id == driven_geometry_id ||
        *reference_center == *driven_center) {
        throw std::invalid_argument("Concentric constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::Concentric &&
                ((constraint.geometry_id == reference_geometry_id &&
                  constraint.second_geometry_id == driven_geometry_id) ||
                 (constraint.geometry_id == driven_geometry_id &&
                  constraint.second_geometry_id == reference_geometry_id));
        })) {
        throw std::invalid_argument("Curves already own this concentric constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::Concentric;
    constraint.geometry_id = reference_geometry_id;
    constraint.second_geometry_id = driven_geometry_id;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Concentric constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Concentric constraint is redundant");
    *this = std::move(next);
    return id;
}

std::string Sketch::add_tangent_constraint(
    const std::string& reference_geometry_id,
    const std::string& driven_geometry_id,
    const std::string& contact_point_id) {
    const bool reference_is_segment = std::any_of(
        segments.begin(), segments.end(), [&](const auto& value) {
            return value.id == reference_geometry_id;
        });
    const bool driven_is_segment = std::any_of(
        segments.begin(), segments.end(), [&](const auto& value) {
            return value.id == driven_geometry_id;
        });
    const bool reference_is_axis = is_base_sketch_axis(reference_geometry_id);
    const bool driven_is_axis = is_base_sketch_axis(driven_geometry_id);
    const bool reference_is_line = reference_is_segment || reference_is_axis;
    const bool driven_is_line = driven_is_segment || driven_is_axis;
    const auto reference_curve = tangent_curve_data(*this, reference_geometry_id);
    const auto driven_curve = tangent_curve_data(*this, driven_geometry_id);
    const bool reference_spline = std::ranges::any_of(bsplines,
        [&](const auto& value) { return value.id == reference_geometry_id; });
    const bool driven_spline = std::ranges::any_of(bsplines,
        [&](const auto& value) { return value.id == driven_geometry_id; });
    const bool line_curve = reference_is_line != driven_is_line &&
        (reference_is_line
            ? static_cast<bool>(driven_curve) || driven_spline
            : static_cast<bool>(reference_curve) || reference_spline);
    const bool curve_pair =
        !reference_is_line && !driven_is_line &&
        reference_curve && driven_curve;
    if (reference_geometry_id.empty() || driven_geometry_id.empty() ||
        reference_geometry_id == driven_geometry_id ||
        (!contact_point_id.empty() && find_point(contact_point_id) == nullptr) ||
        (!line_curve && !curve_pair)) {
        throw std::invalid_argument("Tangent constraint input is invalid");
    }
    bool tangent_internal = false;
    if (line_curve) {
        const std::string& segment_id = reference_is_line
            ? reference_geometry_id : driven_geometry_id;
        const std::string& curve_id = reference_is_line
            ? driven_geometry_id : reference_geometry_id;
        const bool spline_curve = std::ranges::any_of(bsplines,
            [&](const auto& value) { return value.id == curve_id; });
        const auto state = spline_curve
            ? std::optional<SegmentCurveTangentState>{}
            : segment_curve_tangent_state(*this, segment_id, curve_id);
        const auto spline_state = spline_curve && !reference_is_axis && !driven_is_axis
            ? segment_spline_tangent_state(*this, segment_id, curve_id)
            : std::optional<SegmentSplineTangentState>{};
        if ((!spline_curve &&
                (!state || !state->contact_on_segment || !state->contact_on_curve)) ||
            (spline_curve && !spline_state)) {
            throw std::invalid_argument(
                "Tangent contact lies outside the selected segment or curve domain");
        }
    } else if (reference_curve->circular_radius &&
               driven_curve->circular_radius) {
        const auto external = curve_pair_tangent_state(
            *this, reference_geometry_id, driven_geometry_id, false);
        const auto internal = curve_pair_tangent_state(
            *this, reference_geometry_id, driven_geometry_id, true);
        const bool external_valid = external && external->contact_on_reference &&
            external->contact_on_driven;
        const bool internal_valid = internal && internal->contact_on_reference &&
            internal->contact_on_driven;
        if (!external_valid && !internal_valid) {
            throw std::invalid_argument(
                "Tangent contact lies outside one of the selected curve domains");
        }
        tangent_internal = internal_valid && (!external_valid ||
            std::abs(internal->target_distance - internal->center_distance) <
                std::abs(external->target_distance - external->center_distance));
    } else {
        const auto state = general_curve_pair_tangent_state(
            *this, reference_geometry_id, driven_geometry_id);
        if (!state || state->driven_point_ids.empty() ||
            (state->distance <= 1.0e-8 && !state->tangents_parallel)) {
            throw std::invalid_argument(
                "Selected curves do not own a valid tangential contact");
        }
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::Tangent &&
                ((constraint.geometry_id == reference_geometry_id &&
                  constraint.second_geometry_id == driven_geometry_id) ||
                 (constraint.geometry_id == driven_geometry_id &&
                  constraint.second_geometry_id == reference_geometry_id));
        })) {
        throw std::invalid_argument("Geometry already owns this tangent constraint");
    }
    auto next = *this;
    SketchConstraint constraint;
    constraint.id = make_id();
    constraint.kind = ConstraintKind::Tangent;
    constraint.first_point_id = contact_point_id;
    constraint.geometry_id = reference_geometry_id;
    constraint.second_geometry_id = driven_geometry_id;
    constraint.tangent_internal = tangent_internal;
    const auto id = constraint.id;
    next.constraints.push_back(std::move(constraint));
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Tangent constraint conflicts with existing geometry");
    }
    require_constraint_dof_reduction(
        *this, result, "Tangent constraint is redundant");
    *this = std::move(next);
    return id;
}

void Sketch::remove_constraint(const std::string& constraint_id) {
    auto next = *this;
    const auto old_size = next.constraints.size();
    std::erase_if(next.constraints, [&](const auto& constraint) {
        return constraint.id == constraint_id;
    });
    if (next.constraints.size() == old_size) {
        throw std::invalid_argument("Sketch constraint does not exist");
    }
    next.validate();
    // Removing a constraint changes rank, but this transaction only needs to
    // prove that the remaining equations are valid. A later explicit solve
    // reports the newly available DOF when the UI requests it.
    const auto solved = next.solve_impl(100, false);
    if (solved.status == SolveStatus::Invalid ||
        solved.status == SolveStatus::Conflicting) {
        throw std::runtime_error("Sketch is invalid after constraint removal");
    }
    *this = std::move(next);
}

void Sketch::remove_dimension(const std::string& dimension_id) {
    auto next = *this;
    const auto old_size = next.dimensions.size();
    std::erase_if(next.dimensions, [&](const auto& dimension) {
        return dimension.id == dimension_id;
    });
    if (next.dimensions.size() == old_size) {
        throw std::invalid_argument("Sketch dimension does not exist");
    }
    next.validate();
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Invalid ||
        solved.status == SolveStatus::Conflicting) {
        throw std::runtime_error("Sketch is invalid after dimension removal");
    }
    *this = std::move(next);
}

void Sketch::remove_geometry(const std::string& geometry_id) {
    if (geometry_id.empty()) throw std::invalid_argument("Geometry ID is required");
    auto next = *this;
    if (std::any_of(next.external_references.begin(), next.external_references.end(),
            [&](const auto& value) { return value.id == geometry_id; })) {
        const std::string source_path = "external-reference:" + geometry_id;
        const auto block = std::find_if(next.import_blocks.begin(),
            next.import_blocks.end(), [&](const auto& value) {
                return value.source_path == source_path;
            });
        if (block != next.import_blocks.end()) {
            const auto linked_geometry_ids = block->geometry_ids;
            next.import_blocks.erase(block);
            for (const auto& linked_geometry_id : linked_geometry_ids) {
                next.remove_geometry(linked_geometry_id);
            }
            next.remove_geometry(geometry_id);
            *this = std::move(next);
            return;
        }
    }
    std::vector<std::string> candidate_point_ids;
    const auto collect = [&](const std::string& point_id) {
        if (std::find(candidate_point_ids.begin(), candidate_point_ids.end(), point_id) ==
            candidate_point_ids.end()) {
            candidate_point_ids.push_back(point_id);
        }
    };
    if (const auto found = std::find_if(next.segments.begin(), next.segments.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.segments.end()) {
        collect(found->first_point_id);
        collect(found->second_point_id);
    }
    if (const auto found = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.circles.end()) {
        collect(found->center_point_id);
    }
    if (const auto found = std::find_if(next.arcs.begin(), next.arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.arcs.end()) {
        collect(found->center_point_id);
        collect(found->start_point_id);
        collect(found->end_point_id);
    }
    if (const auto found = std::find_if(next.ellipses.begin(), next.ellipses.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.ellipses.end()) {
        collect(found->center_point_id);
        collect(found->major_point_id);
        collect(found->minor_point_id);
    }
    if (const auto found = std::find_if(
            next.elliptical_arcs.begin(), next.elliptical_arcs.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.elliptical_arcs.end()) {
        collect(found->center_point_id);
        collect(found->major_point_id);
        collect(found->minor_point_id);
        collect(found->start_point_id);
        collect(found->end_point_id);
    }
    if (const auto found = std::find_if(next.bsplines.begin(), next.bsplines.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        found != next.bsplines.end()) {
        for (const auto& point_id : found->control_point_ids) collect(point_id);
    }
    const auto segment_count = std::erase_if(next.segments,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto circle_count = std::erase_if(next.circles,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto arc_count = std::erase_if(next.arcs,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto ellipse_count = std::erase_if(next.ellipses,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto elliptical_arc_count = std::erase_if(next.elliptical_arcs,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto spline_count = std::erase_if(next.bsplines,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto text_count = std::erase_if(next.texts,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto external_reference_count = std::erase_if(next.external_references,
        [&](const auto& value) { return value.id == geometry_id; });
    const auto corner_radius_count = std::erase_if(next.corner_radii,
        [&](const auto& value) {
        return value.id == geometry_id ||
            value.first_segment_id == geometry_id ||
            value.second_segment_id == geometry_id;
    });
    if (segment_count + circle_count + arc_count + ellipse_count +
            elliptical_arc_count + spline_count + text_count +
            external_reference_count +
            (corner_radius_count != 0 && segment_count == 0 ? 1 : 0) != 1) {
        throw std::invalid_argument("Sketch geometry does not exist");
    }
    const auto point_has_geometry_owner = [&](const std::string& point_id) {
        return std::any_of(next.segments.begin(), next.segments.end(), [&](const auto& value) {
                return value.first_point_id == point_id || value.second_point_id == point_id;
            }) || std::any_of(next.circles.begin(), next.circles.end(), [&](const auto& value) {
                return value.center_point_id == point_id;
            }) || std::any_of(next.arcs.begin(), next.arcs.end(), [&](const auto& value) {
                return value.center_point_id == point_id ||
                    value.start_point_id == point_id || value.end_point_id == point_id;
            }) || std::any_of(next.ellipses.begin(), next.ellipses.end(),
                [&](const auto& value) {
                    return value.center_point_id == point_id ||
                        value.major_point_id == point_id || value.minor_point_id == point_id;
            }) || std::any_of(next.elliptical_arcs.begin(), next.elliptical_arcs.end(),
                [&](const auto& value) {
                    return value.center_point_id == point_id ||
                        value.major_point_id == point_id ||
                        value.minor_point_id == point_id ||
                        value.start_point_id == point_id ||
                        value.end_point_id == point_id;
            }) || std::any_of(next.bsplines.begin(), next.bsplines.end(),
                [&](const auto& value) {
                    return std::find(value.control_point_ids.begin(),
                                     value.control_point_ids.end(), point_id) !=
                        value.control_point_ids.end();
            });
    };
    const auto removed_unshared_point = [&](const std::string& point_id) {
        return !point_id.empty() &&
            std::find(candidate_point_ids.begin(), candidate_point_ids.end(), point_id) !=
                candidate_point_ids.end() &&
            !point_has_geometry_owner(point_id);
    };
    std::erase_if(next.constraints,
        [&](const auto& value) {
            return value.geometry_id == geometry_id ||
                value.second_geometry_id == geometry_id ||
                (value.second_point_id.starts_with("sketch_keypoint:") &&
                 value.second_point_id.find(":" + geometry_id + ":") !=
                     std::string::npos) ||
                (external_reference_count != 0 &&
                 (value.first_point_id == geometry_id ||
                  value.second_point_id == geometry_id)) ||
                removed_unshared_point(value.first_point_id) ||
                removed_unshared_point(value.second_point_id);
        });
    std::erase_if(next.dimensions,
        [&](const auto& value) {
            return value.geometry_id == geometry_id ||
                value.second_geometry_id == geometry_id ||
                (external_reference_count != 0 &&
                 (value.first_point_id == geometry_id ||
                  value.second_point_id == geometry_id));
        });
    const auto point_is_referenced = [&](const std::string& point_id) {
        return std::any_of(next.segments.begin(), next.segments.end(), [&](const auto& value) {
                return value.first_point_id == point_id || value.second_point_id == point_id;
            }) || std::any_of(next.circles.begin(), next.circles.end(), [&](const auto& value) {
                return value.center_point_id == point_id;
            }) || std::any_of(next.arcs.begin(), next.arcs.end(), [&](const auto& value) {
                return value.center_point_id == point_id ||
                    value.start_point_id == point_id || value.end_point_id == point_id;
            }) || std::any_of(next.ellipses.begin(), next.ellipses.end(),
                [&](const auto& value) {
                    return value.center_point_id == point_id ||
                        value.major_point_id == point_id ||
                        value.minor_point_id == point_id;
            }) || std::any_of(next.elliptical_arcs.begin(), next.elliptical_arcs.end(),
                [&](const auto& value) {
                    return value.center_point_id == point_id ||
                        value.major_point_id == point_id ||
                        value.minor_point_id == point_id ||
                        value.start_point_id == point_id ||
                        value.end_point_id == point_id;
            }) || std::any_of(next.bsplines.begin(), next.bsplines.end(),
                [&](const auto& value) {
                    return std::find(value.control_point_ids.begin(),
                                     value.control_point_ids.end(), point_id) !=
                        value.control_point_ids.end();
            }) || std::any_of(next.constraints.begin(), next.constraints.end(),
                [&](const auto& value) {
                    return value.first_point_id == point_id || value.second_point_id == point_id;
            }) || std::any_of(next.dimensions.begin(), next.dimensions.end(),
                [&](const auto& value) {
                    return value.first_point_id == point_id || value.second_point_id == point_id;
                }) || std::any_of(next.import_blocks.begin(), next.import_blocks.end(),
                [&](const auto& value) {
                    return std::find(value.point_ids.begin(), value.point_ids.end(), point_id) !=
                        value.point_ids.end();
                });
    };
    std::erase_if(next.points,
        [&](const auto& point) {
            return std::find(candidate_point_ids.begin(), candidate_point_ids.end(), point.id) !=
                    candidate_point_ids.end() &&
                !point_is_referenced(point.id);
        });
    next.validate();
    *this = std::move(next);
}

void Sketch::remove_point(const std::string& point_id) {
    if (point_id.empty() || find_point(point_id) == nullptr) {
        throw std::invalid_argument("Sketch point does not exist");
    }
    auto next = *this;
    std::erase_if(next.corner_radii,
        [&](const auto& value) { return value.vertex_id == point_id; });
    std::set<std::string> removed_geometry_ids;
    std::set<std::string> candidate_point_ids{point_id};
    const auto collect_geometry = [&](const std::string& geometry_id,
                                      const auto& point_ids) {
        removed_geometry_ids.insert(geometry_id);
        candidate_point_ids.insert(point_ids.begin(), point_ids.end());
    };
    for (const auto& segment : next.segments) {
        if (segment.first_point_id == point_id || segment.second_point_id == point_id) {
            collect_geometry(segment.id,
                std::array{segment.first_point_id, segment.second_point_id});
        }
    }
    for (const auto& circle : next.circles) {
        if (circle.center_point_id == point_id) {
            collect_geometry(circle.id, std::array{circle.center_point_id});
        }
    }
    for (const auto& arc : next.arcs) {
        if (arc.center_point_id == point_id || arc.start_point_id == point_id ||
            arc.end_point_id == point_id) {
            collect_geometry(arc.id,
                std::array{arc.center_point_id, arc.start_point_id, arc.end_point_id});
        }
    }
    for (const auto& ellipse : next.ellipses) {
        if (ellipse.center_point_id == point_id || ellipse.major_point_id == point_id ||
            ellipse.minor_point_id == point_id) {
            collect_geometry(ellipse.id,
                std::array{ellipse.center_point_id, ellipse.major_point_id,
                           ellipse.minor_point_id});
        }
    }
    for (const auto& arc : next.elliptical_arcs) {
        if (arc.center_point_id == point_id || arc.major_point_id == point_id ||
            arc.minor_point_id == point_id || arc.start_point_id == point_id ||
            arc.end_point_id == point_id) {
            collect_geometry(arc.id,
                std::array{arc.center_point_id, arc.major_point_id,
                           arc.minor_point_id, arc.start_point_id,
                           arc.end_point_id});
        }
    }
    for (const auto& spline : next.bsplines) {
        if (std::find(spline.control_point_ids.begin(), spline.control_point_ids.end(),
                      point_id) != spline.control_point_ids.end()) {
            collect_geometry(spline.id, spline.control_point_ids);
        }
    }
    if (std::any_of(next.import_blocks.begin(), next.import_blocks.end(),
            [&](const auto& block) {
                return std::find(block.point_ids.begin(), block.point_ids.end(), point_id) !=
                        block.point_ids.end() ||
                    std::any_of(block.geometry_ids.begin(), block.geometry_ids.end(),
                        [&](const auto& geometry_id) {
                            return removed_geometry_ids.contains(geometry_id);
                        });
            })) {
        throw std::invalid_argument(
            "A point owned by an imported Sketch block cannot be removed separately");
    }
    const auto geometry_removed = [&](const std::string& geometry_id) {
        return removed_geometry_ids.contains(geometry_id);
    };
    std::erase_if(next.segments,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.circles,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.arcs,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.ellipses,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.elliptical_arcs,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.bsplines,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.constraints, [&](const auto& value) {
        return value.first_point_id == point_id || value.second_point_id == point_id ||
            geometry_removed(value.geometry_id) ||
            geometry_removed(value.second_geometry_id);
    });
    std::erase_if(next.dimensions, [&](const auto& value) {
        return value.first_point_id == point_id || value.second_point_id == point_id ||
            geometry_removed(value.geometry_id) ||
            geometry_removed(value.second_geometry_id);
    });
    const auto point_is_referenced = [&](const std::string& candidate_id) {
        return std::any_of(next.segments.begin(), next.segments.end(), [&](const auto& value) {
                return value.first_point_id == candidate_id ||
                    value.second_point_id == candidate_id;
            }) || std::any_of(next.circles.begin(), next.circles.end(), [&](const auto& value) {
                return value.center_point_id == candidate_id;
            }) || std::any_of(next.arcs.begin(), next.arcs.end(), [&](const auto& value) {
                return value.center_point_id == candidate_id ||
                    value.start_point_id == candidate_id || value.end_point_id == candidate_id;
            }) || std::any_of(next.ellipses.begin(), next.ellipses.end(),
                [&](const auto& value) {
                    return value.center_point_id == candidate_id ||
                        value.major_point_id == candidate_id ||
                        value.minor_point_id == candidate_id;
                }) || std::any_of(
                next.elliptical_arcs.begin(), next.elliptical_arcs.end(),
                [&](const auto& value) {
                    return value.center_point_id == candidate_id ||
                        value.major_point_id == candidate_id ||
                        value.minor_point_id == candidate_id ||
                        value.start_point_id == candidate_id ||
                        value.end_point_id == candidate_id;
                }) || std::any_of(next.bsplines.begin(), next.bsplines.end(),
                [&](const auto& value) {
                    return std::find(value.control_point_ids.begin(),
                                     value.control_point_ids.end(), candidate_id) !=
                        value.control_point_ids.end();
                }) || std::any_of(next.constraints.begin(), next.constraints.end(),
                [&](const auto& value) {
                    return value.first_point_id == candidate_id ||
                        value.second_point_id == candidate_id;
                }) || std::any_of(next.dimensions.begin(), next.dimensions.end(),
                [&](const auto& value) {
                    return value.first_point_id == candidate_id ||
                        value.second_point_id == candidate_id;
                });
    };
    std::erase_if(next.points, [&](const auto& point) {
        return point.id == point_id ||
            (candidate_point_ids.contains(point.id) && !point_is_referenced(point.id));
    });
    next.validate();
    *this = std::move(next);
}

std::vector<std::string> Sketch::add_rectangle(
    double first_x, double first_y, double second_x, double second_y,
    double snap_tolerance) {
    for (const double value : {first_x, first_y, second_x, second_y, snap_tolerance}) {
        require_finite(value, "rectangle coordinate");
    }
    if (snap_tolerance < 0.0 || std::abs(second_x - first_x) <= 1.0e-12 ||
        std::abs(second_y - first_y) <= 1.0e-12) {
        throw std::invalid_argument("Sketch rectangle width and height must be non-zero");
    }
    auto next = *this;
    std::vector<std::string> ids;
    ids.push_back(next.add_segment(
        first_x, first_y, second_x, first_y, snap_tolerance));
    ids.push_back(next.add_segment(
        second_x, first_y, second_x, second_y, snap_tolerance));
    ids.push_back(next.add_segment(
        second_x, second_y, first_x, second_y, snap_tolerance));
    ids.push_back(next.add_segment(
        first_x, second_y, first_x, first_y, snap_tolerance));
    // A rectangle is a progressively driven point graph, not four separately
    // oriented lines.  Keeping the directional relations point-based makes
    // the first corner the stable reference and lets the remaining corners
    // follow it in the same order in which the interactive tool creates them.
    // It also avoids silently over-constraining a side later when either end
    // is attached to another support geometry.
    const auto segment_by_id = [&](const std::string& id) -> const SketchSegment& {
        return *std::find_if(next.segments.begin(), next.segments.end(),
            [&](const auto& value) { return value.id == id; });
    };
    const auto first = segment_by_id(ids[0]);
    const auto second = segment_by_id(ids[1]);
    const auto third = segment_by_id(ids[2]);
    static_cast<void>(next.add_point_pair_constraint(
        first.first_point_id, first.second_point_id,
        ConstraintKind::Horizontal));
    static_cast<void>(next.add_point_pair_constraint(
        first.second_point_id, second.second_point_id,
        ConstraintKind::Vertical));
    static_cast<void>(next.add_point_pair_constraint(
        second.second_point_id, third.second_point_id,
        ConstraintKind::Horizontal));
    static_cast<void>(next.add_point_pair_constraint(
        first.first_point_id, third.second_point_id,
        ConstraintKind::Vertical));
    next.validate();
    *this = std::move(next);
    return ids;
}

std::vector<std::string> Sketch::add_oriented_rectangle(
    double first_x, double first_y, double guide_x, double guide_y,
    const std::string& symmetry_axis_id, double snap_tolerance) {
    for (const double value : {
            first_x, first_y, guide_x, guide_y, snap_tolerance}) {
        require_finite(value, "oriented rectangle coordinate");
    }
    if (snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch snap tolerance must not be negative");
    }
    const auto axis = sketch_axis_line(*this, symmetry_axis_id);
    const auto axis_segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) {
            return value.id == symmetry_axis_id && value.construction;
        });
    const bool base_axis = symmetry_axis_id == "sketch_axis:x" ||
        symmetry_axis_id == "sketch_axis:y";
    if (!axis || (!base_axis && axis_segment == segments.end())) {
        throw std::invalid_argument(
            "Oriented rectangle requires a Sketch axis or construction-axis segment");
    }
    const double ax = axis->second[0];
    const double ay = axis->second[1];
    const double axis_length = std::hypot(ax, ay);
    const double ux = ax / axis_length;
    const double uy = ay / axis_length;
    const double length = (guide_x - first_x) * ux +
        (guide_y - first_y) * uy;
    const double projection =
        (first_x - axis->first[0]) * ux +
        (first_y - axis->first[1]) * uy;
    const std::array foot{
        axis->first[0] + projection * ux,
        axis->first[1] + projection * uy};
    const std::array mirrored{
        2.0 * foot[0] - first_x,
        2.0 * foot[1] - first_y};
    if (std::abs(length) <= 1.0e-12 ||
        std::hypot(mirrored[0] - first_x, mirrored[1] - first_y) <= 1.0e-12) {
        throw std::invalid_argument(
            "Oriented rectangle length and width must be non-zero");
    }
    const std::array far_first{
        first_x + length * ux, first_y + length * uy};
    const std::array far_mirrored{
        mirrored[0] + length * ux, mirrored[1] + length * uy};

    auto next = *this;
    const std::array<std::array<double, 2>, 4> corners{{
        {first_x, first_y}, far_first, far_mirrored, mirrored}};
    std::array<std::string, 4> point_ids;
    for (std::size_t index = 0; index < corners.size(); ++index) {
        point_ids[index] = next.add_point(
            corners[index][0], corners[index][1], snap_tolerance);
    }
    if (std::unordered_set<std::string>(point_ids.begin(), point_ids.end()).size() != 4) {
        throw std::invalid_argument(
            "Oriented rectangle corners collapse onto existing Sketch points");
    }
    std::vector<std::string> ids;
    ids.reserve(4);
    for (std::size_t index = 0; index < point_ids.size(); ++index) {
        auto segment = Sketch::create_segment(
            point_ids[index], point_ids[(index + 1) % point_ids.size()]);
        ids.push_back(segment.id);
        next.segments.push_back(std::move(segment));
    }
    static_cast<void>(next.add_symmetric_constraint(
        point_ids[0], point_ids[3], symmetry_axis_id));
    static_cast<void>(next.add_symmetric_constraint(
        point_ids[1], point_ids[2], symmetry_axis_id));
    static_cast<void>(next.add_segment_pair_constraint(
        symmetry_axis_id, ids[0], ConstraintKind::Parallel));
    next.validate();
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Conflicting ||
        solved.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Oriented rectangle conflicts with existing Sketch geometry");
    }
    *this = std::move(next);
    return ids;
}

RegularPolygonResult Sketch::add_regular_polygon(
    double center_x, double center_y, double rim_x, double rim_y,
    unsigned sides, double snap_tolerance) {
    for (const double value : {center_x, center_y, rim_x, rim_y, snap_tolerance}) {
        require_finite(value, "regular polygon parameter");
    }
    const double radius = std::hypot(rim_x - center_x, rim_y - center_y);
    if ((sides != 4 && sides != 6 && sides != 8) ||
        radius <= 1.0e-12 || snap_tolerance < 0.0) {
        throw std::invalid_argument(
            "Regular polygon requires 4, 6, or 8 sides and a positive radius");
    }
    auto next = *this;
    RegularPolygonResult result;
    result.support_circle_id = next.add_circle(
        center_x, center_y, radius, true, snap_tolerance);
    const auto support = std::find_if(next.circles.begin(), next.circles.end(),
        [&](const auto& value) { return value.id == result.support_circle_id; });
    const auto* center = next.find_point(support->center_point_id);
    const double actual_center_x = center->x;
    const double actual_center_y = center->y;
    const double start_angle = std::atan2(
        rim_y - actual_center_y, rim_x - actual_center_x);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    std::vector<std::array<double, 2>> vertices;
    vertices.reserve(sides);
    result.vertex_ids.reserve(sides);
    for (unsigned index = 0; index < sides; ++index) {
        const double angle = start_angle + full_turn *
            static_cast<double>(index) / static_cast<double>(sides);
        const std::array<double, 2> vertex{
            actual_center_x + radius * std::cos(angle),
            actual_center_y + radius * std::sin(angle)};
        vertices.push_back(vertex);
        result.vertex_ids.push_back(next.add_point(
            vertex[0], vertex[1], snap_tolerance));
    }
    std::unordered_set<std::string> unique_vertices(
        result.vertex_ids.begin(), result.vertex_ids.end());
    if (unique_vertices.size() != sides) {
        throw std::invalid_argument(
            "Regular polygon vertices collapse onto existing sketch points");
    }
    result.segment_ids.reserve(sides);
    for (unsigned index = 0; index < sides; ++index) {
        const auto& first = vertices[index];
        const auto& second = vertices[(index + 1) % sides];
        result.segment_ids.push_back(next.add_segment(
            first[0], first[1], second[0], second[1], snap_tolerance));
    }
    for (const auto& vertex_id : result.vertex_ids) {
        static_cast<void>(next.add_point_on_circle_constraint(
            vertex_id, result.support_circle_id));
    }
    for (std::size_t index = 1; index < result.segment_ids.size(); ++index) {
        static_cast<void>(next.add_segment_pair_constraint(
            result.segment_ids.front(), result.segment_ids[index],
            ConstraintKind::EqualLength));
    }
    next.validate();
    *this = std::move(next);
    return result;
}

MirroredGeometryResult Sketch::mirror_geometry(
    const std::vector<std::string>& entity_ids,
    const std::string& axis_id, double snap_tolerance) {
    if (entity_ids.empty() || axis_id.empty() ||
        !std::isfinite(snap_tolerance) || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch mirror input is invalid");
    }
    const std::unordered_set<std::string> selected(
        entity_ids.begin(), entity_ids.end());
    const auto axis = sketch_axis_line(*this, axis_id);
    if (selected.size() != entity_ids.size() || !axis ||
        selected.contains(axis_id)) {
        throw std::invalid_argument(
            "Sketch mirror sources must be unique and exclude a valid mirror axis");
    }
    if (std::hypot(axis->second[0], axis->second[1]) <= 1.0e-12) {
        throw std::invalid_argument("Sketch mirror axis has zero length");
    }
    for (const auto& entity_id : entity_ids) {
        const std::size_t count =
            static_cast<std::size_t>(std::count_if(
                points.begin(), points.end(), [&](const auto& value) {
                    return value.id == entity_id;
                })) +
            static_cast<std::size_t>(std::count_if(
                segments.begin(), segments.end(), [&](const auto& value) {
                    return value.id == entity_id;
                })) +
            static_cast<std::size_t>(std::count_if(
                circles.begin(), circles.end(), [&](const auto& value) {
                    return value.id == entity_id;
                })) +
            static_cast<std::size_t>(std::count_if(
                arcs.begin(), arcs.end(), [&](const auto& value) {
                    return value.id == entity_id;
                })) +
            static_cast<std::size_t>(std::count_if(
                ellipses.begin(), ellipses.end(), [&](const auto& value) {
                    return value.id == entity_id;
                })) +
            static_cast<std::size_t>(std::count_if(
                elliptical_arcs.begin(), elliptical_arcs.end(),
                [&](const auto& value) { return value.id == entity_id; })) +
            static_cast<std::size_t>(std::count_if(
                bsplines.begin(), bsplines.end(), [&](const auto& value) {
                    return value.id == entity_id;
                }));
        if (count != 1) {
            throw std::invalid_argument(
                "Sketch mirror source entity does not exist uniquely");
        }
    }

    auto next = *this;
    MirroredGeometryResult result;
    std::unordered_map<std::string, std::string> point_map;
    std::vector<std::string> source_point_order;
    const auto map_point = [&](const std::string& source_id) {
        if (const auto existing = point_map.find(source_id);
            existing != point_map.end()) {
            return existing->second;
        }
        const auto* source = find_point(source_id);
        if (source == nullptr) {
            throw std::runtime_error("Sketch mirror source point is missing");
        }
        const auto coordinate = reflected_position(
            {source->x, source->y}, axis->first, axis->second);
        auto mirrored = create_point(coordinate[0], coordinate[1]);
        mirrored.construction = source->construction;
        mirrored.fixed = false;
        const auto mirrored_id = mirrored.id;
        next.points.push_back(std::move(mirrored));
        point_map.emplace(source_id, mirrored_id);
        source_point_order.push_back(source_id);
        result.point_ids.push_back(mirrored_id);
        return mirrored_id;
    };

    for (const auto& entity_id : entity_ids) {
        if (find_point(entity_id) != nullptr) {
            static_cast<void>(map_point(entity_id));
            continue;
        }
        std::string mirrored_id;
        if (const auto source = std::find_if(segments.begin(), segments.end(),
                [&](const auto& value) { return value.id == entity_id; });
            source != segments.end()) {
            auto mirrored = create_segment(
                map_point(source->first_point_id),
                map_point(source->second_point_id), source->construction);
            mirrored_id = mirrored.id;
            next.segments.push_back(std::move(mirrored));
        } else if (const auto source = std::find_if(circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == entity_id; });
            source != circles.end()) {
            SketchCircle mirrored{
                make_id(), map_point(source->center_point_id),
                source->radius, source->construction};
            mirrored_id = mirrored.id;
            next.circles.push_back(std::move(mirrored));
        } else if (const auto source = std::find_if(arcs.begin(), arcs.end(),
                [&](const auto& value) { return value.id == entity_id; });
            source != arcs.end()) {
            const auto center_id = map_point(source->center_point_id);
            const auto start_id = map_point(source->end_point_id);
            const auto end_id = map_point(source->start_point_id);
            const auto* center = next.find_point(center_id);
            const auto* start = next.find_point(start_id);
            const double start_angle = std::atan2(
                start->y - center->y, start->x - center->x);
            SketchArc mirrored{
                make_id(), center_id, start_id, end_id, source->radius,
                start_angle, start_angle + (source->end_angle - source->start_angle),
                source->construction};
            mirrored_id = mirrored.id;
            next.arcs.push_back(std::move(mirrored));
        } else if (const auto source = std::find_if(ellipses.begin(), ellipses.end(),
                [&](const auto& value) { return value.id == entity_id; });
            source != ellipses.end()) {
            const auto center_id = map_point(source->center_point_id);
            const auto major_id = map_point(source->major_point_id);
            const auto minor_id = map_point(source->minor_point_id);
            const auto* center = next.find_point(center_id);
            const auto* major = next.find_point(major_id);
            const double rotation = std::atan2(
                major->y - center->y, major->x - center->x);
            SketchEllipse mirrored{
                make_id(), center_id, major_id, minor_id,
                source->major_radius, source->minor_radius, rotation,
                source->construction, !source->reversed};
            mirrored_id = mirrored.id;
            next.ellipses.push_back(std::move(mirrored));
        } else if (const auto source = std::find_if(
                elliptical_arcs.begin(), elliptical_arcs.end(),
                [&](const auto& value) { return value.id == entity_id; });
            source != elliptical_arcs.end()) {
            const auto center_id = map_point(source->center_point_id);
            const auto major_id = map_point(source->major_point_id);
            const auto minor_id = map_point(source->minor_point_id);
            const auto start_id = map_point(source->start_point_id);
            const auto end_id = map_point(source->end_point_id);
            const auto* center = next.find_point(center_id);
            const auto* major = next.find_point(major_id);
            const auto* start = next.find_point(start_id);
            const double rotation = std::atan2(
                major->y - center->y, major->x - center->x);
            const bool reversed = !source->reversed;
            const double start_parameter = ellipse_parameter(
                center->x, center->y, source->major_radius,
                source->minor_radius, rotation, reversed,
                start->x, start->y);
            SketchEllipticalArc mirrored{
                make_id(), center_id, major_id, minor_id, start_id, end_id,
                source->major_radius, source->minor_radius, rotation,
                start_parameter,
                start_parameter + (source->end_parameter - source->start_parameter),
                source->construction, reversed};
            mirrored_id = mirrored.id;
            next.elliptical_arcs.push_back(std::move(mirrored));
        } else {
            const auto spline_source = std::find_if(bsplines.begin(), bsplines.end(),
                [&](const auto& value) { return value.id == entity_id; });
            SketchBSpline mirrored;
            mirrored.id = make_id();
            mirrored.degree = spline_source->degree;
            mirrored.closed = spline_source->closed;
            mirrored.construction = spline_source->construction;
            for (const auto& point_id : spline_source->control_point_ids) {
                mirrored.control_point_ids.push_back(map_point(point_id));
            }
            mirrored_id = mirrored.id;
            next.bsplines.push_back(std::move(mirrored));
        }
        result.geometry_ids.push_back(std::move(mirrored_id));
    }

    for (const auto& source_id : source_point_order) {
        const auto& mirrored_id = point_map.at(source_id);
        SketchConstraint constraint;
        constraint.id = make_id();
        constraint.kind = ConstraintKind::Symmetric;
        constraint.first_point_id = source_id;
        constraint.second_point_id = mirrored_id;
        constraint.geometry_id = axis_id;
        next.constraints.push_back(std::move(constraint));
    }
    next.validate();
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Conflicting ||
        solved.status == SolveStatus::Invalid) {
        throw std::runtime_error("Sketch mirror symmetry could not be solved");
    }
    next.validate();
    *this = std::move(next);
    return result;
}

std::string Sketch::add_circle(
    double center_x, double center_y, double radius,
    bool construction, double snap_tolerance) {
    for (const double value : {center_x, center_y, radius, snap_tolerance}) {
        require_finite(value, "circle parameter");
    }
    if (radius <= 1.0e-12 || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch circle radius must be positive");
    }
    auto next = *this;
    const auto center = std::find_if(next.points.begin(), next.points.end(),
        [&](const auto& point) {
            return std::hypot(point.x - center_x, point.y - center_y) <= snap_tolerance;
        });
    std::string center_id;
    if (center == next.points.end()) {
        auto point = create_point(center_x, center_y);
        center_id = point.id;
        next.points.push_back(std::move(point));
    } else {
        center_id = center->id;
    }
    SketchCircle circle{make_id(), center_id, radius, construction};
    const auto id = circle.id;
    next.circles.push_back(std::move(circle));
    bind_matching_external_points(next, snap_tolerance);
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_arc(
    double center_x, double center_y, double start_x, double start_y,
    double end_x, double end_y, bool construction, double snap_tolerance,
    bool clockwise) {
    for (const double value : {center_x, center_y, start_x, start_y,
                               end_x, end_y, snap_tolerance}) {
        require_finite(value, "arc parameter");
    }
    const double radius = std::hypot(start_x - center_x, start_y - center_y);
    if (radius <= 1.0e-12 || snap_tolerance < 0.0 ||
        std::hypot(end_x - center_x, end_y - center_y) <= 1.0e-12) {
        throw std::invalid_argument("Sketch arc points or snap tolerance are invalid");
    }
    if (clockwise) {
        std::swap(start_x, end_x);
        std::swap(start_y, end_y);
    }
    double start_angle = std::atan2(start_y - center_y, start_x - center_x);
    double end_angle = std::atan2(end_y - center_y, end_x - center_x);
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    while (end_angle <= start_angle) end_angle += full_turn;
    if (end_angle - start_angle >= full_turn - 1.0e-12) {
        throw std::invalid_argument("Sketch arc must have a non-zero sweep");
    }
    auto next = *this;
    const auto point_id = [&](double x, double y) {
        const auto found = std::find_if(next.points.begin(), next.points.end(),
            [&](const auto& point) {
                return std::hypot(point.x - x, point.y - y) <= snap_tolerance;
            });
        if (found != next.points.end()) return found->id;
        auto point = create_point(x, y);
        const auto id = point.id;
        next.points.push_back(std::move(point));
        return id;
    };
    const auto center_id = point_id(center_x, center_y);
    const auto start_id = point_id(start_x, start_y);
    const auto end_id = point_id(end_x, end_y);
    SketchArc arc{make_id(), center_id, start_id, end_id, radius,
                  start_angle, end_angle, construction};
    const auto id = arc.id;
    next.arcs.push_back(std::move(arc));
    bind_matching_external_points(next, snap_tolerance);
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_tangent_arc(
    const std::string& start_point_id,
    double end_x, double end_y,
    const std::string& tangent_geometry_id,
    bool reverse, bool construction, double snap_tolerance) {
    for (const double value : {end_x, end_y, snap_tolerance}) {
        require_finite(value, "tangent arc parameter");
    }
    const auto* start = find_point(start_point_id);
    if (start == nullptr || snap_tolerance < 0.0 ||
        std::hypot(end_x - start->x, end_y - start->y) <= 1.0e-12) {
        throw std::invalid_argument("Tangent arc endpoints are invalid");
    }
    std::optional<std::array<double, 2>> tangent;
    if (const auto segment = std::find_if(segments.begin(), segments.end(),
            [&](const auto& value) { return value.id == tangent_geometry_id; });
        segment != segments.end()) {
        const auto* first = find_point(segment->first_point_id);
        const auto* second = find_point(segment->second_point_id);
        if (segment->second_point_id == start_point_id) {
            tangent = std::array{second->x - first->x, second->y - first->y};
        } else if (segment->first_point_id == start_point_id) {
            tangent = std::array{first->x - second->x, first->y - second->y};
        }
    } else if (const auto arc = std::find_if(arcs.begin(), arcs.end(),
                   [&](const auto& value) { return value.id == tangent_geometry_id; });
               arc != arcs.end()) {
        const auto* center = find_point(arc->center_point_id);
        const double radial_x = start->x - center->x;
        const double radial_y = start->y - center->y;
        if (arc->end_point_id == start_point_id) {
            tangent = std::array{-radial_y, radial_x};
        } else if (arc->start_point_id == start_point_id) {
            tangent = std::array{radial_y, -radial_x};
        }
    }
    if (!tangent) {
        throw std::invalid_argument(
            "Tangent arc requires connected segment or circular arc geometry");
    }
    if (reverse) {
        (*tangent)[0] = -(*tangent)[0];
        (*tangent)[1] = -(*tangent)[1];
    }
    const double tangent_length = std::hypot((*tangent)[0], (*tangent)[1]);
    const double tx = (*tangent)[0] / tangent_length;
    const double ty = (*tangent)[1] / tangent_length;
    const double nx = -ty;
    const double ny = tx;
    const double dx = end_x - start->x;
    const double dy = end_y - start->y;
    const double denominator = 2.0 * (dx * nx + dy * ny);
    if (std::abs(denominator) <= 1.0e-9) {
        throw std::invalid_argument(
            "Tangent arc endpoint lies on its start tangent");
    }
    const double signed_radius = (dx * dx + dy * dy) / denominator;
    const double center_x = start->x + nx * signed_radius;
    const double center_y = start->y + ny * signed_radius;
    auto next = *this;
    const auto arc_id = next.add_arc(
        center_x, center_y, start->x, start->y,
        end_x, end_y, construction, snap_tolerance, signed_radius < 0.0);
    static_cast<void>(next.add_tangent_constraint(
        tangent_geometry_id, arc_id));
    next.validate();
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting ||
        result.status == SolveStatus::Invalid) {
        throw std::runtime_error(
            "Tangent arc conflicts with existing Sketch geometry");
    }
    *this = std::move(next);
    return arc_id;
}

static CornerFilletResult materialize_corner_fillet(
    Sketch& source,
    const std::string& first_segment_id,
    const std::string& second_segment_id,
    double radius, double snap_tolerance) {
    require_finite(radius, "corner fillet radius");
    require_finite(snap_tolerance, "corner fillet snap tolerance");
    if (radius <= 1.0e-12 || snap_tolerance < 0.0 ||
        first_segment_id == second_segment_id) {
        throw std::invalid_argument("Corner fillet input is invalid");
    }
    auto next = source;
    auto first_segment = std::find_if(next.segments.begin(), next.segments.end(),
        [&](const auto& value) { return value.id == first_segment_id; });
    auto second_segment = std::find_if(next.segments.begin(), next.segments.end(),
        [&](const auto& value) { return value.id == second_segment_id; });
    if (first_segment == next.segments.end() ||
        second_segment == next.segments.end()) {
        throw std::invalid_argument("Corner fillet segments do not exist");
    }
    std::string corner_id;
    if (first_segment->first_point_id == second_segment->first_point_id ||
        first_segment->first_point_id == second_segment->second_point_id) {
        corner_id = first_segment->first_point_id;
    } else if (first_segment->second_point_id == second_segment->first_point_id ||
               first_segment->second_point_id == second_segment->second_point_id) {
        corner_id = first_segment->second_point_id;
    } else {
        throw std::invalid_argument("Corner fillet segments do not share a vertex");
    }
    const auto other_id = [&](const SketchSegment& segment) {
        return segment.first_point_id == corner_id
            ? segment.second_point_id : segment.first_point_id;
    };
    const auto first_other_id = other_id(*first_segment);
    const auto second_other_id = other_id(*second_segment);
    const auto* corner = next.find_point(corner_id);
    const auto* first_other = next.find_point(first_other_id);
    const auto* second_other = next.find_point(second_other_id);
    const double first_dx = first_other->x - corner->x;
    const double first_dy = first_other->y - corner->y;
    const double second_dx = second_other->x - corner->x;
    const double second_dy = second_other->y - corner->y;
    const double first_length = std::hypot(first_dx, first_dy);
    const double second_length = std::hypot(second_dx, second_dy);
    const double ux = first_dx / first_length;
    const double uy = first_dy / first_length;
    const double vx = second_dx / second_length;
    const double vy = second_dy / second_length;
    const double cosine = std::clamp(ux * vx + uy * vy, -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (angle <= 1.0e-7 || angle >= 3.14159265358979323846 - 1.0e-7) {
        throw std::invalid_argument("Corner fillet requires two non-collinear segments");
    }
    const double tangent_distance = radius / std::tan(angle * 0.5);
    if (tangent_distance >= first_length - 1.0e-9 ||
        tangent_distance >= second_length - 1.0e-9) {
        throw std::invalid_argument("Corner fillet radius is too large");
    }
    const double bisector_x = ux + vx;
    const double bisector_y = uy + vy;
    const double bisector_length = std::hypot(bisector_x, bisector_y);
    const double center_distance = radius / std::sin(angle * 0.5);
    const std::array first_tangent{
        corner->x + ux * tangent_distance,
        corner->y + uy * tangent_distance};
    const std::array second_tangent{
        corner->x + vx * tangent_distance,
        corner->y + vy * tangent_distance};
    const std::array center{
        corner->x + bisector_x / bisector_length * center_distance,
        corner->y + bisector_y / bisector_length * center_distance};

    const auto uses_corner_elsewhere = [&] {
        return std::any_of(next.segments.begin(), next.segments.end(),
                   [&](const auto& value) {
                       return value.id != first_segment_id &&
                           value.id != second_segment_id &&
                           (value.first_point_id == corner_id ||
                            value.second_point_id == corner_id);
                   }) ||
            std::any_of(next.circles.begin(), next.circles.end(),
                [&](const auto& value) { return value.center_point_id == corner_id; }) ||
            std::any_of(next.arcs.begin(), next.arcs.end(), [&](const auto& value) {
                return value.center_point_id == corner_id ||
                    value.start_point_id == corner_id || value.end_point_id == corner_id;
            });
    };
    if (uses_corner_elsewhere()) {
        throw std::invalid_argument(
            "Corner fillet vertex is shared by other Sketch geometry");
    }
    CornerFilletResult result;
    result.first_tangent_point_id = next.add_point(
        first_tangent[0], first_tangent[1], snap_tolerance);
    result.second_tangent_point_id = next.add_point(
        second_tangent[0], second_tangent[1], snap_tolerance);
    // add_point() commits through a replacement Sketch, invalidating all
    // iterators into next. Resolve the edited segments again before trimming.
    first_segment = std::find_if(next.segments.begin(), next.segments.end(),
        [&](const auto& value) { return value.id == first_segment_id; });
    second_segment = std::find_if(next.segments.begin(), next.segments.end(),
        [&](const auto& value) { return value.id == second_segment_id; });
    if (first_segment == next.segments.end() ||
        second_segment == next.segments.end()) {
        throw std::runtime_error(
            "Corner fillet segments disappeared while trimming");
    }
    if (result.first_tangent_point_id == result.second_tangent_point_id ||
        result.first_tangent_point_id == corner_id ||
        result.second_tangent_point_id == corner_id) {
        throw std::invalid_argument("Corner fillet tangent points collapse");
    }
    const auto replace_corner = [&](SketchSegment& segment,
                                    const std::string& replacement_id) {
        if (segment.first_point_id == corner_id) segment.first_point_id = replacement_id;
        else segment.second_point_id = replacement_id;
    };
    replace_corner(*first_segment, result.first_tangent_point_id);
    replace_corner(*second_segment, result.second_tangent_point_id);
    std::erase_if(next.constraints, [&](auto& constraint) {
        if (constraint.geometry_id == first_segment_id ||
            constraint.geometry_id == second_segment_id) {
            if (constraint.kind == ConstraintKind::Horizontal ||
                constraint.kind == ConstraintKind::Vertical) {
                const auto& segment = constraint.geometry_id == first_segment_id
                    ? *first_segment : *second_segment;
                constraint.first_point_id = segment.first_point_id;
                constraint.second_point_id = segment.second_point_id;
            }
            return false;
        }
        return constraint.first_point_id == corner_id ||
            constraint.second_point_id == corner_id;
    });
    std::erase_if(next.dimensions, [&](auto& dimension) {
        if (dimension.geometry_id == first_segment_id ||
            dimension.geometry_id == second_segment_id) {
            const auto& segment = dimension.geometry_id == first_segment_id
                ? *first_segment : *second_segment;
            dimension.first_point_id = segment.first_point_id;
            dimension.second_point_id = segment.second_point_id;
            return false;
        }
        return dimension.first_point_id == corner_id ||
            dimension.second_point_id == corner_id;
    });
    // Keep the original corner as the persistent radius handle.  A Sketch
    // fillet trims the two displayed/profile legs, but it must not destroy
    // the design vertex which defined their intersection.  Besides matching
    // the Python interaction contract, retaining this stable ZIMA point is
    // what makes the corner available for later radius editing.
    const double cross = ux * vy - uy * vx;
    result.arc_id = cross > 0.0
        ? next.add_arc(center[0], center[1],
            second_tangent[0], second_tangent[1],
            first_tangent[0], first_tangent[1], false, snap_tolerance)
        : next.add_arc(center[0], center[1],
            first_tangent[0], first_tangent[1],
            second_tangent[0], second_tangent[1], false, snap_tolerance);
    static_cast<void>(next.add_tangent_constraint(first_segment_id, result.arc_id));
    static_cast<void>(next.add_tangent_constraint(second_segment_id, result.arc_id));
    next.validate();
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Conflicting ||
        solved.status == SolveStatus::Invalid) {
        throw std::runtime_error("Corner fillet conflicts with existing Sketch geometry");
    }
    source = std::move(next);
    return result;
}

CornerFilletResult Sketch::add_corner_fillet(
    const std::string& first_segment_id,
    const std::string& second_segment_id,
    double radius, double snap_tolerance) {
    require_finite(radius, "corner radius");
    require_finite(snap_tolerance, "corner radius snap tolerance");
    if (radius < 0.0 || snap_tolerance < 0.0 ||
        first_segment_id == second_segment_id) {
        throw std::invalid_argument("Corner radius input is invalid");
    }
    const auto first = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == first_segment_id; });
    const auto second = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == second_segment_id; });
    if (first == segments.end() || second == segments.end()) {
        throw std::invalid_argument("Corner radius segments do not exist");
    }
    std::string vertex_id;
    for (const auto& point_id : {first->first_point_id, first->second_point_id}) {
        if (point_id == second->first_point_id || point_id == second->second_point_id) {
            vertex_id = point_id;
            break;
        }
    }
    if (vertex_id.empty()) {
        throw std::invalid_argument("Corner radius segments do not share a vertex");
    }
    auto next = *this;
    auto existing = std::find_if(next.corner_radii.begin(), next.corner_radii.end(),
        [&](const auto& value) {
            return value.vertex_id == vertex_id &&
                ((value.first_segment_id == first_segment_id &&
                  value.second_segment_id == second_segment_id) ||
                 (value.first_segment_id == second_segment_id &&
                  value.second_segment_id == first_segment_id));
        });
    if (radius <= 1.0e-9) {
        if (existing == next.corner_radii.end()) return {{}, {}, {}};
        const auto id = existing->id;
        existing->radius = 0.0;
        existing->suppressed = false;
        for (auto& dimension : next.dimensions) {
            if (dimension.geometry_id == id &&
                (dimension.kind == DimensionKind::Radius ||
                 dimension.kind == DimensionKind::Diameter)) {
                dimension.value = 0.0;
            }
        }
        next.validate();
        *this = std::move(next);
        return {id, {}, {}};
    }
    // Validate the value by evaluating it transactionally, without modifying
    // the persisted source graph.
    auto evaluated = next;
    evaluated.corner_radii.clear();
    const auto materialized = materialize_corner_fillet(
        evaluated, first_segment_id, second_segment_id, radius, snap_tolerance);
    const std::string id = existing == next.corner_radii.end()
        ? make_id() : existing->id;
    if (existing == next.corner_radii.end()) {
        next.corner_radii.push_back({id, vertex_id, first_segment_id,
            second_segment_id, radius, false});
    } else {
        existing->radius = radius;
        existing->suppressed = false;
    }
    for (auto& dimension : next.dimensions) {
        if (dimension.geometry_id == id &&
            (dimension.kind == DimensionKind::Radius ||
             dimension.kind == DimensionKind::Diameter)) {
            dimension.value = dimension.kind == DimensionKind::Diameter
                ? radius * 2.0 : radius;
        }
    }
    next.validate();
    *this = std::move(next);
    return {id, {}, {}};
}

Sketch Sketch::evaluated_profile_sketch() const {
    auto result = *this;
    const auto records = result.corner_radii;
    std::erase_if(result.corner_radii, [](const auto& record) {
        return !record.suppressed && record.radius > 1.0e-9;
    });
    for (const auto& record : records) {
        if (record.suppressed || record.radius <= 1.0e-9) continue;
        const auto materialized = materialize_corner_fillet(result,
            record.first_segment_id, record.second_segment_id, record.radius,
            1.0e-7);
        const auto arc = std::find_if(result.arcs.begin(), result.arcs.end(),
            [&](const auto& value) { return value.id == materialized.arc_id; });
        const auto rename_point = [&](const std::string& old_id,
                                      const std::string& role) {
            const std::string new_id = record.id + ":" + role + ":parent:" +
                record.vertex_id;
            if (auto* point = result.find_point(old_id)) point->id = new_id;
            for (auto& segment : result.segments) {
                if (segment.first_point_id == old_id) segment.first_point_id = new_id;
                if (segment.second_point_id == old_id) segment.second_point_id = new_id;
            }
            for (auto& value : result.arcs) {
                if (value.center_point_id == old_id) value.center_point_id = new_id;
                if (value.start_point_id == old_id) value.start_point_id = new_id;
                if (value.end_point_id == old_id) value.end_point_id = new_id;
            }
            for (auto& constraint : result.constraints) {
                if (constraint.first_point_id == old_id)
                    constraint.first_point_id = new_id;
                if (constraint.second_point_id == old_id)
                    constraint.second_point_id = new_id;
            }
            for (auto& dimension : result.dimensions) {
                if (dimension.first_point_id == old_id)
                    dimension.first_point_id = new_id;
                if (dimension.second_point_id == old_id)
                    dimension.second_point_id = new_id;
            }
        };
        rename_point(materialized.first_tangent_point_id, "tangent:first");
        rename_point(materialized.second_tangent_point_id, "tangent:second");
        if (arc != result.arcs.end()) {
            rename_point(arc->center_point_id, "center");
            arc->id = record.id;
        }
        for (auto& constraint : result.constraints) {
            if (constraint.geometry_id == materialized.arc_id)
                constraint.geometry_id = record.id;
            if (constraint.second_geometry_id == materialized.arc_id)
                constraint.second_geometry_id = record.id;
        }
    }
    result.validate();
    return result;
}

std::optional<std::pair<std::array<double, 2>, std::array<double, 2>>>
Sketch::visible_segment_endpoints(const std::string& segment_id) const {
    const auto segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (segment == segments.end()) return std::nullopt;
    const auto* first = find_point(segment->first_point_id);
    const auto* second = find_point(segment->second_point_id);
    if (first == nullptr || second == nullptr) return std::nullopt;
    std::pair result{
        std::array{first->x, first->y}, std::array{second->x, second->y}};
    const auto trim_endpoint = [&](const std::string& vertex_id,
                                   std::array<double, 2>& endpoint) {
        const auto radius = std::find_if(corner_radii.begin(), corner_radii.end(),
            [&](const auto& value) {
                return !value.suppressed && value.vertex_id == vertex_id &&
                    (value.first_segment_id == segment_id ||
                     value.second_segment_id == segment_id);
            });
        if (radius == corner_radii.end()) return;
        const auto other_segment_id = radius->first_segment_id == segment_id
            ? radius->second_segment_id : radius->first_segment_id;
        const auto other_segment = std::find_if(segments.begin(), segments.end(),
            [&](const auto& value) { return value.id == other_segment_id; });
        const auto* vertex = find_point(vertex_id);
        if (other_segment == segments.end() || vertex == nullptr) return;
        const auto outer_point = [&](const SketchSegment& value) {
            return find_point(value.first_point_id == vertex_id
                ? value.second_point_id : value.first_point_id);
        };
        const auto* this_outer = outer_point(*segment);
        const auto* other_outer = outer_point(*other_segment);
        if (this_outer == nullptr || other_outer == nullptr) return;
        const double ax = this_outer->x - vertex->x;
        const double ay = this_outer->y - vertex->y;
        const double bx = other_outer->x - vertex->x;
        const double by = other_outer->y - vertex->y;
        const double al = std::hypot(ax, ay);
        const double bl = std::hypot(bx, by);
        if (al <= 1.0e-12 || bl <= 1.0e-12) return;
        const double angle = std::acos(std::clamp(
            (ax * bx + ay * by) / (al * bl), -1.0, 1.0));
        const double tangent = std::tan(angle * 0.5);
        if (tangent <= 1.0e-12) return;
        const double distance = radius->radius / tangent;
        if (distance >= al) return;
        endpoint = {vertex->x + ax / al * distance,
                    vertex->y + ay / al * distance};
    };
    trim_endpoint(segment->first_point_id, result.first);
    trim_endpoint(segment->second_point_id, result.second);
    return result;
}

std::string Sketch::add_ellipse(
    double center_x, double center_y, double major_x, double major_y,
    double minor_x, double minor_y, bool construction, double snap_tolerance) {
    for (const double value : {center_x, center_y, major_x, major_y,
                               minor_x, minor_y, snap_tolerance}) {
        require_finite(value, "ellipse parameter");
    }
    const double major_radius = std::hypot(major_x - center_x, major_y - center_y);
    const double rotation = std::atan2(major_y - center_y, major_x - center_x);
    const double minor_radius = std::abs(
        -(minor_x - center_x) * std::sin(rotation) +
          (minor_y - center_y) * std::cos(rotation));
    if (major_radius <= 1.0e-12 || minor_radius <= 1.0e-12 || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch ellipse axes or snap tolerance are invalid");
    }
    auto next = *this;
    const auto point_id = [&](double x, double y) {
        const auto found = std::find_if(next.points.begin(), next.points.end(),
            [&](const auto& point) {
                return std::hypot(point.x - x, point.y - y) <= snap_tolerance;
            });
        if (found != next.points.end()) return found->id;
        auto point = create_point(x, y);
        const auto id = point.id;
        next.points.push_back(std::move(point));
        return id;
    };
    const auto center_id = point_id(center_x, center_y);
    const auto major_id = point_id(
        center_x + major_radius * std::cos(rotation),
        center_y + major_radius * std::sin(rotation));
    const auto minor_id = point_id(
        center_x - minor_radius * std::sin(rotation),
        center_y + minor_radius * std::cos(rotation));
    SketchEllipse ellipse{make_id(), center_id, major_id, minor_id,
                          major_radius, minor_radius, rotation, construction};
    const auto id = ellipse.id;
    next.ellipses.push_back(std::move(ellipse));
    bind_matching_external_points(next, snap_tolerance);
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_elliptical_arc(
    double center_x, double center_y, double major_x, double major_y,
    double minor_x, double minor_y, double start_x, double start_y,
    double end_x, double end_y, bool reversed, bool construction,
    double snap_tolerance) {
    for (const double value : {center_x, center_y, major_x, major_y,
                               minor_x, minor_y, start_x, start_y,
                               end_x, end_y, snap_tolerance}) {
        require_finite(value, "elliptical arc parameter");
    }
    const double major_radius = std::hypot(major_x - center_x, major_y - center_y);
    const double rotation = std::atan2(major_y - center_y, major_x - center_x);
    const double orientation = reversed ? -1.0 : 1.0;
    const double minor_projection = orientation * (
        -(minor_x - center_x) * std::sin(rotation) +
         (minor_y - center_y) * std::cos(rotation));
    if (major_radius <= 1.0e-12 || minor_projection <= 1.0e-12 ||
        snap_tolerance < 0.0) {
        throw std::invalid_argument(
            "Sketch elliptical arc axes or snap tolerance are invalid");
    }
    const double minor_radius = minor_projection;
    double start_parameter = ellipse_parameter(
        center_x, center_y, major_radius, minor_radius, rotation, reversed,
        start_x, start_y);
    double end_parameter = ellipse_parameter(
        center_x, center_y, major_radius, minor_radius, rotation, reversed,
        end_x, end_y);
    while (end_parameter <= start_parameter) end_parameter += full_turn;
    const auto exact_start = ellipse_position(
        center_x, center_y, major_radius, minor_radius, rotation, reversed,
        start_parameter);
    const auto exact_end = ellipse_position(
        center_x, center_y, major_radius, minor_radius, rotation, reversed,
        end_parameter);
    const double point_tolerance = std::max(1.0e-7, snap_tolerance);
    if (end_parameter - start_parameter >= full_turn - 1.0e-12 ||
        std::hypot(start_x - exact_start[0], start_y - exact_start[1]) >
            point_tolerance ||
        std::hypot(end_x - exact_end[0], end_y - exact_end[1]) >
            point_tolerance) {
        throw std::invalid_argument(
            "Sketch elliptical arc endpoints must lie on the ellipse");
    }
    auto next = *this;
    const auto point_id = [&](double x, double y) {
        const auto found = std::find_if(next.points.begin(), next.points.end(),
            [&](const auto& point) {
                return std::hypot(point.x - x, point.y - y) <= snap_tolerance;
            });
        if (found != next.points.end() &&
            std::hypot(found->x - x, found->y - y) <= 1.0e-7) {
            return found->id;
        }
        auto point = create_point(x, y);
        const auto id = point.id;
        next.points.push_back(std::move(point));
        return id;
    };
    const auto center_id = point_id(center_x, center_y);
    const auto major_id = point_id(
        center_x + major_radius * std::cos(rotation),
        center_y + major_radius * std::sin(rotation));
    const auto minor_id = point_id(
        center_x - orientation * minor_radius * std::sin(rotation),
        center_y + orientation * minor_radius * std::cos(rotation));
    const auto start_id = point_id(exact_start[0], exact_start[1]);
    const auto end_id = point_id(exact_end[0], exact_end[1]);
    SketchEllipticalArc arc{
        make_id(), center_id, major_id, minor_id, start_id, end_id,
        major_radius, minor_radius, rotation, start_parameter, end_parameter,
        construction, reversed};
    const auto id = arc.id;
    next.elliptical_arcs.push_back(std::move(arc));
    bind_matching_external_points(next, snap_tolerance);
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_bspline(
    const std::vector<std::array<double, 2>>& control_points,
    unsigned degree, bool closed, bool construction, double snap_tolerance,
    bool interpolating) {
    if (degree < 1 || control_points.size() < static_cast<std::size_t>(degree) + 1 ||
        !std::isfinite(snap_tolerance) || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch B-spline degree or control points are invalid");
    }
    auto next = *this;
    SketchBSpline spline;
    spline.id = make_id();
    spline.degree = degree;
    spline.interpolating = interpolating;
    spline.closed = closed;
    spline.construction = construction;
    for (const auto& coordinate : control_points) {
        require_finite(coordinate[0], "B-spline control point x");
        require_finite(coordinate[1], "B-spline control point y");
        const auto found = std::find_if(next.points.begin(), next.points.end(),
            [&](const auto& point) {
                return std::hypot(point.x - coordinate[0], point.y - coordinate[1]) <=
                    snap_tolerance;
            });
        if (found != next.points.end()) {
            spline.control_point_ids.push_back(found->id);
        } else {
            auto point = create_point(coordinate[0], coordinate[1]);
            spline.control_point_ids.push_back(point.id);
            next.points.push_back(std::move(point));
        }
    }
    const auto id = spline.id;
    next.bsplines.push_back(std::move(spline));
    bind_matching_external_points(next, snap_tolerance);
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_import_block(
    std::string name, std::string source_path,
    std::vector<std::string> geometry_ids,
    std::vector<std::string> point_ids) {
    if (name.empty() || geometry_ids.empty() || point_ids.empty()) {
        throw std::invalid_argument("Import block identity or contents are empty");
    }
    SketchImportBlock block{make_id(), std::move(name), std::move(source_path),
        std::move(geometry_ids), std::move(point_ids)};
    const auto id = block.id;
    auto next = *this;
    next.import_blocks.push_back(std::move(block));
    next.validate();
    *this = std::move(next);
    return id;
}

SketchText Sketch::create_text() {
    SketchText text;
    text.id = make_id();
    return text;
}

void Sketch::add_text(SketchText text) {
    if (text.id.empty()) text.id = make_id();
    if (std::ranges::any_of(texts, [&](const auto& value) {
            return value.id == text.id;
        })) {
        throw std::invalid_argument("Sketch text already exists");
    }
    auto next = *this;
    next.texts.push_back(std::move(text));
    next.validate();
    *this = std::move(next);
}

void Sketch::update_text(SketchText text) {
    auto next = *this;
    const auto found = std::find_if(next.texts.begin(), next.texts.end(),
        [&](const auto& value) { return value.id == text.id; });
    if (found == next.texts.end()) {
        throw std::invalid_argument("Sketch text does not exist");
    }
    *found = std::move(text);
    next.validate();
    *this = std::move(next);
}

SketchExternalReference Sketch::create_external_reference(
    ExternalReferenceKind kind) {
    SketchExternalReference reference;
    reference.id = make_id();
    reference.kind = kind;
    return reference;
}

void Sketch::add_external_reference(SketchExternalReference reference) {
    if (reference.id.empty()) reference.id = make_id();
    const auto same_source = [&](const auto& value) {
        return value.kind == reference.kind &&
            value.source_document_id == reference.source_document_id &&
            value.source_owner_id == reference.source_owner_id &&
            value.source_semantic_key == reference.source_semantic_key &&
            value.source_instance_path == reference.source_instance_path &&
            value.context_assembly_document_id ==
                reference.context_assembly_document_id &&
            value.context_instance_path == reference.context_instance_path;
    };
    if (std::ranges::any_of(external_references, [&](const auto& value) {
            return value.id == reference.id || same_source(value);
        })) {
        throw std::invalid_argument("Sketch external reference already exists");
    }
    auto next = *this;
    next.external_references.push_back(std::move(reference));
    next.validate();
    *this = std::move(next);
}

std::string Sketch::add_external_profile_geometry(
    const std::string& reference_id) {
    const auto reference = std::find_if(external_references.begin(),
        external_references.end(), [&](const auto& value) {
            return value.id == reference_id &&
                value.kind == ExternalReferenceKind::Edge && !value.broken;
        });
    if (reference == external_references.end() ||
        reference->cached_points.size() < 2) {
        throw std::invalid_argument(
            "External profile requires a valid projected edge");
    }
    const std::string link = "external-reference:" + reference_id;
    if (std::any_of(import_blocks.begin(), import_blocks.end(),
            [&](const auto& value) { return value.source_path == link; })) {
        throw std::invalid_argument(
            "External edge already owns profile geometry");
    }
    const auto& source = reference->cached_points;
    const auto& first = source.front();
    const auto& last = source.back();
    const double dx = last[0] - first[0];
    const double dy = last[1] - first[1];
    const double length = std::hypot(dx, dy);
    const bool straight = length > 1.0e-10 && std::all_of(
        source.begin() + 1, source.end() - 1, [&](const auto& point) {
            return std::abs(dx * (point[1] - first[1]) -
                dy * (point[0] - first[0])) <=
                std::max(1.0, length) * 1.0e-7;
        });
    auto next = *this;
    std::string geometry_id;
    std::vector<std::string> point_ids;
    if (straight) {
        geometry_id = next.add_segment(
            first[0], first[1], last[0], last[1], 1.0e-9, false);
        const auto segment = std::find_if(next.segments.begin(), next.segments.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        point_ids = {segment->first_point_id, segment->second_point_id};
    } else {
        std::vector<std::array<double, 2>> sampled;
        const auto count = std::min<std::size_t>(16, source.size());
        sampled.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            sampled.push_back(source[std::lround(
                static_cast<double>(index) * (source.size() - 1) /
                static_cast<double>(count - 1))]);
        }
        geometry_id = next.add_bspline(sampled,
            static_cast<unsigned>(std::min<std::size_t>(3, sampled.size() - 1)));
        const auto spline = std::find_if(next.bsplines.begin(), next.bsplines.end(),
            [&](const auto& value) { return value.id == geometry_id; });
        point_ids = spline->control_point_ids;
    }
    static_cast<void>(next.add_import_block(
        "Externí profil", link, {geometry_id}, point_ids));
    *this = std::move(next);
    return geometry_id;
}

std::optional<std::vector<std::array<double, 2>>> Sketch::project_external_axis(
    const zima::kernel::ViewerAxis& axis) const {
    if (!std::isfinite(axis.display_length) || axis.display_length <= 0.0) {
        return std::nullopt;
    }
    const double direction_length = std::sqrt(
        axis.direction.x * axis.direction.x +
        axis.direction.y * axis.direction.y +
        axis.direction.z * axis.direction.z);
    if (!std::isfinite(direction_length) || direction_length <= 1.0e-12) {
        return std::nullopt;
    }
    const double scale = 0.5 * axis.display_length / direction_length;
    const auto first = local_point({
        axis.point.x - axis.direction.x * scale,
        axis.point.y - axis.direction.y * scale,
        axis.point.z - axis.direction.z * scale});
    const auto second = local_point({
        axis.point.x + axis.direction.x * scale,
        axis.point.y + axis.direction.y * scale,
        axis.point.z + axis.direction.z * scale});
    if (std::hypot(second[0] - first[0], second[1] - first[1]) <= 1.0e-9) {
        return std::nullopt;
    }
    return std::vector<std::array<double, 2>>{first, second};
}

std::optional<std::vector<std::array<double, 2>>>
Sketch::project_external_face_plane(
    const zima::kernel::ViewerReferenceGeometry& source_geometry,
    const zima::kernel::FaceReference& face) const {
    if (!face.valid() || source_geometry.triangles.size() % 3 != 0 ||
        source_geometry.triangle_references.size() !=
            source_geometry.triangles.size() / 3) return std::nullopt;
    const auto subtract = [](const auto& left, const auto& right) {
        return zima::kernel::Vec3{left.x - right.x, left.y - right.y,
            left.z - right.z};
    };
    const auto cross = [](const auto& left, const auto& right) {
        return zima::kernel::Vec3{
            left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
    };
    const auto dot = [](const auto& left, const auto& right) {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    };
    const auto length = [&](const auto& value) {
        return std::sqrt(dot(value, value));
    };
    std::optional<zima::kernel::Vec3> face_point;
    std::optional<zima::kernel::Vec3> face_normal;
    for (std::size_t triangle = 0;
         triangle < source_geometry.triangle_references.size(); ++triangle) {
        if (source_geometry.triangle_references[triangle] != face) continue;
        const std::array<std::uint32_t, 3> indices{
            source_geometry.triangles[triangle * 3],
            source_geometry.triangles[triangle * 3 + 1],
            source_geometry.triangles[triangle * 3 + 2]};
        if (std::ranges::any_of(indices, [&](const auto index) {
                return index >= source_geometry.vertices.size();
            })) return std::nullopt;
        const auto& first = source_geometry.vertices[indices[0]];
        const auto normal = cross(
            subtract(source_geometry.vertices[indices[1]], first),
            subtract(source_geometry.vertices[indices[2]], first));
        const double normal_length = length(normal);
        if (normal_length <= 1.0e-12) continue;
        face_point = first;
        face_normal = {normal.x / normal_length, normal.y / normal_length,
            normal.z / normal_length};
        break;
    }
    if (!face_point || !face_normal) return std::nullopt;
    for (std::size_t triangle = 0;
         triangle < source_geometry.triangle_references.size(); ++triangle) {
        if (source_geometry.triangle_references[triangle] != face) continue;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto index = source_geometry.triangles[triangle * 3 + corner];
            if (index >= source_geometry.vertices.size() ||
                std::abs(dot(*face_normal,
                    subtract(source_geometry.vertices[index], *face_point))) >
                    1.0e-6) return std::nullopt;
        }
    }
    const auto sketch_origin = world_point(0.0, 0.0);
    const auto sketch_x = subtract(world_point(1.0, 0.0), sketch_origin);
    const auto sketch_y = subtract(world_point(0.0, 1.0), sketch_origin);
    auto sketch_normal = cross(sketch_x, sketch_y);
    const double sketch_normal_length = length(sketch_normal);
    if (sketch_normal_length <= 1.0e-12) return std::nullopt;
    sketch_normal = {sketch_normal.x / sketch_normal_length,
        sketch_normal.y / sketch_normal_length,
        sketch_normal.z / sketch_normal_length};
    auto direction = cross(*face_normal, sketch_normal);
    const double direction_squared = dot(direction, direction);
    if (direction_squared <= 1.0e-20) return std::nullopt;
    const double first_distance = dot(*face_normal, *face_point);
    const double second_distance = dot(sketch_normal, sketch_origin);
    const auto second_cross_direction = cross(sketch_normal, direction);
    const auto direction_cross_first = cross(direction, *face_normal);
    const zima::kernel::Vec3 point{
        (first_distance * second_cross_direction.x +
            second_distance * direction_cross_first.x) / direction_squared,
        (first_distance * second_cross_direction.y +
            second_distance * direction_cross_first.y) / direction_squared,
        (first_distance * second_cross_direction.z +
            second_distance * direction_cross_first.z) / direction_squared};
    const double direction_length = std::sqrt(direction_squared);
    direction = {direction.x / direction_length, direction.y / direction_length,
        direction.z / direction_length};
    const auto first = local_point({point.x - direction.x,
        point.y - direction.y, point.z - direction.z});
    const auto second = local_point({point.x + direction.x,
        point.y + direction.y, point.z + direction.z});
    if (std::hypot(second[0] - first[0], second[1] - first[1]) <= 1.0e-9) {
        return std::nullopt;
    }
    return std::vector<std::array<double, 2>>{first, second};
}

std::optional<std::vector<std::vector<std::array<double, 2>>>>
Sketch::project_external_face(
    const zima::kernel::ViewerReferenceGeometry& source_geometry,
    const zima::kernel::FaceReference& face) const {
    if (!face.valid() || source_geometry.triangles.size() % 3 != 0 ||
        source_geometry.triangle_references.size() !=
            source_geometry.triangles.size() / 3) {
        return std::nullopt;
    }
    using IndexEdge = std::pair<std::uint32_t, std::uint32_t>;
    std::map<IndexEdge, std::size_t> edge_counts;
    std::size_t matching_triangles{};
    for (std::size_t triangle = 0;
         triangle < source_geometry.triangle_references.size(); ++triangle) {
        if (source_geometry.triangle_references[triangle] != face) continue;
        ++matching_triangles;
        const std::array<std::uint32_t, 3> indices{
            source_geometry.triangles[triangle * 3],
            source_geometry.triangles[triangle * 3 + 1],
            source_geometry.triangles[triangle * 3 + 2]};
        if (std::ranges::any_of(indices, [&](const auto index) {
                return index >= source_geometry.vertices.size();
            })) return std::nullopt;
        for (std::size_t corner = 0; corner < indices.size(); ++corner) {
            const auto first = indices[corner];
            const auto second = indices[(corner + 1) % indices.size()];
            if (first == second) return std::nullopt;
            ++edge_counts[std::minmax(first, second)];
        }
    }
    if (matching_triangles == 0) return std::nullopt;

    std::map<std::uint32_t, std::vector<std::uint32_t>> adjacency;
    std::set<IndexEdge> unused_edges;
    for (const auto& [edge, count] : edge_counts) {
        if (count == 2) continue;
        if (count != 1) return std::nullopt;
        adjacency[edge.first].push_back(edge.second);
        adjacency[edge.second].push_back(edge.first);
        unused_edges.insert(edge);
    }
    if (unused_edges.empty() || std::ranges::any_of(adjacency, [](const auto& item) {
            return item.second.size() != 2;
        })) return std::nullopt;
    for (auto& [vertex, neighbours] : adjacency) {
        static_cast<void>(vertex);
        std::ranges::sort(neighbours);
    }

    std::vector<std::vector<std::array<double, 2>>> paths;
    while (!unused_edges.empty()) {
        const auto first_edge = *unused_edges.begin();
        std::vector<std::uint32_t> loop{first_edge.first};
        std::uint32_t previous = first_edge.first;
        std::uint32_t current = first_edge.second;
        unused_edges.erase(first_edge);
        loop.push_back(current);
        while (current != loop.front()) {
            const auto found = adjacency.find(current);
            if (found == adjacency.end()) return std::nullopt;
            const auto next = found->second.front() == previous
                ? found->second.back() : found->second.front();
            const auto edge = std::minmax(current, next);
            if (!unused_edges.erase(edge)) return std::nullopt;
            previous = current;
            current = next;
            loop.push_back(current);
            if (loop.size() > adjacency.size() + 1) return std::nullopt;
        }
        if (loop.size() < 4) return std::nullopt;
        std::vector<std::array<double, 2>> projected;
        projected.reserve(loop.size());
        for (const auto index : loop) {
            const auto point = local_point(source_geometry.vertices[index]);
            if (projected.empty() || std::hypot(
                    point[0] - projected.back()[0],
                    point[1] - projected.back()[1]) > 1.0e-9) {
                projected.push_back(point);
            }
        }
        if (projected.size() < 4) return std::nullopt;
        if (projected.front() != projected.back()) {
            if (std::hypot(projected.front()[0] - projected.back()[0],
                           projected.front()[1] - projected.back()[1]) > 1.0e-9) {
                return std::nullopt;
            }
            projected.back() = projected.front();
        }
        double signed_area{};
        for (std::size_t index = 1; index < projected.size(); ++index) {
            signed_area += projected[index - 1][0] * projected[index][1] -
                projected[index][0] * projected[index - 1][1];
        }
        if (std::abs(signed_area) <= 1.0e-12) return std::nullopt;
        paths.push_back(std::move(projected));
    }
    if (paths.empty()) return std::nullopt;
    const auto area = [](const auto& path) {
        double result{};
        for (std::size_t index = 1; index < path.size(); ++index) {
            result += path[index - 1][0] * path[index][1] -
                path[index][0] * path[index - 1][1];
        }
        return result * 0.5;
    };
    const auto outer = std::max_element(paths.begin(), paths.end(),
        [&](const auto& left, const auto& right) {
            return std::abs(area(left)) < std::abs(area(right));
        });
    const auto contains = [](const auto& polygon, const auto& point) {
        bool inside = false;
        for (std::size_t first = 0, second = polygon.size() - 2;
             first + 1 < polygon.size(); second = first++) {
            const auto& a = polygon[first];
            const auto& b = polygon[second];
            const bool crosses = (a[1] > point[1]) != (b[1] > point[1]);
            if (crosses && point[0] < (b[0] - a[0]) *
                    (point[1] - a[1]) / (b[1] - a[1]) + a[0]) {
                inside = !inside;
            }
        }
        return inside;
    };
    for (const auto& path : paths) {
        if (&path == &*outer) continue;
        if (!contains(*outer, path.front())) return std::nullopt;
    }
    return paths;
}

bool Sketch::refresh_external_references(
    const std::string& source_document_id,
    const zima::kernel::ViewerReferenceGeometry& source_geometry) {
    if (source_document_id.empty()) {
        throw std::invalid_argument(
            "Sketch external reference source document ID is required");
    }
    auto next = *this;
    bool changed = false;
    const auto same_source = [](const auto& reference, const auto& candidate) {
        return candidate.owner_id == reference.source_owner_id &&
            candidate.semantic_key == reference.source_semantic_key &&
            candidate.instance_path == reference.source_instance_path;
    };
    for (auto& reference : next.external_references) {
        if (reference.source_document_id != source_document_id) continue;
        std::optional<std::vector<std::array<double, 2>>> resolved;
        if (reference.kind == ExternalReferenceKind::Edge) {
            const zima::kernel::ViewerEdge* match = nullptr;
            std::size_t match_count{};
            for (const auto& edge : source_geometry.edges) {
                if (!same_source(reference, edge.reference)) continue;
                match = &edge;
                ++match_count;
            }
            if (match_count == 1 && match != nullptr) {
                std::vector<std::array<double, 2>> points;
                points.reserve(match->points.size());
                for (const auto& world : match->points) {
                    const auto local = next.local_point(world);
                    if (points.empty() || std::hypot(
                            local[0] - points.back()[0],
                            local[1] - points.back()[1]) > 1.0e-9) {
                        points.push_back(local);
                    }
                }
                if (points.size() >= 2) resolved = std::move(points);
            }
        } else if (reference.kind == ExternalReferenceKind::Point) {
            const zima::kernel::ViewerPoint* match = nullptr;
            std::size_t match_count{};
            for (const auto& point : source_geometry.points) {
                if (!same_source(reference, point.reference)) continue;
                match = &point;
                ++match_count;
            }
            if (match_count == 1 && match != nullptr) {
                resolved = std::vector<std::array<double, 2>>{
                    next.local_point(match->position)};
            }
        } else if (reference.kind == ExternalReferenceKind::Axis) {
            const zima::kernel::ViewerAxis* match = nullptr;
            std::size_t match_count{};
            for (const auto& axis : source_geometry.axes) {
                if (!same_source(reference, axis.reference)) continue;
                match = &axis;
                ++match_count;
            }
            if (match_count == 1 && match != nullptr) {
                resolved = next.project_external_axis(*match);
            }
        } else {
            std::size_t match_count{};
            zima::kernel::FaceReference match;
            for (const auto& candidate : source_geometry.triangle_references) {
                if (!same_source(reference, candidate)) continue;
                if (match_count == 0) match = candidate;
                ++match_count;
            }
            if (match_count > 0) resolved =
                next.project_external_face_plane(source_geometry, match);
        }
        const bool broken = !resolved.has_value();
        if (resolved && reference.cached_points != *resolved) {
            reference.cached_points = std::move(*resolved);
            changed = true;
        }
        if (reference.kind == ExternalReferenceKind::Face &&
            !reference.cached_paths.empty()) {
            reference.cached_paths.clear();
            changed = true;
        }
        if (reference.broken != broken) {
            reference.broken = broken;
            changed = true;
        }
    }
    if (!changed) return false;
    constexpr std::string_view external_profile_prefix{"external-reference:"};
    for (const auto& block : next.import_blocks) {
        if (!block.source_path.starts_with(external_profile_prefix) ||
            block.geometry_ids.size() != 1 || block.point_ids.size() < 2) continue;
        const auto reference_id = block.source_path.substr(
            external_profile_prefix.size());
        const auto reference = std::find_if(next.external_references.begin(),
            next.external_references.end(), [&](const auto& value) {
                return value.id == reference_id && !value.broken &&
                    value.kind == ExternalReferenceKind::Edge &&
                    value.cached_points.size() >= 2;
            });
        if (reference == next.external_references.end()) continue;
        const auto segment = std::find_if(next.segments.begin(), next.segments.end(),
            [&](const auto& value) { return value.id == block.geometry_ids.front(); });
        if (segment != next.segments.end() && block.point_ids.size() == 2) {
            auto* first = next.find_point(block.point_ids.front());
            auto* second = next.find_point(block.point_ids.back());
            if (first != nullptr && second != nullptr) {
                first->x = reference->cached_points.front()[0];
                first->y = reference->cached_points.front()[1];
                second->x = reference->cached_points.back()[0];
                second->y = reference->cached_points.back()[1];
            }
            continue;
        }
        const auto spline = std::find_if(next.bsplines.begin(), next.bsplines.end(),
            [&](const auto& value) { return value.id == block.geometry_ids.front(); });
        if (spline == next.bsplines.end() ||
            spline->control_point_ids != block.point_ids) continue;
        for (std::size_t index = 0; index < block.point_ids.size(); ++index) {
            auto* point = next.find_point(block.point_ids[index]);
            if (point == nullptr) continue;
            const auto source_index = static_cast<std::size_t>(std::lround(
                static_cast<double>(index) *
                (reference->cached_points.size() - 1) /
                static_cast<double>(block.point_ids.size() - 1)));
            point->x = reference->cached_points[source_index][0];
            point->y = reference->cached_points[source_index][1];
        }
    }
    next.validate();
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Invalid ||
        solved.status == SolveStatus::Conflicting) {
        throw std::runtime_error(
            "Refreshed external reference conflicts with Sketch constraints");
    }
    *this = std::move(next);
    return true;
}

void Sketch::transform_import_block(
    const std::string& block_id, double translation_x,
    double translation_y, double rotation) {
    for (const double value : {translation_x, translation_y, rotation}) {
        require_finite(value, "import block transform");
    }
    auto next = *this;
    auto block = std::find_if(next.import_blocks.begin(), next.import_blocks.end(),
        [&](const auto& value) { return value.id == block_id; });
    if (block == next.import_blocks.end()) {
        throw std::invalid_argument("Sketch import block does not exist");
    }
    const double angle = rotation - block->rotation;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    for (const auto& point_id : block->point_ids) {
        auto* point = next.find_point(point_id);
        if (point == nullptr) throw std::runtime_error("Import block point is missing");
        const double local_x = point->x - block->translation_x;
        const double local_y = point->y - block->translation_y;
        point->x = cosine * local_x - sine * local_y + translation_x;
        point->y = sine * local_x + cosine * local_y + translation_y;
    }
    for (auto& arc : next.arcs) {
        if (std::ranges::find(block->geometry_ids, arc.id) != block->geometry_ids.end()) {
            arc.start_angle += angle;
            arc.end_angle += angle;
        }
    }
    for (auto& ellipse : next.ellipses) {
        if (std::ranges::find(block->geometry_ids, ellipse.id) != block->geometry_ids.end()) {
            ellipse.rotation += angle;
        }
    }
    for (auto& arc : next.elliptical_arcs) {
        if (std::ranges::find(block->geometry_ids, arc.id) != block->geometry_ids.end()) {
            arc.rotation += angle;
        }
    }
    block->translation_x = translation_x;
    block->translation_y = translation_y;
    block->rotation = rotation;
    next.validate();
    *this = std::move(next);
}

SketchDimension Sketch::create_segment_dimension(
    const std::string& segment_id, DimensionKind kind) const {
    const auto segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == segment_id; });
    if (segment == segments.end()) throw std::invalid_argument("Sketch segment does not exist");
    const auto* first = find_point(segment->first_point_id);
    const auto* second = find_point(segment->second_point_id);
    const double dx = second->x - first->x;
    const double dy = second->y - first->y;
    const double value = kind == DimensionKind::DistanceX ? dx
        : kind == DimensionKind::DistanceY ? dy
        : kind == DimensionKind::Angle
            ? std::atan2(dy, dx) * 180.0 / 3.14159265358979323846
            : std::hypot(dx, dy);
    SketchDimension result{
        make_id(), kind, first->id, second->id, value};
    // A length/projection dimension selected through a segment is still a
    // point-to-point relation. The segment is only the UI selection proxy.
    // Directional angle dimensions retain the segment identity because they
    // describe the line direction itself.
    if (kind == DimensionKind::Angle) result.geometry_id = segment->id;
    return result;
}

SketchDimension Sketch::create_point_dimension(
    const std::string& first_point_id, const std::string& second_point_id,
    DimensionKind kind) const {
    if (kind != DimensionKind::Distance && kind != DimensionKind::DistanceX &&
        kind != DimensionKind::DistanceY) {
        throw std::invalid_argument("Point dimension kind is unsupported");
    }
    const auto position = [&](const std::string& point_id)
        -> std::optional<std::array<double, 2>> {
        if (const auto* point = find_point(point_id)) {
            return std::array{point->x, point->y};
        }
        return external_point_position(*this, point_id);
    };
    const auto first = position(first_point_id);
    const auto second = position(second_point_id);
    if (!first || !second || (find_point(first_point_id) == nullptr &&
                              find_point(second_point_id) == nullptr)) {
        throw std::invalid_argument(
            "Point dimension requires a native and a valid point reference");
    }
    const double dx = (*second)[0] - (*first)[0];
    const double dy = (*second)[1] - (*first)[1];
    return {make_id(), kind, first_point_id, second_point_id,
            kind == DimensionKind::DistanceX ? dx
            : kind == DimensionKind::DistanceY ? dy : std::hypot(dx, dy)};
}

SketchDimension Sketch::create_three_point_angle_dimension(
    const std::string& first_point_id, const std::string& vertex_point_id,
    const std::string& second_point_id) const {
    const auto* first = find_point(first_point_id);
    const auto* vertex = find_point(vertex_point_id);
    const auto* second = find_point(second_point_id);
    if (first == nullptr || vertex == nullptr || second == nullptr ||
        first_point_id == vertex_point_id || first_point_id == second_point_id ||
        vertex_point_id == second_point_id) {
        throw std::invalid_argument("Three-point angle requires three distinct native points");
    }
    const double first_length = std::hypot(
        first->x - vertex->x, first->y - vertex->y);
    const double second_length = std::hypot(
        second->x - vertex->x, second->y - vertex->y);
    if (first_length <= 1.0e-12 || second_length <= 1.0e-12) {
        throw std::invalid_argument("Three-point angle rays must have nonzero length");
    }
    const double cosine = std::clamp(
        ((first->x - vertex->x) * (second->x - vertex->x) +
         (first->y - vertex->y) * (second->y - vertex->y)) /
            (first_length * second_length), -1.0, 1.0);
    SketchDimension result{make_id(), DimensionKind::AngleThreePoint,
        first_point_id, vertex_point_id,
        std::acos(cosine) * 180.0 / 3.14159265358979323846};
    result.geometry_id = second_point_id;
    return result;
}

SketchDimension Sketch::create_point_line_angle_dimension(
    const std::string& first_point_id, const std::string& second_point_id,
    const std::string& reference_line_id) const {
    const auto* first = find_point(first_point_id);
    const auto* second = find_point(second_point_id);
    const auto reference = sketch_axis_line(*this, reference_line_id)
        ? sketch_axis_line(*this, reference_line_id)
        : segment_or_external_line(*this, reference_line_id);
    if (first == nullptr || second == nullptr || first_point_id == second_point_id ||
        !reference) {
        throw std::invalid_argument(
            "Point-line angle requires two distinct points and a valid line");
    }
    const double dx = second->x - first->x;
    const double dy = second->y - first->y;
    const double reference_length = std::hypot(
        reference->second[0], reference->second[1]);
    const double driven_length = std::hypot(dx, dy);
    if (reference_length <= 1.0e-12 || driven_length <= 1.0e-12) {
        throw std::invalid_argument("Angle directions must have nonzero length");
    }
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::AngleBetween;
    result.first_point_id = first_point_id;
    result.second_point_id = second_point_id;
    result.geometry_id = reference_line_id;
    result.value = std::acos(std::clamp(
        (reference->second[0] * dx + reference->second[1] * dy) /
            (reference_length * driven_length), -1.0, 1.0)) *
        180.0 / 3.14159265358979323846;
    return result;
}

SketchDimension Sketch::create_four_point_angle_dimension(
    const std::string& first_point_id, const std::string& second_point_id,
    const std::string& third_point_id, const std::string& fourth_point_id) const {
    const auto* first = find_point(first_point_id);
    const auto* second = find_point(second_point_id);
    const auto* third = find_point(third_point_id);
    const auto* fourth = find_point(fourth_point_id);
    if (first == nullptr || second == nullptr || third == nullptr || fourth == nullptr ||
        first_point_id == second_point_id || third_point_id == fourth_point_id) {
        throw std::invalid_argument("Four-point angle requires two valid point pairs");
    }
    const double ax = second->x - first->x;
    const double ay = second->y - first->y;
    const double bx = fourth->x - third->x;
    const double by = fourth->y - third->y;
    const double scale = std::hypot(ax, ay) * std::hypot(bx, by);
    if (scale <= 1.0e-12) throw std::invalid_argument("Angle directions are degenerate");
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::AngleBetween;
    result.first_point_id = first_point_id;
    result.second_point_id = second_point_id;
    result.geometry_id = third_point_id;
    result.second_geometry_id = fourth_point_id;
    result.value = std::acos(std::clamp((ax * bx + ay * by) / scale, -1.0, 1.0)) *
        180.0 / 3.14159265358979323846;
    return result;
}

SketchDimension Sketch::create_axis_dimension(
    const std::string& point_id,
    const std::string& sketch_axis_id) const {
    const auto* point = find_point(point_id);
    if (point == nullptr ||
        (sketch_axis_id != "sketch_axis:x" &&
         sketch_axis_id != "sketch_axis:y")) {
        throw std::invalid_argument("Axis dimension input is invalid");
    }
    SketchDimension result{
        make_id(),
        sketch_axis_id == "sketch_axis:x"
            ? DimensionKind::DistanceY : DimensionKind::DistanceX,
        point_id, {},
        sketch_axis_id == "sketch_axis:x" ? point->y : point->x};
    // A coordinate-axis reference is not a disguised point-to-origin
    // dimension. Keep the selected axis as its persisted reference owner;
    // kind controls only whether the displayed/calculated form is X or Y.
    result.geometry_id = sketch_axis_id;
    return result;
}

SketchDimension Sketch::create_point_line_dimension(
    const std::string& point_id, const std::string& line_id) const {
    const auto* point = find_point(point_id);
    const auto line = sketch_axis_line(*this, line_id)
        ? sketch_axis_line(*this, line_id)
        : segment_or_external_line(*this, line_id);
    if (point == nullptr || !line) {
        throw std::invalid_argument(
            "Point-line dimension requires a native point and valid line");
    }
    const double length = std::hypot(line->second[0], line->second[1]);
    if (length <= 1.0e-12) {
        throw std::invalid_argument("Point-line dimension line has zero length");
    }
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::DistancePointLine;
    result.first_point_id = point_id;
    result.geometry_id = line_id;
    result.value = std::abs(
        line->second[0] * (point->y - line->first[1]) -
        line->second[1] * (point->x - line->first[0])) / length;
    return result;
}

SketchDimension Sketch::create_symmetric_dimension(
    const std::string& first_point_id,
    const std::string& second_point_id,
    const std::string& axis_id) const {
    auto result = create_point_line_dimension(first_point_id, axis_id);
    if (!second_point_id.empty()) {
        if (second_point_id == first_point_id ||
            find_point(second_point_id) == nullptr) {
            throw std::invalid_argument(
                "Symmetric dimension second point is invalid");
        }
        const auto line = sketch_axis_line(*this, axis_id)
            ? sketch_axis_line(*this, axis_id)
            : segment_or_external_line(*this, axis_id);
        const auto* second = find_point(second_point_id);
        const double length = std::hypot(line->second[0], line->second[1]);
        const double second_distance = std::abs(
            line->second[0] * (second->y - line->first[1]) -
            line->second[1] * (second->x - line->first[0])) / length;
        result.value = result.value + second_distance;
        result.second_point_id = second_point_id;
    } else {
        result.value *= 2.0;
    }
    result.kind = DimensionKind::DistanceSymmetric;
    return result;
}

SketchDimension Sketch::create_line_pair_dimension(
    const std::string& reference_line_id,
    const std::string& driven_line_id,
    DimensionKind kind) const {
    if (kind != DimensionKind::DistanceLine &&
        kind != DimensionKind::AngleBetween) {
        throw std::invalid_argument("Line-pair dimension kind is unsupported");
    }
    const auto reference = sketch_axis_line(*this, reference_line_id)
        ? sketch_axis_line(*this, reference_line_id)
        : segment_or_external_line(*this, reference_line_id);
    const auto driven = segment_or_external_line(*this, driven_line_id);
    const auto driven_segment = std::find_if(segments.begin(), segments.end(),
        [&](const auto& value) { return value.id == driven_line_id; });
    if (!reference || !driven || driven_segment == segments.end() ||
        reference_line_id == driven_line_id) {
        throw std::invalid_argument(
            "Line-pair dimension requires a valid reference and native driven segment");
    }
    const auto& rv = reference->second;
    const auto& dv = driven->second;
    const double reference_length = std::hypot(rv[0], rv[1]);
    const double driven_length = std::hypot(dv[0], dv[1]);
    if (reference_length <= 1.0e-12 || driven_length <= 1.0e-12) {
        throw std::invalid_argument("Line-pair dimension requires non-zero lines");
    }
    if (kind == DimensionKind::DistanceLine &&
        std::abs(rv[0] * dv[1] - rv[1] * dv[0]) >
            reference_length * driven_length * 1.0e-7) {
        throw std::invalid_argument(
            "Line distance requires parallel reference lines");
    }
    double value{};
    if (kind == DimensionKind::AngleBetween) {
        const double cosine = std::clamp(
            (rv[0] * dv[0] + rv[1] * dv[1]) /
                (reference_length * driven_length), -1.0, 1.0);
        value = std::acos(cosine) * 180.0 / 3.14159265358979323846;
    } else {
        const double cross = rv[0] *
            (driven->first[1] - reference->first[1]) - rv[1] *
            (driven->first[0] - reference->first[0]);
        value = std::abs(cross) / reference_length;
    }
    SketchDimension result;
    result.id = make_id();
    result.kind = kind;
    result.value = value;
    result.geometry_id = reference_line_id;
    result.second_geometry_id = driven_line_id;
    return result;
}

SketchDimension Sketch::create_circle_radius_dimension(
    const std::string& circle_id) const {
    const auto circle = std::find_if(circles.begin(), circles.end(),
        [&](const auto& value) { return value.id == circle_id; });
    if (circle == circles.end()) throw std::invalid_argument("Sketch circle does not exist");
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::Radius;
    result.value = circle->radius;
    result.geometry_id = circle->id;
    return result;
}

SketchDimension Sketch::create_circle_diameter_dimension(
    const std::string& circle_id) const {
    const auto circle = std::find_if(circles.begin(), circles.end(),
        [&](const auto& value) { return value.id == circle_id; });
    if (circle == circles.end()) throw std::invalid_argument("Sketch circle does not exist");
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::Diameter;
    result.value = circle->radius * 2.0;
    result.geometry_id = circle->id;
    return result;
}

SketchDimension Sketch::create_arc_radius_dimension(const std::string& arc_id) const {
    const auto arc = std::find_if(arcs.begin(), arcs.end(),
        [&](const auto& value) { return value.id == arc_id; });
    const auto corner = std::find_if(corner_radii.begin(), corner_radii.end(),
        [&](const auto& value) { return value.id == arc_id; });
    if (arc == arcs.end() && corner == corner_radii.end()) {
        throw std::invalid_argument("Sketch arc does not exist");
    }
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::Radius;
    result.value = arc != arcs.end() ? arc->radius : corner->radius;
    result.geometry_id = arc_id;
    return result;
}

SketchDimension Sketch::create_arc_diameter_dimension(
    const std::string& arc_id) const {
    auto result = create_arc_radius_dimension(arc_id);
    result.kind = DimensionKind::Diameter;
    result.value *= 2.0;
    return result;
}

SketchDimension Sketch::create_ellipse_radius_dimension(
    const std::string& ellipse_id, bool major) const {
    const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
        [&](const auto& value) { return value.id == ellipse_id; });
    if (ellipse == ellipses.end()) throw std::invalid_argument("Sketch ellipse does not exist");
    SketchDimension result;
    result.id = make_id();
    result.kind = major ? DimensionKind::EllipseMajorRadius
                        : DimensionKind::EllipseMinorRadius;
    result.value = major ? ellipse->major_radius : ellipse->minor_radius;
    result.geometry_id = ellipse->id;
    return result;
}

SketchDimension Sketch::create_ellipse_rotation_dimension(
    const std::string& ellipse_id) const {
    const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
        [&](const auto& value) { return value.id == ellipse_id; });
    if (ellipse == ellipses.end()) throw std::invalid_argument("Sketch ellipse does not exist");
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::EllipseRotation;
    result.value = ellipse->rotation * 180.0 / 3.14159265358979323846;
    result.geometry_id = ellipse->id;
    return result;
}

void Sketch::apply_dimension(SketchDimension dimension) {
    if (dimension.id.empty()) throw std::invalid_argument("Sketch dimension ID is required");
    auto next = *this;
    const auto existing = std::find_if(next.dimensions.begin(), next.dimensions.end(),
        [&](const auto& value) { return value.id == dimension.id; });
    const bool editing_existing_dimension = existing != next.dimensions.end();
    // A reference dimension measures the current solved geometry; it must
    // never become an indirect geometry-edit command merely because its
    // value field was changed before Driving was unchecked.  When editing an
    // existing driver, its last solved value is already the authoritative
    // measurement. Newly created reference dimensions are initialized from
    // the geometry by every create_*_dimension() factory.
    if (!dimension.driving && editing_existing_dimension) {
        dimension.value = existing->value;
    }
    const auto dimension_kind = dimension.kind;
    const auto dimension_geometry_id = dimension.geometry_id;
    const double dimension_value = dimension.value;
    const bool inserting_driving_dimension =
        !editing_existing_dimension && dimension.driving;
    if (!editing_existing_dimension) {
        const bool same_driver = std::any_of(
            next.dimensions.begin(), next.dimensions.end(), [&](const auto& value) {
                return !value.suppressed && value.driving && dimension.driving &&
                    ((value.kind == dimension.kind) ||
                     ((value.kind == DimensionKind::Radius ||
                       value.kind == DimensionKind::Diameter) &&
                      (dimension.kind == DimensionKind::Radius ||
                       dimension.kind == DimensionKind::Diameter))) &&
                    ((dimension.kind == DimensionKind::Radius ||
                      dimension.kind == DimensionKind::Diameter ||
                      dimension.kind == DimensionKind::EllipseMajorRadius ||
                      dimension.kind == DimensionKind::EllipseMinorRadius ||
                      dimension.kind == DimensionKind::EllipseRotation)
                        ? value.geometry_id == dimension.geometry_id
                        : (dimension.kind == DimensionKind::DistanceLine ||
                           dimension.kind == DimensionKind::AngleBetween)
                            ? (value.geometry_id == dimension.geometry_id &&
                               value.second_geometry_id ==
                                   dimension.second_geometry_id) ||
                              (value.kind == DimensionKind::AngleBetween &&
                               value.geometry_id ==
                                   dimension.second_geometry_id &&
                               value.second_geometry_id ==
                                   dimension.geometry_id)
                        : (dimension.kind == DimensionKind::DistancePointLine ||
                           dimension.kind == DimensionKind::DistanceSymmetric)
                            ? value.first_point_id == dimension.first_point_id &&
                              value.second_point_id == dimension.second_point_id &&
                              value.geometry_id == dimension.geometry_id
                        : dimension.kind == DimensionKind::AngleThreePoint
                            ? value.first_point_id == dimension.first_point_id &&
                              value.second_point_id == dimension.second_point_id &&
                              value.geometry_id == dimension.geometry_id
                        : value.first_point_id == dimension.first_point_id &&
                          value.second_point_id == dimension.second_point_id);
            });
        if (same_driver) throw std::invalid_argument("Segment already owns this driving dimension");
        next.dimensions.push_back(std::move(dimension));
    } else {
        *existing = std::move(dimension);
    }
    const bool reference_dimension = editing_existing_dimension
        ? !existing->driving : !next.dimensions.back().driving;
    if (reference_dimension) {
        if (!refresh_reference_dimensions(next)) {
            throw std::runtime_error(
                "Reference dimension measurement lies outside its limits");
        }
        next.validate();
        *this = std::move(next);
        return;
    }
    if (dimension_kind == DimensionKind::Radius ||
        dimension_kind == DimensionKind::Diameter) {
        const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) { return value.id == dimension_geometry_id; });
        if (circle != next.circles.end()) {
            circle->radius = dimension_kind == DimensionKind::Diameter
                ? dimension_value * 0.5 : dimension_value;
        } else {
            const auto arc = std::find_if(next.arcs.begin(), next.arcs.end(),
                [&](const auto& value) { return value.id == dimension_geometry_id; });
            if (arc == next.arcs.end()) {
                const auto corner = std::find_if(
                    next.corner_radii.begin(), next.corner_radii.end(),
                    [&](const auto& value) {
                        return value.id == dimension_geometry_id;
                    });
                if (corner == next.corner_radii.end()) {
                    throw std::invalid_argument(
                        "Radius dimension geometry does not exist");
                }
                corner->radius = dimension_kind == DimensionKind::Diameter
                    ? dimension_value * 0.5 : dimension_value;
            } else {
                arc->radius = dimension_kind == DimensionKind::Diameter
                    ? dimension_value * 0.5 : dimension_value;
                const auto* center = next.find_point(arc->center_point_id);
                auto* start = next.find_point(arc->start_point_id);
                auto* end = next.find_point(arc->end_point_id);
                start->x = center->x + arc->radius * std::cos(arc->start_angle);
                start->y = center->y + arc->radius * std::sin(arc->start_angle);
                end->x = center->x + arc->radius * std::cos(arc->end_angle);
                end->y = center->y + arc->radius * std::sin(arc->end_angle);
            }
        }
    } else if (dimension_kind == DimensionKind::EllipseMajorRadius ||
               dimension_kind == DimensionKind::EllipseMinorRadius) {
        const auto ellipse = std::find_if(next.ellipses.begin(), next.ellipses.end(),
            [&](const auto& value) { return value.id == dimension_geometry_id; });
        if (ellipse == next.ellipses.end()) {
            throw std::invalid_argument("Ellipse dimension geometry does not exist");
        }
        const auto* center = next.find_point(ellipse->center_point_id);
        if (dimension_kind == DimensionKind::EllipseMajorRadius) {
            ellipse->major_radius = dimension_value;
            auto* point = next.find_point(ellipse->major_point_id);
            point->x = center->x + dimension_value * std::cos(ellipse->rotation);
            point->y = center->y + dimension_value * std::sin(ellipse->rotation);
        } else {
            ellipse->minor_radius = dimension_value;
            auto* point = next.find_point(ellipse->minor_point_id);
            const double orientation = ellipse->reversed ? -1.0 : 1.0;
            point->x = center->x - orientation * dimension_value *
                std::sin(ellipse->rotation);
            point->y = center->y + orientation * dimension_value *
                std::cos(ellipse->rotation);
        }
    } else if (dimension_kind == DimensionKind::EllipseRotation) {
        const auto ellipse = std::find_if(next.ellipses.begin(), next.ellipses.end(),
            [&](const auto& value) { return value.id == dimension_geometry_id; });
        if (ellipse == next.ellipses.end()) {
            throw std::invalid_argument("Ellipse rotation geometry does not exist");
        }
        ellipse->rotation = dimension_value * 3.14159265358979323846 / 180.0;
        const auto* center = next.find_point(ellipse->center_point_id);
        auto* major = next.find_point(ellipse->major_point_id);
        auto* minor = next.find_point(ellipse->minor_point_id);
        const double orientation = ellipse->reversed ? -1.0 : 1.0;
        major->x = center->x + ellipse->major_radius * std::cos(ellipse->rotation);
        major->y = center->y + ellipse->major_radius * std::sin(ellipse->rotation);
        minor->x = center->x - orientation * ellipse->minor_radius *
            std::sin(ellipse->rotation);
        minor->y = center->y + orientation * ellipse->minor_radius *
            std::cos(ellipse->rotation);
    }
    next.validate();
    const bool needs_rank_for_redundancy = inserting_driving_dimension &&
        dimension_kind != DimensionKind::EllipseMajorRadius &&
        dimension_kind != DimensionKind::EllipseMinorRadius &&
        dimension_kind != DimensionKind::EllipseRotation;
    // Editing a value leaves the equation graph unchanged. Recompute rank
    // only when inserting a driver whose redundancy must be diagnosed.
    const auto result = next.solve_impl(100, needs_rank_for_redundancy);
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Sketch dimension conflicts with existing geometry");
    }
    if (inserting_driving_dimension &&
        dimension_kind != DimensionKind::EllipseMajorRadius &&
        dimension_kind != DimensionKind::EllipseMinorRadius &&
        dimension_kind != DimensionKind::EllipseRotation) {
        require_constraint_dof_reduction(
            *this, result, "Sketch dimension is redundant");
    }
    *this = std::move(next);
}

SolveResult Sketch::solve(std::size_t maximum_iterations) {
    return solve_impl(maximum_iterations, true);
}

SolveResult Sketch::solve_impl(
    std::size_t maximum_iterations, bool calculate_degrees_of_freedom,
    const std::vector<std::string>& preferred_point_ids) {
    point_lookup_indices_.clear();
    point_lookup_indices_.reserve(points.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        point_lookup_indices_.emplace(points[index].id, index);
    }
    point_lookup_active_ = true;
    struct PointLookupGuard {
        bool* active;
        std::unordered_map<std::string, std::size_t>* indices;
        ~PointLookupGuard() {
            *active = false;
            indices->clear();
        }
    } point_lookup_guard{&point_lookup_active_, &point_lookup_indices_};
    try { validate(); } catch (const std::exception&) { return {SolveStatus::Invalid, 0, 0.0}; }
    if (maximum_iterations == 0) return {SolveStatus::Invalid, 0, 0.0};
    const auto original_points = points;
    const auto original_circles = circles;
    const auto original_arcs = arcs;
    const auto original_corner_radii = corner_radii;
    const auto original_ellipses = ellipses;
    const auto original_elliptical_arcs = elliptical_arcs;
    const auto original_dimensions = dimensions;
    constexpr double tolerance = 1.0e-8;
    double maximum_residual{};
    const auto linked_points = externally_linked_point_ids(*this);
    const std::unordered_set<std::string> preferred_points(
        preferred_point_ids.begin(), preferred_point_ids.end());
    const auto immutable = [&](const SketchPoint& point) {
        // During a drag the requested point is the interaction root. Treat it
        // as a temporary solver anchor so corrections propagate away from the
        // cursor into movable branches instead of pulling the handle back.
        return point.fixed || linked_points.contains(point.id) ||
            preferred_points.contains(point.id);
    };
    std::unordered_set<std::string> centerline_points;
    for (const auto& segment : segments) {
        if (!segment.centerline) continue;
        centerline_points.insert(segment.first_point_id);
        centerline_points.insert(segment.second_point_id);
    }
    const auto move_pair = [&](SketchPoint& first, SketchPoint& second,
                              double dx, double dy) {
        if (immutable(first) && immutable(second)) return false;
        if (immutable(first)) { second.x += dx; second.y += dy; }
        else if (immutable(second)) { first.x -= dx; first.y -= dy; }
        else if (centerline_points.contains(first.id) &&
                 !centerline_points.contains(second.id)) {
            second.x += dx;
            second.y += dy;
        } else if (centerline_points.contains(second.id) &&
                   !centerline_points.contains(first.id)) {
            first.x -= dx;
            first.y -= dy;
        }
        else {
            first.x -= dx * 0.5; first.y -= dy * 0.5;
            second.x += dx * 0.5; second.y += dy * 0.5;
        }
        return true;
    };
    const auto point_position = [&](const std::string& point_id)
        -> std::optional<std::array<double, 2>> {
        if (const auto* point = find_point(point_id)) {
            return std::array{point->x, point->y};
        }
        return external_point_position(*this, point_id);
    };
    const auto move_point_pair = [&](const std::string& first_id,
                                     const std::string& second_id,
                                     double dx, double dy) {
        auto* first = find_point(first_id);
        auto* second = find_point(second_id);
        if (first && second) return move_pair(*first, *second, dx, dy);
        if (first) {
            if (immutable(*first)) return false;
            first->x -= dx;
            first->y -= dy;
            return true;
        }
        if (second) {
            if (immutable(*second)) return false;
            second->x += dx;
            second->y += dy;
            return true;
        }
        return false;
    };
    const auto project_common_tangent_segments = [&] {
        constexpr double turn = 2.0 * 3.14159265358979323846;
        for (const auto& segment : segments) {
            std::vector<std::string> tangent_curves;
            for (const auto& constraint : constraints) {
                if (constraint.suppressed ||
                    constraint.kind != ConstraintKind::Tangent) continue;
                if (constraint.geometry_id == segment.id) {
                    tangent_curves.push_back(constraint.second_geometry_id);
                } else if (constraint.second_geometry_id == segment.id) {
                    tangent_curves.push_back(constraint.geometry_id);
                }
            }
            if (tangent_curves.size() != 2) continue;
            const auto endpoint_for = [&](const std::string& curve_id)
                    -> std::optional<std::string> {
                for (const auto& constraint : constraints) {
                    if (!constraint.suppressed &&
                        constraint.kind == ConstraintKind::PointOnCircle &&
                        constraint.geometry_id == curve_id &&
                        (constraint.first_point_id == segment.first_point_id ||
                         constraint.first_point_id == segment.second_point_id)) {
                        return constraint.first_point_id;
                    }
                }
                return std::nullopt;
            };
            const auto first_id = endpoint_for(tangent_curves[0]);
            const auto second_id = endpoint_for(tangent_curves[1]);
            const auto first_center_id = center_curve_point_id(*this, tangent_curves[0]);
            const auto second_center_id = center_curve_point_id(*this, tangent_curves[1]);
            const auto first_radius = circular_curve_radius(*this, tangent_curves[0]);
            const auto second_radius = circular_curve_radius(*this, tangent_curves[1]);
            if (!first_id || !second_id || *first_id == *second_id ||
                !first_center_id || !second_center_id ||
                !first_radius || !second_radius) continue;
            auto* first = find_point(*first_id);
            auto* second = find_point(*second_id);
            const auto* first_center = find_point(*first_center_id);
            const auto* second_center = find_point(*second_center_id);
            if (first == nullptr || second == nullptr || first_center == nullptr ||
                second_center == nullptr || immutable(*first) || immutable(*second)) {
                continue;
            }
            const double current_dx = second->x - first->x;
            const double current_dy = second->y - first->y;
            const double current_length = std::hypot(current_dx, current_dy);
            const double center_dx = second_center->x - first_center->x;
            const double center_dy = second_center->y - first_center->y;
            const double center_length = std::hypot(center_dx, center_dy);
            if (current_length <= 1.0e-12 || center_length <= 1.0e-12) continue;
            const double current_nx = -current_dy / current_length;
            const double current_ny = current_dx / current_length;
            const double first_side =
                ((first_center->x - first->x) * current_nx +
                 (first_center->y - first->y) * current_ny) >= 0.0 ? 1.0 : -1.0;
            const double second_side =
                ((second_center->x - second->x) * current_nx +
                 (second_center->y - second->y) * current_ny) >= 0.0 ? 1.0 : -1.0;
            const double normal_along_centers =
                (second_side * *second_radius - first_side * *first_radius) /
                center_length;
            if (std::abs(normal_along_centers) > 1.0 + 1.0e-10) continue;
            const double ux = center_dx / center_length;
            const double uy = center_dy / center_length;
            const double perpendicular = std::sqrt(std::max(
                0.0, 1.0 - normal_along_centers * normal_along_centers));
            struct Candidate { double nx, ny, first_x, first_y, second_x, second_y, error; };
            std::optional<Candidate> best;
            for (const double branch : {-1.0, 1.0}) {
                const double nx = normal_along_centers * ux -
                    branch * perpendicular * uy;
                const double ny = normal_along_centers * uy +
                    branch * perpendicular * ux;
                Candidate candidate{nx, ny,
                    first_center->x - first_side * *first_radius * nx,
                    first_center->y - first_side * *first_radius * ny,
                    second_center->x - second_side * *second_radius * nx,
                    second_center->y - second_side * *second_radius * ny, 0.0};
                candidate.error = std::hypot(candidate.first_x - first->x,
                    candidate.first_y - first->y) +
                    std::hypot(candidate.second_x - second->x,
                        candidate.second_y - second->y);
                if (!best || candidate.error < best->error) best = candidate;
            }
            if (!best) continue;
            first->x = best->first_x; first->y = best->first_y;
            second->x = best->second_x; second->y = best->second_y;
            for (auto& arc : arcs) {
                const auto update_endpoint = [&](const std::string& endpoint_id,
                                                  bool start) {
                    if (endpoint_id != *first_id && endpoint_id != *second_id) return;
                    const auto* endpoint = find_point(endpoint_id);
                    const auto* center = find_point(arc.center_point_id);
                    double angle = std::atan2(
                        endpoint->y - center->y, endpoint->x - center->x);
                    const double old = start ? arc.start_angle : arc.end_angle;
                    while (angle < old - 3.14159265358979323846) angle += turn;
                    while (angle > old + 3.14159265358979323846) angle -= turn;
                    if (start) arc.start_angle = angle;
                    else arc.end_angle = angle;
                };
                update_endpoint(arc.start_point_id, true);
                update_endpoint(arc.end_point_id, false);
                while (arc.end_angle <= arc.start_angle) arc.end_angle += turn;
            }
        }
    };
    const auto directional_translation_closure = [&](
            const std::string& root_id, DimensionKind kind) {
        std::set<std::string> result;
        if (find_point(root_id) == nullptr) return result;
        result.insert(root_id);
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& constraint : constraints) {
                if (constraint.suppressed) continue;
                const bool transfers_coordinate =
                    constraint.kind == ConstraintKind::Coincident ||
                    (kind == DimensionKind::DistanceX &&
                     constraint.kind == ConstraintKind::Vertical) ||
                    (kind == DimensionKind::DistanceY &&
                     constraint.kind == ConstraintKind::Horizontal);
                if (!transfers_coordinate ||
                    find_point(constraint.first_point_id) == nullptr ||
                    find_point(constraint.second_point_id) == nullptr) {
                    continue;
                }
                if (result.contains(constraint.first_point_id) &&
                    result.insert(constraint.second_point_id).second) {
                    changed = true;
                }
                if (result.contains(constraint.second_point_id) &&
                    result.insert(constraint.first_point_id).second) {
                    changed = true;
                }
            }
        }
        return result;
    };
    const auto directional_group_anchored = [&](
            const std::set<std::string>& group) {
        if (std::any_of(group.begin(), group.end(), [&](const auto& point_id) {
                const auto* point = find_point(point_id);
                return point == nullptr || immutable(*point);
            })) return true;
        return std::any_of(constraints.begin(), constraints.end(),
            [&](const auto& constraint) {
                if (constraint.suppressed ||
                    constraint.kind != ConstraintKind::Coincident) return false;
                const bool first_native =
                    find_point(constraint.first_point_id) != nullptr;
                const bool second_native =
                    find_point(constraint.second_point_id) != nullptr;
                return (first_native &&
                        group.contains(constraint.first_point_id) &&
                        !second_native && external_point_position(
                            *this, constraint.second_point_id)) ||
                    (second_native &&
                     group.contains(constraint.second_point_id) &&
                     !first_native && external_point_position(
                         *this, constraint.first_point_id));
            });
    };
    const auto move_directional_pair = [&](const std::string& first_id,
            const std::string& second_id, DimensionKind kind,
            double correction) {
        const auto first_group = directional_translation_closure(first_id, kind);
        const auto second_group = directional_translation_closure(second_id, kind);
        const bool first_external = first_group.empty() &&
            external_point_position(*this, first_id).has_value();
        const bool second_external = second_group.empty() &&
            external_point_position(*this, second_id).has_value();
        if ((!first_external && first_group.empty()) ||
            (!second_external && second_group.empty()) ||
            std::any_of(first_group.begin(), first_group.end(),
                [&](const auto& point_id) {
                    return second_group.contains(point_id);
                })) return false;
        const bool first_anchored = first_external ||
            directional_group_anchored(first_group);
        const bool second_anchored = second_external ||
            directional_group_anchored(second_group);
        if (first_anchored && second_anchored) return false;
        const double first_shift = second_anchored ? -correction
            : first_anchored ? 0.0 : -correction * 0.5;
        const double second_shift = first_anchored ? correction
            : second_anchored ? 0.0 : correction * 0.5;
        const auto shift = [&](const std::set<std::string>& group, double value) {
            for (const auto& point_id : group) {
                auto* point = find_point(point_id);
                if (kind == DimensionKind::DistanceX) point->x += value;
                else point->y += value;
            }
        };
        shift(first_group, first_shift);
        shift(second_group, second_shift);
        return true;
    };
    const auto synchronize_curves = [&]() {
        constexpr double full_turn = 2.0 * 3.14159265358979323846;
        for (auto& arc : arcs) {
            const auto* center = find_point(arc.center_point_id);
            const auto* start = find_point(arc.start_point_id);
            const auto* end = find_point(arc.end_point_id);
            const double start_radius = std::hypot(
                start->x - center->x, start->y - center->y);
            const double end_radius = std::hypot(
                end->x - center->x, end->y - center->y);
            if (start_radius <= tolerance || end_radius <= tolerance ||
                std::abs(start_radius - end_radius) > 1.0e-7) return false;
            arc.radius = (start_radius + end_radius) * 0.5;
            arc.start_angle = std::atan2(
                start->y - center->y, start->x - center->x);
            arc.end_angle = std::atan2(
                end->y - center->y, end->x - center->x);
            while (arc.end_angle <= arc.start_angle) arc.end_angle += full_turn;
            if (arc.end_angle - arc.start_angle >= full_turn - 1.0e-10) return false;
        }
        for (auto& ellipse : ellipses) {
            const auto* center = find_point(ellipse.center_point_id);
            const auto* major = find_point(ellipse.major_point_id);
            const auto* minor = find_point(ellipse.minor_point_id);
            const double major_x = major->x - center->x;
            const double major_y = major->y - center->y;
            const double minor_x = minor->x - center->x;
            const double minor_y = minor->y - center->y;
            const double major_radius = std::hypot(major_x, major_y);
            const double minor_radius = std::hypot(minor_x, minor_y);
            if (major_radius <= tolerance || minor_radius <= tolerance ||
                std::abs(major_x * minor_x + major_y * minor_y) >
                    1.0e-7 * major_radius * minor_radius) return false;
            const double orientation = ellipse.reversed ? -1.0 : 1.0;
            if (orientation * (major_x * minor_y - major_y * minor_x) <= 0.0) {
                return false;
            }
            ellipse.major_radius = major_radius;
            ellipse.minor_radius = minor_radius;
            ellipse.rotation = std::atan2(major_y, major_x);
        }
        for (auto& arc : elliptical_arcs) {
            const auto* center = find_point(arc.center_point_id);
            const auto* major = find_point(arc.major_point_id);
            const auto* minor = find_point(arc.minor_point_id);
            const auto* start = find_point(arc.start_point_id);
            const auto* end = find_point(arc.end_point_id);
            const double major_x = major->x - center->x;
            const double major_y = major->y - center->y;
            const double minor_x = minor->x - center->x;
            const double minor_y = minor->y - center->y;
            const double major_radius = std::hypot(major_x, major_y);
            const double minor_radius = std::hypot(minor_x, minor_y);
            if (major_radius <= tolerance || minor_radius <= tolerance ||
                std::abs(major_x * minor_x + major_y * minor_y) >
                    1.0e-7 * major_radius * minor_radius) return false;
            const double orientation = arc.reversed ? -1.0 : 1.0;
            if (orientation * (major_x * minor_y - major_y * minor_x) <= 0.0) {
                return false;
            }
            const double rotation = std::atan2(major_y, major_x);
            const auto normalized_radius = [&](const SketchPoint* point) {
                const double dx = point->x - center->x;
                const double dy = point->y - center->y;
                const double local_x =
                    (dx * std::cos(rotation) + dy * std::sin(rotation)) /
                    major_radius;
                const double local_y =
                    (-dx * std::sin(rotation) + dy * std::cos(rotation)) /
                    minor_radius;
                return local_x * local_x + local_y * local_y;
            };
            if (std::abs(normalized_radius(start) - 1.0) > 1.0e-7 ||
                std::abs(normalized_radius(end) - 1.0) > 1.0e-7) return false;
            double start_parameter = unwrap_near(
                ellipse_parameter(
                    center->x, center->y, major_radius, minor_radius,
                    rotation, arc.reversed, start->x, start->y),
                arc.start_parameter);
            double end_parameter = unwrap_near(
                ellipse_parameter(
                    center->x, center->y, major_radius, minor_radius,
                    rotation, arc.reversed, end->x, end->y),
                arc.end_parameter);
            while (end_parameter <= start_parameter) end_parameter += full_turn;
            while (end_parameter - start_parameter >= full_turn) {
                end_parameter -= full_turn;
            }
            if (end_parameter <= start_parameter ||
                end_parameter - start_parameter >= full_turn - 1.0e-10) {
                return false;
            }
            arc.major_radius = major_radius;
            arc.minor_radius = minor_radius;
            arc.rotation = rotation;
            arc.start_parameter = start_parameter;
            arc.end_parameter = end_parameter;
        }
        return true;
    };
    const auto resize_circular_curve = [&](const std::string& geometry_id,
                                            double target_radius,
                                            const std::set<std::string>&
                                                protected_points) {
        std::unordered_map<std::string, std::array<double, 2>> translations;
        const auto add_radial_target = [&](const std::string& point_id,
                                           double target_x,
                                           double target_y) {
            const auto* root = find_point(point_id);
            if (root == nullptr) return false;
            const std::array delta{target_x - root->x, target_y - root->y};
            if (std::hypot(delta[0], delta[1]) <= tolerance) return true;
            const auto closure = point_translation_closure(*this, point_id);
            if (closure.empty()) return false;
            for (const auto& dependent_id : closure) {
                const auto* dependent = find_point(dependent_id);
                if (dependent == nullptr || immutable(*dependent) ||
                    protected_points.contains(dependent_id)) {
                    return false;
                }
                const auto existing = translations.find(dependent_id);
                if (existing != translations.end() &&
                    std::hypot(existing->second[0] - delta[0],
                               existing->second[1] - delta[1]) > tolerance) {
                    return false;
                }
                translations[dependent_id] = delta;
            }
            return true;
        };
        if (auto circle = std::find_if(
                circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            circle != circles.end()) {
            const auto* center = find_point(circle->center_point_id);
            if (center == nullptr) return false;
            for (const auto& point_id : circular_curve_radial_points(
                    *this, geometry_id)) {
                const auto* point = find_point(point_id);
                if (point == nullptr) return false;
                const double dx = point->x - center->x;
                const double dy = point->y - center->y;
                const double distance = std::hypot(dx, dy);
                if (distance <= tolerance || !add_radial_target(
                        point_id,
                        center->x + dx * target_radius / distance,
                        center->y + dy * target_radius / distance)) {
                    return false;
                }
            }
            circle->radius = target_radius;
        } else if (auto arc = std::find_if(
                       arcs.begin(), arcs.end(),
                       [&](const auto& value) { return value.id == geometry_id; });
                   arc != arcs.end()) {
            const auto* center = find_point(arc->center_point_id);
            if (center == nullptr || !add_radial_target(
                    arc->start_point_id,
                    center->x + target_radius * std::cos(arc->start_angle),
                    center->y + target_radius * std::sin(arc->start_angle)) ||
                !add_radial_target(
                    arc->end_point_id,
                    center->x + target_radius * std::cos(arc->end_angle),
                    center->y + target_radius * std::sin(arc->end_angle))) {
                return false;
            }
            arc->radius = target_radius;
        } else {
            return false;
        }
        for (const auto& [point_id, delta] : translations) {
            auto* point = find_point(point_id);
            point->x += delta[0];
            point->y += delta[1];
        }
        return true;
    };
    for (std::size_t iteration = 0; iteration < maximum_iterations; ++iteration) {
        project_common_tangent_segments();
        maximum_residual = 0.0;
        bool immovable_conflict = false;
        for (const auto& constraint : constraints) {
            if (constraint.suppressed) continue;
            if (constraint.kind == ConstraintKind::Tangent) {
                const bool reference_is_segment = std::any_of(
                    segments.begin(), segments.end(), [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                const bool driven_is_segment = std::any_of(
                    segments.begin(), segments.end(), [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                const bool reference_is_line = reference_is_segment ||
                    is_base_sketch_axis(constraint.geometry_id);
                const bool driven_is_line = driven_is_segment ||
                    is_base_sketch_axis(constraint.second_geometry_id);
                if (!reference_is_line && !driven_is_line) {
                    const auto reference_curve = tangent_curve_data(
                        *this, constraint.geometry_id);
                    const auto driven_curve = tangent_curve_data(
                        *this, constraint.second_geometry_id);
                    std::set<std::string> translated;
                    double correction_x{};
                    double correction_y{};
                    double residual{};
                    bool state_valid{};
                    if (reference_curve && driven_curve &&
                        reference_curve->circular_radius &&
                        driven_curve->circular_radius) {
                        const auto state = curve_pair_tangent_state(
                            *this, constraint.geometry_id,
                            constraint.second_geometry_id,
                            constraint.tangent_internal);
                        if (state && state->contact_on_reference &&
                            state->contact_on_driven) {
                            const double correction = state->target_distance -
                                state->center_distance;
                            correction_x = correction * state->direction_x;
                            correction_y = correction * state->direction_y;
                            residual = std::abs(correction);
                            translated = center_curve_translation_points(
                                *this, constraint.second_geometry_id);
                            state_valid = true;
                        }
                    } else {
                        const auto state = general_curve_pair_tangent_state(
                            *this, constraint.geometry_id,
                            constraint.second_geometry_id);
                        if (state) {
                            correction_x = state->correction_x;
                            correction_y = state->correction_y;
                            residual = state->distance;
                            translated = state->driven_point_ids;
                            state_valid = state->distance > tolerance ||
                                state->tangents_parallel;
                        }
                    }
                    if (!state_valid) {
                        maximum_residual = std::max(maximum_residual, 1.0);
                        immovable_conflict = true;
                        continue;
                    }
                    maximum_residual = std::max(maximum_residual, residual);
                    if (residual <= tolerance) continue;
                    const auto reference_points = center_curve_translation_points(
                        *this, constraint.geometry_id);
                    const bool blocked = translated.empty() || std::any_of(
                        translated.begin(), translated.end(),
                        [&](const auto& point_id) {
                            const auto* point = find_point(point_id);
                            return point == nullptr || immutable(*point) ||
                                reference_points.contains(point_id);
                        });
                    if (blocked) {
                        immovable_conflict = true;
                    } else {
                        for (const auto& point_id : translated) {
                            auto* point = find_point(point_id);
                            point->x += correction_x;
                            point->y += correction_y;
                        }
                    }
                    continue;
                }
                const std::string& segment_id = reference_is_line
                    ? constraint.geometry_id : constraint.second_geometry_id;
                const std::string& curve_id = reference_is_line
                    ? constraint.second_geometry_id : constraint.geometry_id;
                if (std::ranges::any_of(bsplines,
                        [&](const auto& value) { return value.id == curve_id; })) {
                    const auto state = segment_spline_tangent_state(
                        *this, segment_id, curve_id);
                    if (!state) {
                        maximum_residual = std::max(maximum_residual, 1.0);
                        immovable_conflict = true;
                        continue;
                    }
                    maximum_residual = std::max(
                        maximum_residual, state->residual);
                    if (state->residual <= tolerance) continue;
                    auto* contact = find_point(state->contact_point_id);
                    auto* other = find_point(state->other_point_id);
                    if (contact == nullptr || other == nullptr) {
                        immovable_conflict = true;
                        continue;
                    }
                    // For an endpoint-connected line, Tangent is a shape
                    // constraint on the spline. Preserve both the shared
                    // contact and the selected line, and rotate the adjacent
                    // spline control point around that contact. Interior
                    // contacts retain the established line correction because
                    // they do not have one unambiguous endpoint handle.
                    if (!state->spline_tangent_point_id.empty()) {
                        auto* tangent_point = find_point(
                            state->spline_tangent_point_id);
                        if (tangent_point == nullptr || immutable(*tangent_point)) {
                            immovable_conflict = true;
                            continue;
                        }
                        const double handle_length = std::hypot(
                            tangent_point->x - contact->x,
                            tangent_point->y - contact->y);
                        if (handle_length <= 1.0e-12) {
                            immovable_conflict = true;
                            continue;
                        }
                        const double segment_x =
                            (other->x - contact->x) / state->segment_length;
                        const double segment_y =
                            (other->y - contact->y) / state->segment_length;
                        const std::array forward{
                            contact->x + handle_length * segment_x,
                            contact->y + handle_length * segment_y};
                        const std::array reverse{
                            contact->x - handle_length * segment_x,
                            contact->y - handle_length * segment_y};
                        const auto& desired = std::hypot(
                            tangent_point->x - forward[0],
                            tangent_point->y - forward[1]) <= std::hypot(
                            tangent_point->x - reverse[0],
                            tangent_point->y - reverse[1]) ? forward : reverse;
                        tangent_point->x = desired[0];
                        tangent_point->y = desired[1];
                        continue;
                    }
                    if (immutable(*other)) {
                        immovable_conflict = true;
                        continue;
                    }
                    const std::array forward{
                        contact->x + state->segment_length * state->tangent_x,
                        contact->y + state->segment_length * state->tangent_y};
                    const std::array reverse{
                        contact->x - state->segment_length * state->tangent_x,
                        contact->y - state->segment_length * state->tangent_y};
                    const auto& desired = std::hypot(
                        other->x - forward[0], other->y - forward[1]) <=
                        std::hypot(other->x - reverse[0], other->y - reverse[1])
                        ? forward : reverse;
                    other->x = desired[0];
                    other->y = desired[1];
                    continue;
                }
                // Persisted endpoint ownership is stronger evidence than a
                // nearest-contact search. After an arc endpoint/radius edit,
                // the old line may no longer touch the new supporting circle;
                // a generic contact search then reports an out-of-domain
                // state before it gets a chance to restore endpoint tangency.
                if (const auto segment = std::ranges::find_if(segments,
                        [&](const auto& value) {
                            return value.id == segment_id;
                        }); segment != segments.end()) {
                    std::string shared_id;
                    const auto offer_shared = [&](const std::string& id) {
                        if (id == segment->first_point_id ||
                            id == segment->second_point_id) shared_id = id;
                    };
                    if (const auto arc = std::ranges::find_if(arcs,
                            [&](const auto& value) {
                                return value.id == curve_id;
                            }); arc != arcs.end()) {
                        offer_shared(arc->start_point_id);
                        offer_shared(arc->end_point_id);
                    }
                    if (const auto arc = std::ranges::find_if(elliptical_arcs,
                            [&](const auto& value) {
                                return value.id == curve_id;
                            }); arc != elliptical_arcs.end()) {
                        offer_shared(arc->start_point_id);
                        offer_shared(arc->end_point_id);
                    }
                    if (!shared_id.empty()) {
                        auto* contact = find_point(shared_id);
                        auto* other = find_point(
                            segment->first_point_id == shared_id
                                ? segment->second_point_id
                                : segment->first_point_id);
                        const auto tangent = contact == nullptr
                            ? std::optional<std::array<double, 2>>{}
                            : curve_tangent_at_point(
                                  curve_id, contact->x, contact->y);
                        const double segment_length = contact == nullptr ||
                                other == nullptr ? 0.0 : std::hypot(
                            other->x - contact->x, other->y - contact->y);
                        if (!tangent || segment_length <= 1.0e-12) {
                            maximum_residual = std::max(maximum_residual, 1.0);
                            immovable_conflict = true;
                            continue;
                        }
                        const double segment_x =
                            (other->x - contact->x) / segment_length;
                        const double segment_y =
                            (other->y - contact->y) / segment_length;
                        const double residual = std::abs(
                            segment_x * (*tangent)[1] -
                            segment_y * (*tangent)[0]);
                        maximum_residual = std::max(maximum_residual, residual);
                        if (residual <= tolerance) continue;
                        if (immutable(*other)) {
                            immovable_conflict = true;
                            continue;
                        }
                        const std::array forward{
                            contact->x + segment_length * (*tangent)[0],
                            contact->y + segment_length * (*tangent)[1]};
                        const std::array reverse{
                            contact->x - segment_length * (*tangent)[0],
                            contact->y - segment_length * (*tangent)[1]};
                        const auto& desired = std::hypot(
                            other->x - forward[0], other->y - forward[1]) <=
                            std::hypot(other->x - reverse[0],
                                       other->y - reverse[1])
                            ? forward : reverse;
                        other->x = desired[0];
                        other->y = desired[1];
                        continue;
                    }
                }
                const auto state = segment_curve_tangent_state(
                    *this, segment_id, curve_id);
                if (!state || !state->contact_on_segment || !state->contact_on_curve) {
                    maximum_residual = std::max(maximum_residual, 1.0);
                    immovable_conflict = true;
                    continue;
                }
                const double correction =
                    state->target_distance - state->signed_distance;
                maximum_residual = std::max(
                    maximum_residual, std::abs(correction));
                if (std::abs(correction) <= tolerance) continue;
                // A curve translated against a line cannot move as one rigid
                // body when their tangent contact is also a shared endpoint.
                // That is the normal polyline/connected-curve state, not an
                // immovable conflict: preserve the contact and curve, and
                // rotate the free segment arm onto the exact curve tangent.
                // This is the analytic-curve equivalent of the endpoint
                // B-spline branch above.
                if (!state->first_point_id.empty() &&
                    !state->second_point_id.empty()) {
                    auto* first = find_point(state->first_point_id);
                    auto* second = find_point(state->second_point_id);
                    SketchPoint* contact = nullptr;
                    SketchPoint* other = nullptr;
                    if (first != nullptr && std::hypot(
                            first->x - state->contact_x,
                            first->y - state->contact_y) <= 1.0e-6) {
                        contact = first;
                        other = second;
                    } else if (second != nullptr && std::hypot(
                            second->x - state->contact_x,
                            second->y - state->contact_y) <= 1.0e-6) {
                        contact = second;
                        other = first;
                    }
                    if (contact != nullptr && other != nullptr) {
                        const auto tangent = curve_tangent_at_point(
                            curve_id, contact->x, contact->y);
                        const double segment_length = std::hypot(
                            other->x - contact->x,
                            other->y - contact->y);
                        if (!tangent || segment_length <= 1.0e-12 ||
                            immutable(*other)) {
                            immovable_conflict = true;
                            continue;
                        }
                        const std::array forward{
                            contact->x + segment_length * (*tangent)[0],
                            contact->y + segment_length * (*tangent)[1]};
                        const std::array reverse{
                            contact->x - segment_length * (*tangent)[0],
                            contact->y - segment_length * (*tangent)[1]};
                        const auto& desired = std::hypot(
                            other->x - forward[0], other->y - forward[1]) <=
                            std::hypot(other->x - reverse[0],
                                       other->y - reverse[1])
                            ? forward : reverse;
                        other->x = desired[0];
                        other->y = desired[1];
                        continue;
                    }
                }
                std::set<std::string> translated;
                std::set<std::string> reference_points;
                double translation_x{};
                double translation_y{};
                // Selection order must not make a persisted base axis the
                // driven object. When the axis was selected second, the
                // line/curve normalization above still identifies the same
                // tangent pair, but the original ordering alone would try to
                // translate the axis (which owns no movable points) and turn
                // a valid radius edit into a conflict.
                const bool translate_curve = reference_is_line ||
                    is_base_sketch_axis(constraint.second_geometry_id);
                if (translate_curve) {
                    translated = center_curve_translation_points(*this, curve_id);
                    if (!state->first_point_id.empty())
                        reference_points.insert(state->first_point_id);
                    if (!state->second_point_id.empty())
                        reference_points.insert(state->second_point_id);
                    translation_x = correction * state->normal_x;
                    translation_y = correction * state->normal_y;
                } else {
                    translated.insert(state->first_point_id);
                    translated.insert(state->second_point_id);
                    reference_points = center_curve_translation_points(*this, curve_id);
                    translation_x = -correction * state->normal_x;
                    translation_y = -correction * state->normal_y;
                }
                const bool blocked = translated.empty() || std::any_of(
                    translated.begin(), translated.end(), [&](const auto& point_id) {
                        const auto* point = find_point(point_id);
                        return point == nullptr || immutable(*point) ||
                            reference_points.contains(point_id);
                    });
                if (blocked) {
                    immovable_conflict = true;
                } else {
                    for (const auto& point_id : translated) {
                        auto* point = find_point(point_id);
                        point->x += translation_x;
                        point->y += translation_y;
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::EqualRadius) {
                const auto reference_radius = circular_curve_radius(
                    *this, constraint.geometry_id);
                const auto driven_radius = circular_curve_radius(
                    *this, constraint.second_geometry_id);
                if (!reference_radius || !driven_radius) {
                    maximum_residual = std::max(maximum_residual, 1.0);
                    immovable_conflict = true;
                    continue;
                }
                const double residual = *driven_radius - *reference_radius;
                maximum_residual = std::max(
                    maximum_residual, std::abs(residual));
                if (std::abs(residual) > tolerance && !resize_circular_curve(
                        constraint.second_geometry_id, *reference_radius,
                        center_curve_translation_points(
                            *this, constraint.geometry_id))) {
                    immovable_conflict = true;
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::Concentric) {
                const auto reference_center_id = center_curve_point_id(
                    *this, constraint.geometry_id);
                const auto driven_center_id = center_curve_point_id(
                    *this, constraint.second_geometry_id);
                const auto* reference = find_point(*reference_center_id);
                const auto* driven = find_point(*driven_center_id);
                const double dx = reference->x - driven->x;
                const double dy = reference->y - driven->y;
                const double residual = std::hypot(dx, dy);
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance) {
                    const auto translated = center_curve_translation_points(
                        *this, constraint.second_geometry_id);
                    const bool blocked = std::any_of(
                        translated.begin(), translated.end(), [&](const auto& point_id) {
                            const auto* point = find_point(point_id);
                            return point_id == *reference_center_id ||
                                point == nullptr || immutable(*point);
                        });
                    if (blocked) {
                        immovable_conflict = true;
                    } else {
                        for (const auto& point_id : translated) {
                            auto* point = find_point(point_id);
                            point->x += dx;
                            point->y += dy;
                        }
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::MidpointOnLine) {
                const auto segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                if (segment == segments.end()) {
                    immovable_conflict = true;
                    continue;
                }
                auto* first = find_point(segment->first_point_id);
                auto* second = find_point(segment->second_point_id);
                if (first == nullptr || second == nullptr) {
                    immovable_conflict = true;
                    continue;
                }
                const double midpoint_x = (first->x + second->x) * 0.5;
                const double midpoint_y = (first->y + second->y) * 0.5;
                const auto target = point_on_line_target(
                    *this, constraint.second_geometry_id,
                    midpoint_x, midpoint_y);
                if (!target) {
                    immovable_conflict = true;
                    continue;
                }
                const double dx = (*target)[0] - midpoint_x;
                const double dy = (*target)[1] - midpoint_y;
                const double residual = std::hypot(dx, dy);
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance) {
                    const bool first_movable = !immutable(*first);
                    const bool second_movable = !immutable(*second);
                    if (!first_movable && !second_movable) {
                        immovable_conflict = true;
                    } else {
                        const auto correct_coordinate = [&](DimensionKind kind,
                                double correction) {
                            if (std::abs(correction) <= tolerance) return true;
                            const auto first_group = directional_translation_closure(
                                first->id, kind);
                            const auto second_group = directional_translation_closure(
                                second->id, kind);
                            const auto available = [&](const auto& group) {
                                return !group.empty() && !std::any_of(
                                    group.begin(), group.end(), [&](const auto& id) {
                                        const auto* point = find_point(id);
                                        return point == nullptr || immutable(*point);
                                    });
                            };
                            const auto shift = [&](const auto& group, double amount) {
                            for (const auto& id : group) {
                                auto* point = find_point(id);
                                if (kind == DimensionKind::DistanceX)
                                    point->x += amount;
                                else
                                    point->y += amount;
                            }
                            };
                            const bool same_group = std::any_of(
                                first_group.begin(), first_group.end(),
                                [&](const auto& id) {
                                    return second_group.contains(id);
                                });
                            const bool first_available = available(first_group);
                            const bool second_available = available(second_group);
                            if (same_group) {
                                if (!first_available) return false;
                                shift(first_group, correction);
                            } else if (first_available && second_available) {
                                shift(first_group, correction);
                                shift(second_group, correction);
                            } else if (first_available) {
                                shift(first_group, 2.0 * correction);
                            } else if (second_available) {
                                shift(second_group, 2.0 * correction);
                            } else {
                                return false;
                            }
                            return true;
                        };
                        const bool shifted = correct_coordinate(
                                DimensionKind::DistanceX, dx) &&
                            correct_coordinate(DimensionKind::DistanceY, dy);
                        if (!shifted) immovable_conflict = true;
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::Midpoint) {
                const auto segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                auto* point = find_point(constraint.first_point_id);
                auto* first = find_point(segment->first_point_id);
                auto* second = find_point(segment->second_point_id);
                const auto visible = visible_segment_endpoints(segment->id);
                const double target_x = visible
                    ? (visible->first[0] + visible->second[0]) * 0.5
                    : (first->x + second->x) * 0.5;
                const double target_y = visible
                    ? (visible->first[1] + visible->second[1]) * 0.5
                    : (first->y + second->y) * 0.5;
                const double residual_x =
                    point->x - target_x;
                const double residual_y =
                    point->y - target_y;
                const double residual = std::hypot(residual_x, residual_y);
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance) {
                    double denominator{};
                    if (!immutable(*point)) denominator += 1.0;
                    if (!immutable(*first)) denominator += 0.25;
                    if (!immutable(*second)) denominator += 0.25;
                    if (denominator <= 0.0) {
                        immovable_conflict = true;
                    } else {
                        if (!immutable(*point)) {
                            point->x -= residual_x / denominator;
                            point->y -= residual_y / denominator;
                        }
                        if (!immutable(*first)) {
                            first->x += 0.5 * residual_x / denominator;
                            first->y += 0.5 * residual_y / denominator;
                        }
                        if (!immutable(*second)) {
                            second->x += 0.5 * residual_x / denominator;
                            second->y += 0.5 * residual_y / denominator;
                        }
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::Symmetric) {
                const auto axis = sketch_axis_line(*this, constraint.geometry_id);
                auto* source = find_point(constraint.first_point_id);
                auto* mirrored = find_point(constraint.second_point_id);
                if (!axis || source == nullptr || mirrored == nullptr) {
                    immovable_conflict = true;
                    continue;
                }
                const auto desired = reflected_position(
                    {source->x, source->y}, axis->first, axis->second);
                const double dx = desired[0] - mirrored->x;
                const double dy = desired[1] - mirrored->y;
                const double residual = std::hypot(dx, dy);
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance) {
                    if (immutable(*mirrored)) {
                        immovable_conflict = true;
                    } else {
                        mirrored->x = desired[0];
                        mirrored->y = desired[1];
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::PointOnCircle) {
                auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                auto* point = find_point(constraint.first_point_id);
                const auto target = circular_constraint_target(
                    *this, constraint.geometry_id, point->x, point->y);
                if (!target) {
                    immovable_conflict = true;
                    continue;
                }
                maximum_residual = std::max(
                    maximum_residual, target->residual);
                if (target->residual > tolerance) {
                    if (immutable(*point)) {
                        // A full circle may still adapt its free radius to an
                        // immutable attached point. An arc has persisted end
                        // points and an angular domain, so changing only its
                        // scalar radius would invalidate that geometry.
                        if (circle == circles.end()) {
                            immovable_conflict = true;
                        } else {
                            const auto* center = find_point(
                                circle->center_point_id);
                            circle->radius = std::hypot(
                                point->x - center->x, point->y - center->y);
                        }
                    } else {
                        point->x = target->position[0];
                        point->y = target->position[1];
                    }
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::PointOnLine) {
                auto* point = find_point(constraint.first_point_id);
                const auto target = point_on_line_target(
                    *this, constraint.geometry_id, point->x, point->y);
                if (!target) {
                    immovable_conflict = true;
                    continue;
                }
                const double residual = std::hypot(
                    point->x - (*target)[0], point->y - (*target)[1]);
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance) {
                    if (immutable(*point)) immovable_conflict = true;
                    else { point->x = (*target)[0]; point->y = (*target)[1]; }
                }
                continue;
            }
            if (is_segment_pair_constraint(constraint.kind)) {
                const auto reference_line = segment_or_external_line(
                    *this, constraint.geometry_id);
                const auto second_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                auto* driven_first = find_point(second_segment->first_point_id);
                auto* driven_second = find_point(second_segment->second_point_id);
                const double rx = reference_line->second[0];
                const double ry = reference_line->second[1];
                const double reference_length = std::hypot(rx, ry);
                const double dx = driven_second->x - driven_first->x;
                const double dy = driven_second->y - driven_first->y;
                const double driven_length = std::hypot(dx, dy);
                if (reference_length <= tolerance || driven_length <= tolerance) {
                    immovable_conflict = true;
                    continue;
                }
                double desired_x{};
                double desired_y{};
                double residual{};
                if (constraint.kind == ConstraintKind::EqualLength) {
                    desired_x = dx * reference_length / driven_length;
                    desired_y = dy * reference_length / driven_length;
                    residual = std::abs(driven_length - reference_length);
                } else {
                    double ux = rx / reference_length;
                    double uy = ry / reference_length;
                    if (constraint.kind == ConstraintKind::Perpendicular) {
                        const double rotated_x = -uy;
                        uy = ux;
                        ux = rotated_x;
                    }
                    if (ux * dx + uy * dy < 0.0) { ux = -ux; uy = -uy; }
                    desired_x = ux * driven_length;
                    desired_y = uy * driven_length;
                    residual = constraint.kind == ConstraintKind::Parallel
                        ? std::abs((rx * dy - ry * dx) /
                            (reference_length * driven_length))
                        : std::abs((rx * dx + ry * dy) /
                            (reference_length * driven_length));
                }
                const double correction_x = desired_x - dx;
                const double correction_y = desired_y - dy;
                maximum_residual = std::max(maximum_residual, residual);
                if (residual > tolerance && !move_pair(
                        *driven_first, *driven_second, correction_x, correction_y)) {
                    immovable_conflict = true;
                }
                continue;
            }
            const auto first = point_position(constraint.first_point_id);
            const auto second = point_position(constraint.second_point_id);
            double dx{};
            double dy{};
            if (constraint.kind == ConstraintKind::Horizontal) dy = (*first)[1] - (*second)[1];
            else if (constraint.kind == ConstraintKind::Vertical) dx = (*first)[0] - (*second)[0];
            else { dx = (*first)[0] - (*second)[0]; dy = (*first)[1] - (*second)[1]; }
            const double residual = std::hypot(dx, dy);
            maximum_residual = std::max(maximum_residual, residual);
            if (residual > tolerance) {
                auto* reference = find_point(constraint.first_point_id);
                auto* driven = find_point(constraint.second_point_id);
                if (driven != nullptr && !immutable(*driven)) {
                    driven->x += dx;
                    driven->y += dy;
                } else if (reference != nullptr && !immutable(*reference)) {
                    reference->x -= dx;
                    reference->y -= dy;
                } else {
                    immovable_conflict = true;
                }
            }
        }
        if (!synchronize_curves()) immovable_conflict = true;
        for (const auto& dimension : dimensions) {
            if (dimension.suppressed || !dimension.driving) continue;
            if (dimension.kind == DimensionKind::EllipseRotation) {
                auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                const double desired = dimension.value *
                    3.14159265358979323846 / 180.0;
                maximum_residual = std::max(maximum_residual,
                    std::abs(wrapped_degrees(
                        ellipse->rotation * 180.0 / 3.14159265358979323846 -
                        dimension.value)));
                ellipse->rotation = desired;
                const auto* center = find_point(ellipse->center_point_id);
                auto* major = find_point(ellipse->major_point_id);
                auto* minor = find_point(ellipse->minor_point_id);
                const double orientation = ellipse->reversed ? -1.0 : 1.0;
                major->x = center->x + ellipse->major_radius * std::cos(desired);
                major->y = center->y + ellipse->major_radius * std::sin(desired);
                minor->x = center->x - orientation * ellipse->minor_radius *
                    std::sin(desired);
                minor->y = center->y + orientation * ellipse->minor_radius *
                    std::cos(desired);
                continue;
            }
            if (dimension.kind == DimensionKind::EllipseMajorRadius ||
                dimension.kind == DimensionKind::EllipseMinorRadius) {
                auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                double& radius = dimension.kind == DimensionKind::EllipseMajorRadius
                    ? ellipse->major_radius : ellipse->minor_radius;
                maximum_residual = std::max(
                    maximum_residual, std::abs(radius - dimension.value));
                radius = dimension.value;
                const auto* center = find_point(ellipse->center_point_id);
                auto* axis_point = find_point(
                    dimension.kind == DimensionKind::EllipseMajorRadius
                        ? ellipse->major_point_id : ellipse->minor_point_id);
                const double angle = dimension.kind == DimensionKind::EllipseMajorRadius
                    ? ellipse->rotation
                    : ellipse->rotation + (ellipse->reversed ? -1.0 : 1.0) *
                        3.14159265358979323846 / 2.0;
                axis_point->x = center->x + radius * std::cos(angle);
                axis_point->y = center->y + radius * std::sin(angle);
                continue;
            }
            if (dimension.kind == DimensionKind::Radius ||
                dimension.kind == DimensionKind::Diameter) {
                auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                double* radius{};
                if (circle != circles.end()) {
                    radius = &circle->radius;
                } else {
                    auto arc = std::find_if(arcs.begin(), arcs.end(),
                        [&](const auto& value) { return value.id == dimension.geometry_id; });
                    if (arc != arcs.end()) {
                        radius = &arc->radius;
                    } else {
                        auto corner = std::find_if(
                            corner_radii.begin(), corner_radii.end(),
                            [&](const auto& value) {
                                return value.id == dimension.geometry_id;
                            });
                        if (corner == corner_radii.end()) {
                            maximum_residual = std::max(maximum_residual, 1.0);
                            immovable_conflict = true;
                            continue;
                        }
                        radius = &corner->radius;
                    }
                }
                const double measured = dimension.kind == DimensionKind::Diameter
                    ? *radius * 2.0 : *radius;
                const double residual = measured - dimension.value;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                *radius = dimension.kind == DimensionKind::Diameter
                    ? dimension.value * 0.5 : dimension.value;
                continue;
            }
            if (dimension.kind == DimensionKind::DistancePointLine ||
                dimension.kind == DimensionKind::DistanceSymmetric) {
                const auto reference = sketch_axis_line(*this, dimension.geometry_id)
                    ? sketch_axis_line(*this, dimension.geometry_id)
                    : segment_or_external_line(*this, dimension.geometry_id);
                const double length = std::hypot(
                    reference->second[0], reference->second[1]);
                const double target = dimension.kind ==
                        DimensionKind::DistanceSymmetric
                    ? dimension.value * 0.5 : dimension.value;
                std::vector<std::string> target_points{
                    dimension.first_point_id};
                if (dimension.kind == DimensionKind::DistanceSymmetric &&
                    !dimension.second_point_id.empty()) {
                    target_points.push_back(dimension.second_point_id);
                }
                for (const auto& target_point_id : target_points) {
                    auto* point = find_point(target_point_id);
                    const double signed_distance =
                        (reference->second[0] *
                             (point->y - reference->first[1]) -
                         reference->second[1] *
                             (point->x - reference->first[0])) / length;
                    const double side = signed_distance < 0.0 ? -1.0 : 1.0;
                    const double correction = side * target - signed_distance;
                    maximum_residual = std::max(
                        maximum_residual, std::abs(correction));
                    if (std::abs(correction) <= tolerance) continue;
                    const auto translated = point_translation_closure(
                        *this, target_point_id);
                    const auto reference_segment = std::find_if(
                        segments.begin(), segments.end(), [&](const auto& value) {
                            return value.id == dimension.geometry_id;
                        });
                    const bool owns_reference_point =
                        reference_segment != segments.end() &&
                        (translated.contains(reference_segment->first_point_id) ||
                         translated.contains(reference_segment->second_point_id));
                    const bool blocked = translated.empty() || owns_reference_point ||
                        std::any_of(translated.begin(), translated.end(),
                            [&](const auto& point_id) {
                                const auto* value = find_point(point_id);
                                return value == nullptr || immutable(*value);
                            });
                    if (blocked) {
                        immovable_conflict = true;
                    } else {
                        const double shift_x =
                            -reference->second[1] / length * correction;
                        const double shift_y =
                            reference->second[0] / length * correction;
                        for (const auto& point_id : translated) {
                            auto* value = find_point(point_id);
                            value->x += shift_x;
                            value->y += shift_y;
                        }
                    }
                }
                continue;
            }
            if (dimension.kind == DimensionKind::DistanceLine ||
                dimension.kind == DimensionKind::AngleBetween) {
                const auto reference = sketch_axis_line(*this, dimension.geometry_id)
                    ? sketch_axis_line(*this, dimension.geometry_id)
                    : segment_or_external_line(*this, dimension.geometry_id);
                const auto driven_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == dimension.second_geometry_id;
                    });
                const bool point_line_angle =
                    dimension.kind == DimensionKind::AngleBetween &&
                    dimension.second_geometry_id.empty();
                const bool four_point_angle =
                    dimension.kind == DimensionKind::AngleBetween &&
                    find_point(dimension.geometry_id) != nullptr &&
                    find_point(dimension.second_geometry_id) != nullptr;
                auto* driven_first = find_point(four_point_angle
                    ? dimension.geometry_id
                    : point_line_angle ? dimension.first_point_id
                    : driven_segment->first_point_id);
                auto* driven_second = find_point(four_point_angle
                    ? dimension.second_geometry_id
                    : point_line_angle ? dimension.second_point_id
                    : driven_segment->second_point_id);
                const auto angle_lines = dimension.kind == DimensionKind::AngleBetween
                    ? angle_dimension_lines(*this, dimension) : std::nullopt;
                const double rx = angle_lines
                    ? angle_lines->first.second[0] : reference->second[0];
                const double ry = angle_lines
                    ? angle_lines->first.second[1] : reference->second[1];
                const double reference_length = std::hypot(rx, ry);
                const double dx = driven_second->x - driven_first->x;
                const double dy = driven_second->y - driven_first->y;
                const double driven_length = std::hypot(dx, dy);
                if (dimension.kind == DimensionKind::DistanceLine) {
                    const double signed_distance =
                        (rx * (driven_first->y - reference->first[1]) -
                         ry * (driven_first->x - reference->first[0])) /
                        reference_length;
                    const double sign = signed_distance < 0.0 ? -1.0 : 1.0;
                    const double residual = std::abs(signed_distance) - dimension.value;
                    maximum_residual = std::max(maximum_residual, std::abs(residual));
                    if (std::abs(residual) > tolerance) {
                        if (immutable(*driven_first) || immutable(*driven_second)) {
                            immovable_conflict = true;
                        } else {
                            const double shift = -residual * sign;
                            driven_first->x += -ry / reference_length * shift;
                            driven_first->y += rx / reference_length * shift;
                            driven_second->x += -ry / reference_length * shift;
                            driven_second->y += rx / reference_length * shift;
                        }
                    }
                } else {
                    const double cosine = std::clamp(
                        (rx * dx + ry * dy) /
                            (reference_length * driven_length), -1.0, 1.0);
                    const double measured = std::acos(cosine) *
                        180.0 / 3.14159265358979323846;
                    const double displayed_magnitude = std::abs(dimension.value);
                    const double supplementary_target =
                        180.0 - displayed_magnitude;
                    // The displayed sector can reverse either line ray at
                    // their intersection.  Therefore angle_sector alone does
                    // not say whether the stored line directions constrain
                    // alpha or 180-alpha.  Keep the branch represented by the
                    // current geometry; this is stable while editing another
                    // dimension and follows a direct edit to the nearby
                    // angular value.
                    const double solver_target = dimension.angle_sector == 1 &&
                            std::abs(measured - supplementary_target) <
                                std::abs(measured - displayed_magnitude)
                        ? supplementary_target : displayed_magnitude;
                    const double residual = measured - solver_target;
                    maximum_residual = std::max(maximum_residual, std::abs(residual));
                    if (std::abs(residual) > tolerance) {
                        const double reference_angle = std::atan2(ry, rx);
                        const double orientation = rx * dy - ry * dx < 0.0 ? -1.0 : 1.0;
                        const double target = reference_angle + orientation *
                            solver_target * 3.14159265358979323846 / 180.0;
                        const double target_dx = driven_length * std::cos(target);
                        const double target_dy = driven_length * std::sin(target);
                        bool moved{};
                        const auto reference_segment = std::find_if(
                            segments.begin(), segments.end(), [&](const auto& value) {
                                return value.id == dimension.geometry_id;
                            });
                        SketchPoint* shared{};
                        SketchPoint* free_endpoint{};
                        double direction_sign{1.0};
                        if (reference_segment != segments.end()) {
                            if (reference_segment->first_point_id == driven_first->id ||
                                reference_segment->second_point_id == driven_first->id) {
                                shared = driven_first;
                                free_endpoint = driven_second;
                            } else if (reference_segment->first_point_id ==
                                    driven_second->id ||
                                reference_segment->second_point_id ==
                                    driven_second->id) {
                                shared = driven_second;
                                free_endpoint = driven_first;
                                direction_sign = -1.0;
                            }
                        }
                        if (shared != nullptr && free_endpoint != nullptr &&
                            !immutable(*free_endpoint)) {
                            const double ray_x = direction_sign * std::cos(target);
                            const double ray_y = direction_sign * std::sin(target);
                            std::optional<std::array<double, 2>> intersection;
                            for (const auto& constraint : constraints) {
                                if (constraint.suppressed ||
                                    constraint.kind != ConstraintKind::PointOnLine ||
                                    constraint.first_point_id != free_endpoint->id) {
                                    continue;
                                }
                                const auto line = sketch_axis_line(
                                    *this, constraint.geometry_id)
                                    ? sketch_axis_line(*this, constraint.geometry_id)
                                    : segment_or_external_line(
                                        *this, constraint.geometry_id);
                                if (!line) continue;
                                const double denominator =
                                    ray_x * line->second[1] -
                                    ray_y * line->second[0];
                                if (std::abs(denominator) <= 1.0e-12) continue;
                                const double offset_x = line->first[0] - shared->x;
                                const double offset_y = line->first[1] - shared->y;
                                const double parameter =
                                    (offset_x * line->second[1] -
                                     offset_y * line->second[0]) / denominator;
                                intersection = std::array{
                                    shared->x + parameter * ray_x,
                                    shared->y + parameter * ray_y};
                                break;
                            }
                            if (intersection) {
                                free_endpoint->x = (*intersection)[0];
                                free_endpoint->y = (*intersection)[1];
                            } else {
                                free_endpoint->x = shared->x +
                                    direction_sign * target_dx;
                                free_endpoint->y = shared->y +
                                    direction_sign * target_dy;
                            }
                            moved = true;
                        } else {
                            moved = move_pair(*driven_first, *driven_second,
                                target_dx - dx, target_dy - dy);
                        }
                        if (!moved) {
                            immovable_conflict = true;
                        }
                    }
                }
                continue;
            }
            if (dimension.kind == DimensionKind::AngleThreePoint) {
                const auto first = point_position(dimension.first_point_id);
                const auto vertex = point_position(dimension.second_point_id);
                const auto second = point_position(dimension.geometry_id);
                const double ax = (*first)[0] - (*vertex)[0];
                const double ay = (*first)[1] - (*vertex)[1];
                const double bx = (*second)[0] - (*vertex)[0];
                const double by = (*second)[1] - (*vertex)[1];
                const double first_length = std::hypot(ax, ay);
                const double second_length = std::hypot(bx, by);
                const double measured = std::acos(std::clamp(
                    (ax * bx + ay * by) / (first_length * second_length),
                    -1.0, 1.0)) * 180.0 / 3.14159265358979323846;
                const double target_angle = geometric_angle_degrees(dimension.value);
                const double residual = measured - target_angle;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                if (std::abs(residual) > tolerance) {
                    const double orientation = ax * by - ay * bx < 0.0 ? -1.0 : 1.0;
                    const double target = std::atan2(ay, ax) + orientation *
                        target_angle * 3.14159265358979323846 / 180.0;
                    const double target_x = (*vertex)[0] + second_length * std::cos(target);
                    const double target_y = (*vertex)[1] + second_length * std::sin(target);
                    const auto translated = point_translation_closure(
                        *this, dimension.geometry_id);
                    const bool blocked = translated.empty() ||
                        translated.contains(dimension.first_point_id) ||
                        translated.contains(dimension.second_point_id) ||
                        std::any_of(translated.begin(), translated.end(),
                            [&](const auto& point_id) {
                                const auto* point = find_point(point_id);
                                return point == nullptr || immutable(*point);
                            });
                    if (blocked) {
                        immovable_conflict = true;
                    } else {
                        const double correction_x = target_x - (*second)[0];
                        const double correction_y = target_y - (*second)[1];
                        for (const auto& point_id : translated) {
                            auto* point = find_point(point_id);
                            point->x += correction_x;
                            point->y += correction_y;
                        }
                    }
                }
                continue;
            }
            const bool coordinate_axis_dimension =
                has_coordinate_axis_reference(dimension);
            if (coordinate_axis_dimension) {
                auto* point = find_point(dimension.first_point_id);
                const double measured = dimension.kind == DimensionKind::DistanceX
                    ? point->x : point->y;
                const double residual = measured - dimension.value;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                if (std::abs(residual) > tolerance) {
                    if (immutable(*point)) {
                        immovable_conflict = true;
                    } else if (dimension.kind == DimensionKind::DistanceX) {
                        point->x -= residual;
                    } else {
                        point->y -= residual;
                    }
                }
                continue;
            }
            const auto first = point_position(dimension.first_point_id);
            const auto second = point_position(dimension.second_point_id);
            double dx = (*second)[0] - (*first)[0];
            double dy = (*second)[1] - (*first)[1];
            double correction_x{};
            double correction_y{};
            double residual{};
            if (dimension.kind == DimensionKind::DistanceX) {
                residual = dx - dimension.value;
                correction_x = -residual;
            } else if (dimension.kind == DimensionKind::DistanceY) {
                residual = dy - dimension.value;
                correction_y = -residual;
            } else if (dimension.kind == DimensionKind::Angle) {
                const double distance = std::hypot(dx, dy);
                const double radians = dimension.value *
                    3.14159265358979323846 / 180.0;
                residual = wrapped_degrees(
                    std::atan2(dy, dx) * 180.0 / 3.14159265358979323846 -
                    dimension.value);
                correction_x = distance * std::cos(radians) - dx;
                correction_y = distance * std::sin(radians) - dy;
            } else {
                const double distance = std::hypot(dx, dy);
                residual = distance - dimension.value;
                if (distance <= tolerance) {
                    correction_x = dimension.value;
                } else {
                    correction_x = -residual * dx / distance;
                    correction_y = -residual * dy / distance;
                }
            }
            maximum_residual = std::max(maximum_residual, std::abs(residual));
            if (std::abs(residual) > tolerance) {
                const bool axis_aligned_distance_x =
                    dimension.kind == DimensionKind::Distance &&
                    std::abs(dy) <= tolerance && std::abs(dx) > tolerance;
                const bool axis_aligned_distance_y =
                    dimension.kind == DimensionKind::Distance &&
                    std::abs(dx) <= tolerance && std::abs(dy) > tolerance;
                const bool moved = dimension.kind == DimensionKind::DistanceX ||
                        axis_aligned_distance_x
                    ? move_directional_pair(dimension.first_point_id,
                        dimension.second_point_id, DimensionKind::DistanceX,
                        correction_x)
                    : dimension.kind == DimensionKind::DistanceY ||
                            axis_aligned_distance_y
                    ? move_directional_pair(dimension.first_point_id,
                        dimension.second_point_id, DimensionKind::DistanceY,
                        correction_y)
                    : move_point_pair(dimension.first_point_id,
                        dimension.second_point_id, correction_x, correction_y);
                if (!moved) immovable_conflict = true;
            }
        }
        if (immovable_conflict) {
            points = original_points;
            circles = original_circles;
            arcs = original_arcs;
            corner_radii = original_corner_radii;
            ellipses = original_ellipses;
            elliptical_arcs = original_elliptical_arcs;
            return {SolveStatus::Conflicting, 0, maximum_residual};
        }
        if (maximum_residual <= tolerance) break;
    }
    if (maximum_residual > tolerance) {
        points = original_points;
        circles = original_circles;
        arcs = original_arcs;
        corner_radii = original_corner_radii;
        ellipses = original_ellipses;
        elliptical_arcs = original_elliptical_arcs;
        return {SolveStatus::Conflicting, 0, maximum_residual};
    }
    for (const auto& segment : segments) {
        const auto* first = find_point(segment.first_point_id);
        const auto* second = find_point(segment.second_point_id);
        if (std::hypot(second->x - first->x, second->y - first->y) <= tolerance) {
            points = original_points;
            circles = original_circles;
            arcs = original_arcs;
            corner_radii = original_corner_radii;
            ellipses = original_ellipses;
            elliptical_arcs = original_elliptical_arcs;
            return {SolveStatus::Conflicting, 0, 0.0};
        }
    }
    if (!calculate_degrees_of_freedom) {
        if (!refresh_reference_dimensions(*this)) {
            points = original_points;
            circles = original_circles;
            arcs = original_arcs;
            corner_radii = original_corner_radii;
            ellipses = original_ellipses;
            elliptical_arcs = original_elliptical_arcs;
            dimensions = original_dimensions;
            return {SolveStatus::Conflicting, 0, maximum_residual};
        }
        // This mode is private and its callers only accept/reject the
        // transaction. Do not pretend that an uncomputed DOF count is zero.
        return {SolveStatus::UnderConstrained, 0, maximum_residual};
    }
    // Rank is considerably more expensive than equation convergence. Reuse
    // it only for a byte-identical, already solved ZIMA state. This also makes
    // the baseline half of a constraint-insertion transaction effectively
    // free after the preceding committed solve.
    if (!refresh_reference_dimensions(*this)) {
        points = original_points;
        circles = original_circles;
        arcs = original_arcs;
        corner_radii = original_corner_radii;
        ellipses = original_ellipses;
        elliptical_arcs = original_elliptical_arcs;
        dimensions = original_dimensions;
        return {SolveStatus::Conflicting, 0, maximum_residual};
    }
    const auto rank_cache_key = serialized();
    if (solved_rank_cache_result_ && rank_cache_key == solved_rank_cache_key_) {
        auto cached = *solved_rank_cache_result_;
        cached.maximum_residual = maximum_residual;
        return cached;
    }
    const auto residuals = [&]() {
        std::vector<double> result;
        for (const auto& constraint : constraints) {
            if (constraint.suppressed) continue;
            if (constraint.kind == ConstraintKind::Tangent) {
                const bool reference_is_segment = std::any_of(
                    segments.begin(), segments.end(), [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                const bool driven_is_segment = std::any_of(
                    segments.begin(), segments.end(), [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                const bool reference_is_line = reference_is_segment ||
                    is_base_sketch_axis(constraint.geometry_id);
                const bool driven_is_line = driven_is_segment ||
                    is_base_sketch_axis(constraint.second_geometry_id);
                if (!reference_is_line && !driven_is_line) {
                    const auto reference_curve = tangent_curve_data(
                        *this, constraint.geometry_id);
                    const auto driven_curve = tangent_curve_data(
                        *this, constraint.second_geometry_id);
                    if (reference_curve && driven_curve &&
                        reference_curve->circular_radius &&
                        driven_curve->circular_radius) {
                        const auto state = curve_pair_tangent_state(
                            *this, constraint.geometry_id,
                            constraint.second_geometry_id,
                            constraint.tangent_internal);
                        result.push_back(state
                            ? state->center_distance - state->target_distance
                            : 1.0e12);
                    } else {
                        const auto state = general_curve_pair_tangent_state(
                            *this, constraint.geometry_id,
                            constraint.second_geometry_id);
                        result.push_back(state ? state->distance : 1.0e12);
                    }
                } else {
                    const auto& curve_id = reference_is_line
                        ? constraint.second_geometry_id : constraint.geometry_id;
                    const auto& segment_id = reference_is_line
                        ? constraint.geometry_id : constraint.second_geometry_id;
                    if (const auto endpoint_residual =
                            segment_curve_endpoint_tangent_residual(
                                *this, segment_id, curve_id)) {
                        result.push_back(*endpoint_residual);
                        continue;
                    }
                    if (std::ranges::any_of(bsplines,
                            [&](const auto& value) { return value.id == curve_id; })) {
                        const auto state = segment_spline_tangent_state(
                            *this,
                            segment_id,
                            curve_id);
                        result.push_back(state ? state->residual : 1.0e12);
                        continue;
                    }
                    const auto state = segment_curve_tangent_state(
                        *this,
                        reference_is_line
                            ? constraint.geometry_id : constraint.second_geometry_id,
                        reference_is_line
                            ? constraint.second_geometry_id : constraint.geometry_id);
                    result.push_back(state
                        ? std::abs(state->signed_distance) -
                            std::abs(state->target_distance)
                        : 1.0e12);
                }
                continue;
            }
            if (constraint.kind == ConstraintKind::EqualRadius) {
                const auto reference_radius = circular_curve_radius(
                    *this, constraint.geometry_id);
                const auto driven_radius = circular_curve_radius(
                    *this, constraint.second_geometry_id);
                result.push_back(reference_radius && driven_radius
                    ? *driven_radius - *reference_radius
                    : 1.0e12);
                continue;
            }
            if (constraint.kind == ConstraintKind::Concentric) {
                const auto reference_center_id = center_curve_point_id(
                    *this, constraint.geometry_id);
                const auto driven_center_id = center_curve_point_id(
                    *this, constraint.second_geometry_id);
                const auto* reference = find_point(*reference_center_id);
                const auto* driven = find_point(*driven_center_id);
                result.push_back(reference->x - driven->x);
                result.push_back(reference->y - driven->y);
                continue;
            }
            if (constraint.kind == ConstraintKind::MidpointOnLine) {
                const auto segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                if (segment == segments.end()) {
                    result.push_back(std::numeric_limits<double>::infinity());
                    continue;
                }
                const auto* first = find_point(segment->first_point_id);
                const auto* second = find_point(segment->second_point_id);
                if (first == nullptr || second == nullptr) {
                    result.push_back(std::numeric_limits<double>::infinity());
                    continue;
                }
                const double midpoint_x = (first->x + second->x) * 0.5;
                const double midpoint_y = (first->y + second->y) * 0.5;
                const auto target = point_on_line_target(
                    *this, constraint.second_geometry_id,
                    midpoint_x, midpoint_y);
                result.push_back(target ? std::hypot(
                    midpoint_x - (*target)[0], midpoint_y - (*target)[1])
                    : std::numeric_limits<double>::infinity());
                continue;
            }
            if (constraint.kind == ConstraintKind::Midpoint) {
                const auto segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                const auto* point = find_point(constraint.first_point_id);
                const auto* first = find_point(segment->first_point_id);
                const auto* second = find_point(segment->second_point_id);
                const auto visible = visible_segment_endpoints(segment->id);
                const double target_x = visible
                    ? (visible->first[0] + visible->second[0]) * 0.5
                    : (first->x + second->x) * 0.5;
                const double target_y = visible
                    ? (visible->first[1] + visible->second[1]) * 0.5
                    : (first->y + second->y) * 0.5;
                result.push_back(point->x - target_x);
                result.push_back(point->y - target_y);
                continue;
            }
            if (constraint.kind == ConstraintKind::Symmetric) {
                const auto axis = sketch_axis_line(*this, constraint.geometry_id);
                const auto* source = find_point(constraint.first_point_id);
                const auto* mirrored = find_point(constraint.second_point_id);
                const auto desired = reflected_position(
                    {source->x, source->y}, axis->first, axis->second);
                result.push_back(mirrored->x - desired[0]);
                result.push_back(mirrored->y - desired[1]);
                continue;
            }
            if (constraint.kind == ConstraintKind::PointOnCircle) {
                const auto* point = find_point(constraint.first_point_id);
                const auto target = circular_constraint_target(
                    *this, constraint.geometry_id, point->x, point->y);
                result.push_back(target ? target->residual
                                        : std::numeric_limits<double>::infinity());
                continue;
            }
            if (constraint.kind == ConstraintKind::PointOnLine) {
                const auto* point = find_point(constraint.first_point_id);
                const auto target = point_on_line_target(
                    *this, constraint.geometry_id, point->x, point->y);
                result.push_back(target ? std::hypot(
                    point->x - (*target)[0], point->y - (*target)[1])
                    : std::numeric_limits<double>::infinity());
                continue;
            }
            if (is_segment_pair_constraint(constraint.kind)) {
                const auto reference_line = segment_or_external_line(
                    *this, constraint.geometry_id);
                const auto second_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                const auto* c = find_point(second_segment->first_point_id);
                const auto* d = find_point(second_segment->second_point_id);
                const double rx = reference_line->second[0];
                const double ry = reference_line->second[1];
                const double dx = d->x - c->x;
                const double dy = d->y - c->y;
                const double scale = std::hypot(rx, ry) * std::hypot(dx, dy);
                result.push_back(constraint.kind == ConstraintKind::Parallel
                    ? (rx * dy - ry * dx) / scale
                    : constraint.kind == ConstraintKind::Perpendicular
                        ? (rx * dx + ry * dy) / scale
                        : std::hypot(dx, dy) - std::hypot(rx, ry));
                continue;
            }
            const auto first = point_position(constraint.first_point_id);
            const auto second = point_position(constraint.second_point_id);
            if (constraint.kind == ConstraintKind::Horizontal) {
                result.push_back((*first)[1] - (*second)[1]);
            } else if (constraint.kind == ConstraintKind::Vertical) {
                result.push_back((*first)[0] - (*second)[0]);
            } else {
                result.push_back((*first)[0] - (*second)[0]);
                result.push_back((*first)[1] - (*second)[1]);
            }
        }
        for (const auto& dimension : dimensions) {
            if (dimension.suppressed || !dimension.driving) continue;
            if (dimension.kind == DimensionKind::EllipseRotation) {
                const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                result.push_back(wrapped_degrees(
                    ellipse->rotation * 180.0 / 3.14159265358979323846 -
                    dimension.value));
                continue;
            }
            if (dimension.kind == DimensionKind::EllipseMajorRadius ||
                dimension.kind == DimensionKind::EllipseMinorRadius) {
                const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                result.push_back((dimension.kind == DimensionKind::EllipseMajorRadius
                    ? ellipse->major_radius : ellipse->minor_radius) - dimension.value);
                continue;
            }
            if (dimension.kind == DimensionKind::Radius ||
                dimension.kind == DimensionKind::Diameter) {
                const auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                if (circle != circles.end()) {
                    result.push_back((dimension.kind == DimensionKind::Diameter
                        ? circle->radius * 2.0 : circle->radius) - dimension.value);
                } else {
                    const auto arc = std::find_if(arcs.begin(), arcs.end(),
                        [&](const auto& value) { return value.id == dimension.geometry_id; });
                    if (arc != arcs.end()) {
                        result.push_back((dimension.kind == DimensionKind::Diameter
                            ? arc->radius * 2.0 : arc->radius) - dimension.value);
                    } else {
                        const auto corner = std::find_if(
                            corner_radii.begin(), corner_radii.end(),
                            [&](const auto& value) {
                                return value.id == dimension.geometry_id;
                            });
                        result.push_back(corner == corner_radii.end()
                            ? std::numeric_limits<double>::infinity()
                            : (dimension.kind == DimensionKind::Diameter
                                ? corner->radius * 2.0 : corner->radius) -
                                  dimension.value);
                    }
                }
                continue;
            }
            if (dimension.kind == DimensionKind::DistancePointLine ||
                dimension.kind == DimensionKind::DistanceSymmetric) {
                const auto reference = sketch_axis_line(*this, dimension.geometry_id)
                    ? sketch_axis_line(*this, dimension.geometry_id)
                    : segment_or_external_line(*this, dimension.geometry_id);
                const double length = std::hypot(
                    reference->second[0], reference->second[1]);
                const double target = dimension.kind ==
                        DimensionKind::DistanceSymmetric
                    ? dimension.value * 0.5 : dimension.value;
                for (const auto& point_id : std::array{
                        dimension.first_point_id, dimension.second_point_id}) {
                    if (point_id.empty()) continue;
                    const auto* point = find_point(point_id);
                    const double signed_distance =
                        (reference->second[0] *
                             (point->y - reference->first[1]) -
                         reference->second[1] *
                             (point->x - reference->first[0])) / length;
                    result.push_back(std::abs(signed_distance) - target);
                    if (dimension.kind != DimensionKind::DistanceSymmetric) break;
                }
                continue;
            }
            if (dimension.kind == DimensionKind::DistanceLine ||
                dimension.kind == DimensionKind::AngleBetween) {
                if (dimension.kind == DimensionKind::AngleBetween) {
                    const auto lines = angle_dimension_lines(*this, dimension);
                    if (!lines) {
                        result.push_back(1.0e12);
                        continue;
                    }
                    const auto& rv = lines->first.second;
                    const auto& dv = lines->second.second;
                    const double scale = std::hypot(rv[0], rv[1]) *
                        std::hypot(dv[0], dv[1]);
                    const double cross =
                        (rv[0] * dv[1] - rv[1] * dv[0]) / scale;
                    const double dot =
                        (rv[0] * dv[0] + rv[1] * dv[1]) / scale;
                    const double orientation =
                        dimension.value < 0.0 ? -1.0 : 1.0;
                    const double measured = std::acos(std::clamp(dot, -1.0, 1.0)) *
                        180.0 / 3.14159265358979323846;
                    const double displayed_magnitude = std::abs(dimension.value);
                    const double supplementary_target =
                        180.0 - displayed_magnitude;
                    const double solver_target = dimension.angle_sector == 1 &&
                            std::abs(measured - supplementary_target) <
                                std::abs(measured - displayed_magnitude)
                        ? supplementary_target : displayed_magnitude;
                    // acos has a singular numerical derivative at 0 and 180
                    // degrees.  That made a horizontal constraint and a 0°
                    // angular driver look independent to the rank test.  A
                    // locally signed atan2 residual describes the same single
                    // rotational equation and remains differentiable at 0°.
                    result.push_back(orientation * std::atan2(cross, dot) *
                        180.0 / 3.14159265358979323846 - solver_target);
                    continue;
                }
                const auto reference = sketch_axis_line(*this, dimension.geometry_id)
                    ? sketch_axis_line(*this, dimension.geometry_id)
                    : segment_or_external_line(*this, dimension.geometry_id);
                auto driven = segment_or_external_line(
                    *this, dimension.second_geometry_id);
                if (dimension.kind == DimensionKind::AngleBetween &&
                    dimension.second_geometry_id.empty()) {
                    const auto first = point_position(dimension.first_point_id);
                    const auto second = point_position(dimension.second_point_id);
                    if (first && second) {
                        driven = std::pair{*first,
                            std::array{(*second)[0] - (*first)[0],
                                       (*second)[1] - (*first)[1]}};
                    }
                }
                const double rx = reference->second[0];
                const double ry = reference->second[1];
                const double dx = driven->second[0];
                const double dy = driven->second[1];
                const double reference_length = std::hypot(rx, ry);
                if (dimension.kind == DimensionKind::DistanceLine) {
                    const double signed_distance =
                        (rx * (driven->first[1] - reference->first[1]) -
                         ry * (driven->first[0] - reference->first[0])) /
                        reference_length;
                    result.push_back(std::abs(signed_distance) - dimension.value);
                } else {
                    const double cosine = std::clamp(
                        (rx * dx + ry * dy) /
                            (reference_length * std::hypot(dx, dy)), -1.0, 1.0);
                    result.push_back(std::acos(cosine) *
                        180.0 / 3.14159265358979323846 - dimension.value);
                }
                continue;
            }
            if (dimension.kind == DimensionKind::AngleThreePoint) {
                const auto first = point_position(dimension.first_point_id);
                const auto vertex = point_position(dimension.second_point_id);
                const auto second = point_position(dimension.geometry_id);
                const double ax = (*first)[0] - (*vertex)[0];
                const double ay = (*first)[1] - (*vertex)[1];
                const double bx = (*second)[0] - (*vertex)[0];
                const double by = (*second)[1] - (*vertex)[1];
                const double measured = std::acos(std::clamp(
                    (ax * bx + ay * by) /
                        (std::hypot(ax, ay) * std::hypot(bx, by)), -1.0, 1.0)) *
                    180.0 / 3.14159265358979323846;
                result.push_back(measured - geometric_angle_degrees(dimension.value));
                continue;
            }
            if (has_coordinate_axis_reference(dimension)) {
                const auto* point = find_point(dimension.first_point_id);
                result.push_back((dimension.kind == DimensionKind::DistanceX
                    ? point->x : point->y) - dimension.value);
                continue;
            }
            const auto first = point_position(dimension.first_point_id);
            const auto second = point_position(dimension.second_point_id);
            const double dx = (*second)[0] - (*first)[0];
            const double dy = (*second)[1] - (*first)[1];
            result.push_back(dimension.kind == DimensionKind::DistanceX
                ? dx - dimension.value
                : dimension.kind == DimensionKind::DistanceY
                    ? dy - dimension.value
                : dimension.kind == DimensionKind::Angle
                    ? wrapped_degrees(
                        std::atan2(dy, dx) * 180.0 / 3.14159265358979323846 -
                        dimension.value)
                    : std::hypot(dx, dy) - dimension.value);
        }
        return result;
    };
    std::vector<double*> variables;
    std::unordered_map<std::string, std::vector<std::size_t>> entity_columns;
    for (auto& point : points) {
        if (!immutable(point)) {
            const auto first_column = variables.size();
            variables.push_back(&point.x);
            variables.push_back(&point.y);
            entity_columns[point.id] = {first_column, first_column + 1};
        }
    }
    for (auto& circle : circles) {
        entity_columns[circle.id] = entity_columns[circle.center_point_id];
        entity_columns[circle.id].push_back(variables.size());
        variables.push_back(&circle.radius);
    }
    for (auto& arc : arcs) {
        auto& columns = entity_columns[arc.id];
        for (const auto& point_id : {arc.center_point_id, arc.start_point_id,
                                    arc.end_point_id}) {
            const auto& point_columns = entity_columns[point_id];
            columns.insert(columns.end(), point_columns.begin(), point_columns.end());
        }
        columns.push_back(variables.size());
        variables.push_back(&arc.radius);
    }
    for (auto& radius : corner_radii) {
        // The owned visible radius dimension is a driving parameter. Unlike
        // a generic geometric dimension it needs no residual equation; the
        // value itself is authoritative and therefore is not a free solver
        // variable.
        auto& columns = entity_columns[radius.id];
        columns = entity_columns[radius.vertex_id];
        if (!radius.dimension_visible) {
            columns.push_back(variables.size());
            variables.push_back(&radius.radius);
        }
    }
    const auto append_entity = [&](const std::string& target,
                                   const std::string& source) {
        const auto found = entity_columns.find(source);
        if (found == entity_columns.end()) return;
        auto& columns = entity_columns[target];
        columns.insert(columns.end(), found->second.begin(), found->second.end());
    };
    for (const auto& segment : segments) {
        append_entity(segment.id, segment.first_point_id);
        append_entity(segment.id, segment.second_point_id);
    }
    for (const auto& radius : corner_radii) {
        append_entity(radius.id, radius.first_segment_id);
        append_entity(radius.id, radius.second_segment_id);
    }
    for (const auto& ellipse : ellipses) {
        for (const auto& point_id : {ellipse.center_point_id,
                                    ellipse.major_point_id,
                                    ellipse.minor_point_id}) {
            append_entity(ellipse.id, point_id);
        }
    }
    for (const auto& arc : elliptical_arcs) {
        for (const auto& point_id : {arc.center_point_id, arc.major_point_id,
                                    arc.minor_point_id, arc.start_point_id,
                                    arc.end_point_id}) {
            append_entity(arc.id, point_id);
        }
    }
    for (const auto& spline : bsplines) {
        for (const auto& point_id : spline.control_point_ids) {
            append_entity(spline.id, point_id);
        }
    }

    std::vector<std::size_t> structural_parents(variables.size());
    std::iota(structural_parents.begin(), structural_parents.end(), std::size_t{});
    const auto structural_root = [&](std::size_t item) {
        while (structural_parents[item] != item) {
            structural_parents[item] = structural_parents[structural_parents[item]];
            item = structural_parents[item];
        }
        return item;
    };
    const auto structural_unite = [&](std::size_t first, std::size_t second) {
        first = structural_root(first);
        second = structural_root(second);
        if (first != second) structural_parents[second] = first;
    };
    const auto unite_entities = [&](std::initializer_list<std::string> ids) {
        std::optional<std::size_t> first;
        for (const auto& id : ids) {
            const auto found = entity_columns.find(id);
            if (found == entity_columns.end()) continue;
            for (const auto column : found->second) {
                if (first) structural_unite(*first, column);
                else first = column;
            }
        }
    };
    for (const auto& [_, columns] : entity_columns) {
        if (columns.empty()) continue;
        for (std::size_t index = 1; index < columns.size(); ++index) {
            structural_unite(columns.front(), columns[index]);
        }
    }
    std::set<std::string> tangent_contact_points;
    for (const auto& constraint : constraints) {
        if (!constraint.suppressed &&
            constraint.kind == ConstraintKind::Tangent &&
            !constraint.first_point_id.empty()) {
            tangent_contact_points.insert(constraint.first_point_id);
        }
    }
    for (const auto& constraint : constraints) {
        if (constraint.suppressed) continue;
        if ((constraint.kind == ConstraintKind::PointOnCircle ||
             constraint.kind == ConstraintKind::PointOnLine) &&
            tangent_contact_points.contains(constraint.first_point_id)) {
            continue;
        }
        unite_entities({constraint.first_point_id, constraint.second_point_id,
                        constraint.geometry_id, constraint.second_geometry_id});
    }
    for (const auto& dimension : dimensions) {
        if (dimension.suppressed || !dimension.driving) continue;
        unite_entities({dimension.first_point_id, dimension.second_point_id,
                        dimension.geometry_id, dimension.second_geometry_id});
    }

    std::vector<std::vector<std::size_t>> equation_columns;
    const auto append_equations = [&](std::size_t count,
                                      std::initializer_list<std::string> ids) {
        std::vector<std::size_t> columns;
        for (const auto& id : ids) {
            const auto found = entity_columns.find(id);
            if (found == entity_columns.end()) continue;
            columns.insert(columns.end(),
                found->second.begin(), found->second.end());
        }
        std::ranges::sort(columns);
        columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
        equation_columns.insert(equation_columns.end(), count, columns);
    };
    for (const auto& constraint : constraints) {
        if (constraint.suppressed) continue;
        const std::size_t count =
            constraint.kind == ConstraintKind::Concentric ||
            constraint.kind == ConstraintKind::Midpoint ||
            constraint.kind == ConstraintKind::Symmetric ||
            constraint.kind == ConstraintKind::Coincident ? 2 : 1;
        append_equations(count,
            {constraint.first_point_id, constraint.second_point_id,
             constraint.geometry_id, constraint.second_geometry_id});
    }
    for (const auto& dimension : dimensions) {
        if (dimension.suppressed || !dimension.driving) continue;
        const std::size_t count =
            dimension.kind == DimensionKind::DistanceSymmetric &&
                    !dimension.second_point_id.empty() ? 2 : 1;
        append_equations(count,
            {dimension.first_point_id, dimension.second_point_id,
             dimension.geometry_id, dimension.second_geometry_id});
    }
    const auto base = residuals();
    if (equation_columns.size() != base.size()) {
        throw std::runtime_error("Sketch equation ownership is inconsistent");
    }
    std::vector<std::vector<double>> jacobian(
        base.size(), std::vector<double>(variables.size()));
    constexpr double step = 1.0e-6;
    std::vector<std::size_t> column_colors(
        variables.size(), std::numeric_limits<std::size_t>::max());
    std::size_t color_count{};
    std::unordered_map<std::size_t, std::vector<std::size_t>> structural_columns;
    for (std::size_t column = 0; column < variables.size(); ++column) {
        structural_columns[structural_root(column)].push_back(column);
    }
    std::size_t largest_component{};
    for (const auto& [_, columns] : structural_columns) {
        largest_component = std::max(largest_component, columns.size());
    }
    if (largest_component <= 8) {
        // Many tiny independent branches: local position is already a valid
        // colouring and avoids constructing a general conflict graph.
        for (const auto& [_, columns] : structural_columns) {
            for (std::size_t color = 0; color < columns.size(); ++color) {
                column_colors[columns[color]] = color;
            }
            color_count = std::max(color_count, columns.size());
        }
    } else {
        // Large connected component: greedy column-intersection colouring.
        // Columns sharing an equation differ; distant variables in the same
        // chain may still be differentiated simultaneously.
        std::vector<std::unordered_set<std::size_t>> conflicts(variables.size());
        for (const auto& columns : equation_columns) {
            for (std::size_t first = 0; first < columns.size(); ++first) {
                for (std::size_t second = first + 1;
                     second < columns.size(); ++second) {
                    conflicts[columns[first]].insert(columns[second]);
                    conflicts[columns[second]].insert(columns[first]);
                }
            }
        }
        for (std::size_t column = 0; column < variables.size(); ++column) {
            std::unordered_set<std::size_t> used;
            for (const auto neighbor : conflicts[column]) {
                if (column_colors[neighbor] !=
                    std::numeric_limits<std::size_t>::max()) {
                    used.insert(column_colors[neighbor]);
                }
            }
            std::size_t color{};
            while (used.contains(color)) ++color;
            column_colors[column] = color;
            color_count = std::max(color_count, color + 1);
        }
    }
    for (std::size_t color = 0; color < color_count; ++color) {
        std::vector<std::pair<std::size_t, double>> shifted_columns;
        for (std::size_t column = 0; column < variables.size(); ++column) {
            if (column_colors[column] != color) continue;
            shifted_columns.emplace_back(column, *variables[column]);
            *variables[column] = shifted_columns.back().second + step;
        }
        const auto shifted = residuals();
        for (const auto& [column, original_value] : shifted_columns) {
            *variables[column] = original_value;
            for (std::size_t row = 0; row < base.size(); ++row) {
                if (std::ranges::binary_search(equation_columns[row], column)) {
                    jacobian[row][column] = (shifted[row] - base[row]) / step;
                }
            }
        }
    }
    // The numerical Jacobian is naturally block diagonal for disconnected
    // Sketch branches. Eliminating the full dense matrix made unrelated
    // geometry cubic work during every drag and dimension edit. Discover the
    // blocks from the evaluated Jacobian itself, so every current and future
    // constraint kind is partitioned by its actual dependencies rather than
    // by a second hand-maintained topology model.
    std::vector<std::size_t> parents(variables.size());
    std::iota(parents.begin(), parents.end(), std::size_t{});
    const auto root_of = [&](std::size_t item) {
        while (parents[item] != item) {
            parents[item] = parents[parents[item]];
            item = parents[item];
        }
        return item;
    };
    const auto unite = [&](std::size_t first, std::size_t second) {
        first = root_of(first);
        second = root_of(second);
        if (first != second) parents[second] = first;
    };
    constexpr double dependency_tolerance = 1.0e-10;
    for (const auto& row : jacobian) {
        std::optional<std::size_t> first_column;
        for (std::size_t column = 0; column < row.size(); ++column) {
            if (std::abs(row[column]) <= dependency_tolerance) continue;
            if (first_column) unite(*first_column, column);
            else first_column = column;
        }
    }
    std::unordered_map<std::size_t, std::vector<std::size_t>> component_columns;
    for (std::size_t column = 0; column < variables.size(); ++column) {
        component_columns[root_of(column)].push_back(column);
    }
    std::unordered_map<std::size_t, std::vector<std::size_t>> component_rows;
    for (std::size_t row_index = 0; row_index < jacobian.size(); ++row_index) {
        for (std::size_t column = 0; column < variables.size(); ++column) {
            if (std::abs(jacobian[row_index][column]) > dependency_tolerance) {
                component_rows[root_of(column)].push_back(row_index);
                break;
            }
        }
    }
    std::size_t rank{};
    for (const auto& [component, columns] : component_columns) {
        const auto rows_found = component_rows.find(component);
        if (rows_found == component_rows.end()) continue;
        std::vector<std::vector<double>> block(
            rows_found->second.size(), std::vector<double>(columns.size()));
        std::size_t nonzero_count{};
        for (std::size_t local_row = 0; local_row < rows_found->second.size();
             ++local_row) {
            for (std::size_t local_column = 0; local_column < columns.size();
                 ++local_column) {
                block[local_row][local_column] =
                    jacobian[rows_found->second[local_row]][columns[local_column]];
                if (std::abs(block[local_row][local_column]) >
                    dependency_tolerance) ++nonzero_count;
            }
        }
        std::size_t block_rank{};
        const double density = block.empty() || columns.empty() ? 0.0
            : static_cast<double>(nonzero_count) /
                static_cast<double>(block.size() * columns.size());
        if (columns.size() >= 64 && density < 0.15) {
            std::vector<std::map<std::size_t, double>> sparse(block.size());
            for (std::size_t row = 0; row < block.size(); ++row) {
                for (std::size_t column = 0; column < columns.size(); ++column) {
                    if (std::abs(block[row][column]) > dependency_tolerance) {
                        sparse[row][column] = block[row][column];
                    }
                }
            }
            block.clear();
            for (std::size_t column = 0;
                 column < columns.size() && block_rank < sparse.size(); ++column) {
                std::optional<std::size_t> pivot;
                double pivot_value{};
                for (std::size_t row = block_rank; row < sparse.size(); ++row) {
                    const auto found = sparse[row].find(column);
                    if (found != sparse[row].end() &&
                        std::abs(found->second) > std::abs(pivot_value)) {
                        pivot = row;
                        pivot_value = found->second;
                    }
                }
                if (!pivot || std::abs(pivot_value) < 1.0e-7) continue;
                std::swap(sparse[block_rank], sparse[*pivot]);
                const double divisor = sparse[block_rank].at(column);
                for (auto& [_, value] : sparse[block_rank]) value /= divisor;
                for (std::size_t row = 0; row < sparse.size(); ++row) {
                    if (row == block_rank) continue;
                    const auto factor_found = sparse[row].find(column);
                    if (factor_found == sparse[row].end()) continue;
                    const double factor = factor_found->second;
                    for (const auto& [index, pivot_entry] : sparse[block_rank]) {
                        const double updated = sparse[row][index] -
                            factor * pivot_entry;
                        if (std::abs(updated) <= dependency_tolerance) {
                            sparse[row].erase(index);
                        } else {
                            sparse[row][index] = updated;
                        }
                    }
                }
                ++block_rank;
            }
        } else {
            for (std::size_t column = 0;
                 column < columns.size() && block_rank < block.size(); ++column) {
                auto pivot = block_rank;
                for (std::size_t row = block_rank + 1; row < block.size(); ++row) {
                    if (std::abs(block[row][column]) >
                        std::abs(block[pivot][column])) pivot = row;
                }
                if (std::abs(block[pivot][column]) < 1.0e-7) continue;
                std::swap(block[block_rank], block[pivot]);
                const double divisor = block[block_rank][column];
                for (std::size_t index = column; index < columns.size(); ++index) {
                    block[block_rank][index] /= divisor;
                }
                for (std::size_t row = 0; row < block.size(); ++row) {
                    if (row == block_rank) continue;
                    const double factor = block[row][column];
                    for (std::size_t index = column; index < columns.size(); ++index) {
                        block[row][index] -= factor * block[block_rank][index];
                    }
                }
                ++block_rank;
            }
        }
        rank += block_rank;
    }
    const std::size_t dof = variables.size() > rank ? variables.size() - rank : 0;
    const SolveResult solved{
        dof == 0 ? SolveStatus::Solved : SolveStatus::UnderConstrained,
        dof, maximum_residual};
    solved_rank_cache_key_ = rank_cache_key;
    solved_rank_cache_result_ = solved;
    return solved;
}

zima::kernel::ViewerMesh Sketch::viewer_mesh() const {
    validate();
    if (std::any_of(corner_radii.begin(), corner_radii.end(),
            [](const auto& value) {
                return !value.suppressed && value.radius > 1.0e-9;
            })) {
        auto evaluated = evaluated_profile_sketch();
        // A corner radius owns its parameter and annotation. Feed a transient
        // render adapter to the evaluated profile; never persist a generic
        // dimension against the derived arc.
        for (const auto& radius : corner_radii) {
            if (radius.suppressed || radius.radius <= 1.0e-9 ||
                !radius.dimension_visible) continue;
            SketchDimension display;
            display.id = "corner-display:" + radius.id;
            display.kind = DimensionKind::Radius;
            display.value = radius.radius;
            display.geometry_id = radius.id;
            display.driving = false;
            display.placement = radius.dimension_placement;
            evaluated.dimensions.push_back(std::move(display));
        }
        auto result = evaluated.viewer_mesh();
        for (auto& edge : result.edges) {
            constexpr std::string_view arc_prefix{"arc:"};
            if (!edge.reference.semantic_key.starts_with(arc_prefix)) continue;
            const auto arc_id = edge.reference.semantic_key.substr(arc_prefix.size());
            if (std::any_of(corner_radii.begin(), corner_radii.end(),
                    [&](const auto& radius) { return radius.id == arc_id; })) {
                edge.reference.semantic_key = "corner_radius:" + arc_id;
            }
        }
        for (auto& dimension : result.dimensions) {
            constexpr std::string_view prefix{"dimension:corner-display:"};
            if (dimension.reference.semantic_key.starts_with(prefix)) {
                dimension.reference.semantic_key = "corner_dimension:" +
                    dimension.reference.semantic_key.substr(prefix.size());
            }
        }
        for (auto& point : result.points) {
            constexpr std::string_view point_prefix{"point:"};
            if (!point.reference.semantic_key.starts_with(point_prefix)) continue;
            const auto derived_id = point.reference.semantic_key.substr(
                point_prefix.size());
            for (const auto& radius : corner_radii) {
                const auto base = radius.id + ":";
                if (derived_id.starts_with(base + "tangent:first:parent:")) {
                    point.reference.semantic_key =
                        "corner_radius_handle:" + radius.id + ":first";
                    break;
                }
                if (derived_id.starts_with(base + "tangent:second:parent:")) {
                    point.reference.semantic_key =
                        "corner_radius_handle:" + radius.id + ":second";
                    break;
                }
            }
        }
        const auto source_point = [&](const auto& viewer_point) {
            if (viewer_point.reference.semantic_key.starts_with(
                    "corner_radius_handle:")) return true;
            constexpr std::string_view prefix{"point:"};
            if (!viewer_point.reference.semantic_key.starts_with(prefix)) return true;
            return find_point(viewer_point.reference.semantic_key.substr(
                prefix.size())) != nullptr;
        };
        std::erase_if(result.points,
            [&](const auto& point) { return !source_point(point); });
        return result;
    }
    const auto project = [&](const SketchPoint& point) {
        return world_point(point.x, point.y);
    };
    zima::kernel::ViewerMesh result;
    double axis_half_extent = 50.0;
    for (const auto& point : points) {
        axis_half_extent = std::max(
            axis_half_extent, 1.25 * std::max(std::abs(point.x), std::abs(point.y)));
    }
    for (const auto& text : texts) {
        axis_half_extent = std::max(axis_half_extent,
            1.25 * std::max(std::abs(text.anchor_x), std::abs(text.anchor_y)));
        for (const auto& contour : text.contours) {
            for (const auto& point : contour) {
                axis_half_extent = std::max(axis_half_extent,
                    1.25 * std::max(std::abs(point[0]), std::abs(point[1])));
            }
        }
    }
    for (const auto& reference : external_references) {
        for (const auto& point : reference.cached_points) {
            axis_half_extent = std::max(axis_half_extent,
                1.25 * std::max(std::abs(point[0]), std::abs(point[1])));
        }
        for (const auto& path : reference.cached_paths) {
            for (const auto& point : path) {
                axis_half_extent = std::max(axis_half_extent,
                    1.25 * std::max(std::abs(point[0]), std::abs(point[1])));
            }
        }
    }
    const auto origin = world_point(0.0, 0.0);
    const auto x_end = world_point(1.0, 0.0);
    const auto y_end = world_point(0.0, 1.0);
    result.axes.push_back({
        origin,
        {x_end.x - origin.x, x_end.y - origin.y, x_end.z - origin.z},
        axis_half_extent * 2.0, {id, "sketch_axis:x", {}}});
    result.axes.push_back({
        origin,
        {y_end.x - origin.x, y_end.y - origin.y, y_end.z - origin.z},
        axis_half_extent * 2.0, {id, "sketch_axis:y", {}}});
    // The Sketch origin is a stable reference in the ZIMA model even though
    // it is not an editable SketchPoint. Publish it to the common viewer
    // candidate stream so hover, confirmation and the solver refer to the
    // same object.
    result.points.reserve(points.size() + segments.size() + 1);
    result.points.push_back(
        {origin, {id, "external_point:sketch_origin", {}}, {}, true});
    for (const auto& point : points) {
        result.points.push_back(
            {project(point), {id, "point:" + point.id, {}}, {}, true,
             point.construction});
    }
    for (const auto& segment : segments) {
        const auto* first = find_point(segment.first_point_id);
        const auto* second = find_point(segment.second_point_id);
        result.points.push_back({world_point(
                (first->x + second->x) * 0.5,
                (first->y + second->y) * 0.5),
            {id, "sketch_midpoint:" + segment.id, {}}, {}, false});
    }
    struct PlacementLine {
        std::string id;
        std::array<double, 2> origin;
        std::array<double, 2> direction;
        bool bounded{};
    };
    std::vector<PlacementLine> placement_lines{
        {"sketch_axis:x", {0.0, 0.0}, {1.0, 0.0}, false},
        {"sketch_axis:y", {0.0, 0.0}, {0.0, 1.0}, false}};
    placement_lines.reserve(segments.size() + 2);
    for (const auto& segment : segments) {
        const auto* first = find_point(segment.first_point_id);
        const auto* second = find_point(segment.second_point_id);
        placement_lines.push_back({segment.id, {first->x, first->y},
            {second->x - first->x, second->y - first->y},
            !segment.construction});
    }
    for (std::size_t first_index = 0; first_index < placement_lines.size();
         ++first_index) {
        const auto& first = placement_lines[first_index];
        for (std::size_t second_index = first_index + 1;
             second_index < placement_lines.size(); ++second_index) {
            const auto& second = placement_lines[second_index];
            const double denominator =
                first.direction[0] * second.direction[1] -
                first.direction[1] * second.direction[0];
            if (std::abs(denominator) <= 1.0e-12) continue;
            const double offset_x = second.origin[0] - first.origin[0];
            const double offset_y = second.origin[1] - first.origin[1];
            const double first_parameter =
                (offset_x * second.direction[1] -
                 offset_y * second.direction[0]) / denominator;
            const double second_parameter =
                (offset_x * first.direction[1] -
                 offset_y * first.direction[0]) / denominator;
            if ((first.bounded &&
                    (first_parameter < -1.0e-12 ||
                     first_parameter > 1.0 + 1.0e-12)) ||
                (second.bounded &&
                    (second_parameter < -1.0e-12 ||
                     second_parameter > 1.0 + 1.0e-12))) continue;
            result.points.push_back({world_point(
                    first.origin[0] + first_parameter * first.direction[0],
                    first.origin[1] + first_parameter * first.direction[1]),
                {id, "sketch_intersection:" + first.id + "||" + second.id, {}},
                {}, false});
        }
    }
    constexpr double half_turn = 3.14159265358979323846;
    constexpr double quarter_turn = half_turn * 0.5;
    for (const auto& circle : circles) {
        const auto* center = find_point(circle.center_point_id);
        for (int quarter = 0; quarter < 4; ++quarter) {
            const double angle = quarter_turn * static_cast<double>(quarter);
            result.points.push_back({world_point(
                    center->x + circle.radius * std::cos(angle),
                    center->y + circle.radius * std::sin(angle)),
                {id, "sketch_curve_keypoint:circle:" + circle.id + ":" +
                    std::to_string(quarter), {}}, {}, false});
        }
    }
    for (const auto& arc : arcs) {
        const auto* center = find_point(arc.center_point_id);
        for (int quarter = 0; quarter < 4; ++quarter) {
            double angle = quarter_turn * static_cast<double>(quarter);
            while (angle < arc.start_angle) angle += 2.0 * half_turn;
            if (angle > arc.end_angle + 1.0e-12) continue;
            result.points.push_back({world_point(
                    center->x + arc.radius * std::cos(angle),
                    center->y + arc.radius * std::sin(angle)),
                {id, "sketch_curve_keypoint:arc:" + arc.id + ":" +
                    std::to_string(quarter), {}}, {}, false});
        }
    }
    for (const auto& ellipse : ellipses) {
        const auto* center = find_point(ellipse.center_point_id);
        const auto* major = find_point(ellipse.major_point_id);
        const auto* minor = find_point(ellipse.minor_point_id);
        for (int quarter = 0; quarter < 4; ++quarter) {
            const double parameter = quarter_turn * static_cast<double>(quarter);
            result.points.push_back({world_point(
                    center->x + (major->x - center->x) * std::cos(parameter) +
                        (minor->x - center->x) * std::sin(parameter),
                    center->y + (major->y - center->y) * std::cos(parameter) +
                        (minor->y - center->y) * std::sin(parameter)),
                {id, "sketch_curve_keypoint:ellipse:" + ellipse.id + ":" +
                    std::to_string(quarter), {}}, {}, false});
        }
    }
    for (const auto& arc : elliptical_arcs) {
        const auto* center = find_point(arc.center_point_id);
        const auto* major = find_point(arc.major_point_id);
        const auto* minor = find_point(arc.minor_point_id);
        for (int quarter = 0; quarter < 4; ++quarter) {
            double parameter = quarter_turn * static_cast<double>(quarter);
            while (parameter < arc.start_parameter) parameter += 2.0 * half_turn;
            if (parameter > arc.end_parameter + 1.0e-12) continue;
            result.points.push_back({world_point(
                    center->x + (major->x - center->x) * std::cos(parameter) +
                        (minor->x - center->x) * std::sin(parameter),
                    center->y + (major->y - center->y) * std::cos(parameter) +
                        (minor->y - center->y) * std::sin(parameter)),
                {id, "sketch_curve_keypoint:elliptical_arc:" + arc.id + ":" +
                    std::to_string(quarter), {}}, {}, false});
        }
    }
    std::vector<std::string> placement_curve_ids;
    placement_curve_ids.reserve(circles.size() + arcs.size() + ellipses.size() +
        elliptical_arcs.size() + bsplines.size());
    for (const auto& curve : circles) placement_curve_ids.push_back(curve.id);
    for (const auto& curve : arcs) placement_curve_ids.push_back(curve.id);
    for (const auto& curve : ellipses) placement_curve_ids.push_back(curve.id);
    for (const auto& curve : elliptical_arcs)
        placement_curve_ids.push_back(curve.id);
    for (const auto& curve : bsplines) placement_curve_ids.push_back(curve.id);
    for (const auto& line : placement_lines) {
        for (const auto& curve_id : placement_curve_ids) {
            for (const auto& point : curve_line_intersections(
                    curve_id, line.origin, line.direction, line.bounded)) {
                result.points.push_back({world_point(point[0], point[1]),
                    {id, "sketch_intersection:" + line.id + "||" + curve_id, {}},
                    {}, false});
            }
        }
    }
    for (const auto& reference : external_references) {
        if (reference.kind != ExternalReferenceKind::Point) continue;
        const auto& point = reference.cached_points.front();
        result.points.push_back({world_point(point[0], point[1]),
            {id, "external_point:" + reference.id +
                (reference.broken ? ":broken" : ""), {}}});
    }
    result.edges.reserve(segments.size());
    for (const auto& segment : segments) {
        result.edges.push_back({
            {project(*find_point(segment.first_point_id)),
             project(*find_point(segment.second_point_id))},
            {id, "segment:" + segment.id, {}}, segment.construction, true,
            segment.centerline, segment.centerline});
    }
    constexpr std::size_t circle_samples = 96;
    for (const auto& circle : circles) {
        const auto* center = find_point(circle.center_point_id);
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "circle:" + circle.id, {}};
        edge.construction = circle.construction;
        edge.overlay = true;
        edge.points.reserve(circle_samples + 1);
        for (std::size_t sample = 0; sample <= circle_samples; ++sample) {
            const double angle = 2.0 * 3.14159265358979323846 *
                static_cast<double>(sample) / static_cast<double>(circle_samples);
            edge.points.push_back(world_point(
                center->x + circle.radius * std::cos(angle),
                center->y + circle.radius * std::sin(angle)));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& spline : bsplines) {
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "bspline:" + spline.id, {}};
        edge.construction = spline.construction;
        edge.overlay = true;
        for (const auto& point : sampled_bspline_points(*this, spline, 128)) {
            edge.points.push_back(world_point(point[0], point[1]));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& ellipse : ellipses) {
        const auto* center = find_point(ellipse.center_point_id);
        const double orientation = ellipse.reversed ? -1.0 : 1.0;
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "ellipse:" + ellipse.id, {}};
        edge.construction = ellipse.construction;
        edge.overlay = true;
        edge.points.reserve(circle_samples + 1);
        for (std::size_t sample = 0; sample <= circle_samples; ++sample) {
            const double parameter = 2.0 * 3.14159265358979323846 *
                static_cast<double>(sample) / static_cast<double>(circle_samples);
            const double local_x = ellipse.major_radius * std::cos(parameter);
            const double local_y = ellipse.minor_radius * std::sin(parameter);
            edge.points.push_back(world_point(
                center->x + local_x * std::cos(ellipse.rotation) -
                    orientation * local_y * std::sin(ellipse.rotation),
                center->y + local_x * std::sin(ellipse.rotation) +
                    orientation * local_y * std::cos(ellipse.rotation)));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& arc : elliptical_arcs) {
        const auto* center = find_point(arc.center_point_id);
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "elliptical_arc:" + arc.id, {}};
        edge.construction = arc.construction;
        edge.overlay = true;
        const double sweep = arc.end_parameter - arc.start_parameter;
        const auto samples = std::max<std::size_t>(8,
            static_cast<std::size_t>(std::ceil(192.0 * sweep / full_turn)));
        edge.points.reserve(samples + 1);
        for (std::size_t sample = 0; sample <= samples; ++sample) {
            const double parameter = arc.start_parameter + sweep *
                static_cast<double>(sample) / static_cast<double>(samples);
            const auto position = ellipse_position(
                center->x, center->y, arc.major_radius, arc.minor_radius,
                arc.rotation, arc.reversed, parameter);
            edge.points.push_back(world_point(position[0], position[1]));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& arc : arcs) {
        const auto* center = find_point(arc.center_point_id);
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "arc:" + arc.id, {}};
        edge.construction = arc.construction;
        edge.overlay = true;
        const double sweep = arc.end_angle - arc.start_angle;
        const auto samples = std::max<std::size_t>(2,
            static_cast<std::size_t>(std::ceil(96.0 * sweep /
                (2.0 * 3.14159265358979323846))));
        edge.points.reserve(samples + 1);
        for (std::size_t sample = 0; sample <= samples; ++sample) {
            const double angle = arc.start_angle + sweep *
                static_cast<double>(sample) / static_cast<double>(samples);
            edge.points.push_back(world_point(
                center->x + arc.radius * std::cos(angle),
                center->y + arc.radius * std::sin(angle)));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& text : texts) {
        for (const auto& contour : text.contours) {
            zima::kernel::ViewerEdge edge;
            edge.reference = {id, "text:" + text.id + ":" +
                text_color_name(text.color), {}};
            edge.overlay = true;
            edge.points.reserve(contour.size() + 1);
            for (const auto& point : contour) {
                edge.points.push_back(world_point(point[0], point[1]));
            }
            edge.points.push_back(edge.points.front());
            result.edges.push_back(std::move(edge));
        }
    }
    for (const auto& reference : external_references) {
        if (reference.kind != ExternalReferenceKind::Edge &&
            reference.kind != ExternalReferenceKind::Axis) continue;
        zima::kernel::ViewerEdge edge;
        edge.reference = {id,
            (reference.kind == ExternalReferenceKind::Axis
                ? "external_axis:" : "external_edge:") + reference.id +
            (reference.broken ? ":broken" : ""), {}};
        edge.construction = true;
        edge.overlay = true;
        edge.infinite = reference.infinite;
        edge.points.reserve(reference.cached_points.size());
        for (const auto& point : reference.cached_points) {
            edge.points.push_back(world_point(point[0], point[1]));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& reference : external_references) {
        if (reference.kind != ExternalReferenceKind::Face) continue;
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "external_face:" + reference.id +
            (reference.broken ? ":broken" : ""), {}};
        edge.construction = true;
        edge.overlay = true;
        edge.infinite = true;
        edge.points.reserve(reference.cached_points.size());
        for (const auto& point : reference.cached_points) {
            edge.points.push_back(world_point(point[0], point[1]));
        }
        result.edges.push_back(std::move(edge));
    }
    const auto geometry_anchor = [&](const std::string& geometry_id)
            -> std::optional<zima::kernel::Vec3> {
        if (const auto segment = std::find_if(segments.begin(), segments.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            segment != segments.end()) {
            const auto* first = find_point(segment->first_point_id);
            const auto* second = find_point(segment->second_point_id);
            return world_point((first->x + second->x) * 0.5,
                               (first->y + second->y) * 0.5);
        }
        if (const auto circle = std::find_if(circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            circle != circles.end()) {
            const auto* center = find_point(circle->center_point_id);
            return world_point(center->x + circle->radius, center->y);
        }
        if (const auto arc = std::find_if(arcs.begin(), arcs.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            arc != arcs.end()) {
            const auto* center = find_point(arc->center_point_id);
            const double angle = (arc->start_angle + arc->end_angle) * 0.5;
            return world_point(center->x + arc->radius * std::cos(angle),
                               center->y + arc->radius * std::sin(angle));
        }
        if (const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                [&](const auto& value) { return value.id == geometry_id; });
            ellipse != ellipses.end()) {
            const auto* center = find_point(ellipse->center_point_id);
            return world_point(
                center->x + ellipse->major_radius * std::cos(ellipse->rotation),
                center->y + ellipse->major_radius * std::sin(ellipse->rotation));
        }
        return std::nullopt;
    };
    const auto marker_label = [](ConstraintKind kind) -> std::string {
        switch (kind) {
            case ConstraintKind::Horizontal: return "H";
            case ConstraintKind::Vertical: return "V";
            case ConstraintKind::Coincident: return "C";
            case ConstraintKind::Parallel: return "//";
            case ConstraintKind::Perpendicular: return "⊥";
            case ConstraintKind::EqualLength:
            case ConstraintKind::EqualRadius: return "=";
            case ConstraintKind::PointOnCircle:
            case ConstraintKind::PointOnLine: return "C";
            case ConstraintKind::MidpointOnLine: return "M";
            case ConstraintKind::Symmetric: return "S";
            case ConstraintKind::Midpoint: return "M";
            case ConstraintKind::Concentric: return "◎";
            case ConstraintKind::Tangent: return "T";
        }
        return {};
    };
    const auto geometry_semantic_key = [&](const std::string& geometry_id) {
        if (geometry_id.empty()) return std::string{};
        if (geometry_id == "sketch_axis:x" || geometry_id == "sketch_axis:y")
            return geometry_id;
        if (std::ranges::any_of(segments,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "segment:" + geometry_id;
        if (std::ranges::any_of(circles,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "circle:" + geometry_id;
        if (std::ranges::any_of(arcs,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "arc:" + geometry_id;
        if (std::ranges::any_of(ellipses,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "ellipse:" + geometry_id;
        if (std::ranges::any_of(elliptical_arcs,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "elliptical_arc:" + geometry_id;
        if (std::ranges::any_of(bsplines,
                [&](const auto& value) { return value.id == geometry_id; }))
            return "bspline:" + geometry_id;
        if (const auto reference = std::find_if(external_references.begin(),
                external_references.end(), [&](const auto& value) {
                    return value.id == geometry_id;
                }); reference != external_references.end()) {
            return std::string(reference->kind == ExternalReferenceKind::Point
                    ? "external_point:"
                : reference->kind == ExternalReferenceKind::Axis
                    ? "external_axis:"
                : reference->kind == ExternalReferenceKind::Face
                    ? "external_face:" : "external_edge:") + geometry_id;
        }
        return std::string{};
    };
    for (const auto& constraint : constraints) {
        if (constraint.suppressed) continue;
        std::optional<zima::kernel::Vec3> anchor;
        if (constraint.kind == ConstraintKind::Tangent &&
            !constraint.first_point_id.empty()) {
            if (const auto* contact = find_point(constraint.first_point_id)) {
                anchor = project(*contact);
            }
        }
        if (!anchor && constraint.kind == ConstraintKind::Tangent) {
            const bool first_segment = std::ranges::any_of(segments,
                [&](const auto& value) {
                    return value.id == constraint.geometry_id;
                });
            const bool second_segment = std::ranges::any_of(segments,
                [&](const auto& value) {
                    return value.id == constraint.second_geometry_id;
                });
            if (first_segment != second_segment) {
                const auto state = segment_curve_tangent_state(*this,
                    first_segment ? constraint.geometry_id
                                  : constraint.second_geometry_id,
                    first_segment ? constraint.second_geometry_id
                                  : constraint.geometry_id);
                if (state) anchor = world_point(state->contact_x, state->contact_y);
            }
        }
        const bool directional = constraint.kind == ConstraintKind::Horizontal ||
            constraint.kind == ConstraintKind::Vertical;
        const auto point_anchor = [&](const std::string& point_id)
                -> std::optional<zima::kernel::Vec3> {
            if (point_id == "sketch_origin") return world_point(0.0, 0.0);
            if (const auto* point = find_point(point_id)) return project(*point);
            return std::nullopt;
        };
        if (!anchor && directional && !constraint.geometry_id.empty()) {
            anchor = geometry_anchor(constraint.geometry_id);
        }
        if (!anchor &&
            (directional || constraint.kind == ConstraintKind::Coincident) &&
            !constraint.second_point_id.empty()) {
            anchor = point_anchor(constraint.second_point_id);
        }
        if (!anchor && !constraint.first_point_id.empty()) {
            anchor = point_anchor(constraint.first_point_id);
        }
        if (!anchor && !constraint.second_geometry_id.empty()) {
            anchor = geometry_anchor(constraint.second_geometry_id);
        }
        if (!anchor && !constraint.geometry_id.empty()) {
            anchor = geometry_anchor(constraint.geometry_id);
        }
        if (!anchor) continue;
        std::vector<std::string> participants;
        const bool coincident_relation =
            constraint.kind == ConstraintKind::Coincident ||
            constraint.kind == ConstraintKind::PointOnCircle ||
            constraint.kind == ConstraintKind::PointOnLine;
        const bool geometry_relation =
            coincident_relation || constraint.kind == ConstraintKind::Tangent;
        const auto append_point_geometry = [&](const std::string& point_id) {
            for (const auto& segment : segments) {
                if (segment.first_point_id == point_id ||
                    segment.second_point_id == point_id) {
                    participants.push_back("segment:" + segment.id);
                }
            }
            for (const auto& circle : circles) {
                if (circle.center_point_id == point_id)
                    participants.push_back("circle:" + circle.id);
            }
            for (const auto& arc : arcs) {
                if (arc.center_point_id == point_id ||
                    arc.start_point_id == point_id ||
                    arc.end_point_id == point_id) {
                    participants.push_back("arc:" + arc.id);
                }
            }
            for (const auto& ellipse : ellipses) {
                if (ellipse.center_point_id == point_id ||
                    ellipse.major_point_id == point_id ||
                    ellipse.minor_point_id == point_id) {
                    participants.push_back("ellipse:" + ellipse.id);
                }
            }
            for (const auto& arc : elliptical_arcs) {
                if (arc.center_point_id == point_id ||
                    arc.major_point_id == point_id ||
                    arc.minor_point_id == point_id ||
                    arc.start_point_id == point_id ||
                    arc.end_point_id == point_id) {
                    participants.push_back("elliptical_arc:" + arc.id);
                }
            }
            for (const auto& spline : bsplines) {
                if (std::ranges::find(spline.control_point_ids, point_id) !=
                    spline.control_point_ids.end()) {
                    participants.push_back("bspline:" + spline.id);
                }
            }
        };
        const auto append_geometry_points = [&](const std::string& geometry_id) {
            const auto append_point = [&](const std::string& point_id) {
                if (!point_id.empty()) participants.push_back("point:" + point_id);
            };
            if (const auto segment = std::ranges::find_if(segments,
                    [&](const auto& value) { return value.id == geometry_id; });
                segment != segments.end()) {
                append_point(segment->first_point_id);
                append_point(segment->second_point_id);
                return;
            }
            if (const auto circle = std::ranges::find_if(circles,
                    [&](const auto& value) { return value.id == geometry_id; });
                circle != circles.end()) {
                append_point(circle->center_point_id);
                return;
            }
            if (const auto arc = std::ranges::find_if(arcs,
                    [&](const auto& value) { return value.id == geometry_id; });
                arc != arcs.end()) {
                append_point(arc->center_point_id);
                append_point(arc->start_point_id);
                append_point(arc->end_point_id);
                return;
            }
            if (const auto ellipse = std::ranges::find_if(ellipses,
                    [&](const auto& value) { return value.id == geometry_id; });
                ellipse != ellipses.end()) {
                append_point(ellipse->center_point_id);
                append_point(ellipse->major_point_id);
                append_point(ellipse->minor_point_id);
                return;
            }
            if (const auto arc = std::ranges::find_if(elliptical_arcs,
                    [&](const auto& value) { return value.id == geometry_id; });
                arc != elliptical_arcs.end()) {
                append_point(arc->center_point_id);
                append_point(arc->major_point_id);
                append_point(arc->minor_point_id);
                append_point(arc->start_point_id);
                append_point(arc->end_point_id);
                return;
            }
            if (const auto spline = std::ranges::find_if(bsplines,
                    [&](const auto& value) { return value.id == geometry_id; });
                spline != bsplines.end()) {
                for (const auto& point_id : spline->control_point_ids)
                    append_point(point_id);
            }
        };
        for (const auto& point_id : {constraint.first_point_id,
                                     constraint.second_point_id}) {
            if (point_id.empty()) continue;
            if (point_id.starts_with("sketch_keypoint:")) {
                const auto kind_separator = point_id.find(':', 16);
                const auto quarter_separator = point_id.rfind(':');
                if (kind_separator != std::string::npos &&
                    quarter_separator != kind_separator) {
                    if (const auto key = geometry_semantic_key(point_id.substr(
                            kind_separator + 1,
                            quarter_separator - kind_separator - 1));
                        !key.empty()) participants.push_back(key);
                }
            } else {
                participants.push_back(
                    point_id == "sketch_origin" ? "origin:point"
                                                 : "point:" + point_id);
                if (coincident_relation && point_id != "sketch_origin")
                    append_point_geometry(point_id);
            }
        }
        for (const auto& geometry_id : {constraint.geometry_id,
                                        constraint.second_geometry_id}) {
            if (geometry_relation) {
                if (const auto key = geometry_semantic_key(geometry_id); !key.empty())
                    participants.push_back(key);
            } else {
                append_geometry_points(geometry_id);
            }
        }
        std::sort(participants.begin(), participants.end());
        participants.erase(
            std::unique(participants.begin(), participants.end()),
            participants.end());
        const auto marker_reference = zima::kernel::EdgeReference{
            id, "constraint:" + constraint.id, {}};
        result.constraint_markers.push_back({*anchor,
            marker_label(constraint.kind),
            marker_reference, participants});
        if (constraint.kind == ConstraintKind::Symmetric &&
            !constraint.second_point_id.empty()) {
            if (const auto* mirrored = find_point(constraint.second_point_id)) {
                result.constraint_markers.push_back({project(*mirrored),
                    marker_label(constraint.kind), marker_reference,
                    std::move(participants)});
            }
        }
    }
    for (const auto& point : points) {
        if (!point.fixed) continue;
        result.constraint_markers.push_back({project(point), "F",
            {id, "fixed:" + point.id, {}}, {"point:" + point.id}});
    }
    result.dimensions.reserve(dimensions.size());
    for (const auto& dimension : dimensions) {
        if (dimension.suppressed) continue;
        if (dimension.kind == DimensionKind::EllipseRotation) {
            const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (ellipse == ellipses.end()) continue;
            const auto* center = find_point(ellipse->center_point_id);
            const auto* major = find_point(ellipse->major_point_id);
            const double radius = std::hypot(
                major->x - center->x, major->y - center->y);
            result.dimensions.push_back({
                project(*center), project(*center),
                world_point(center->x + radius, center->y), project(*major),
                dimension.value, {id, "dimension:" + dimension.id, {}}, "∠", " °"});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Angular;
            result.dimensions.back().plane_normal = resolved_normal;
            result.dimensions.back().sweep_degrees = dimension.value;
            continue;
        }
        if (dimension.kind == DimensionKind::EllipseMajorRadius ||
            dimension.kind == DimensionKind::EllipseMinorRadius) {
            const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (ellipse == ellipses.end()) continue;
            const auto* center = find_point(ellipse->center_point_id);
            const auto* rim = find_point(
                dimension.kind == DimensionKind::EllipseMajorRadius
                    ? ellipse->major_point_id : ellipse->minor_point_id);
            result.dimensions.push_back({
                project(*center), project(*rim), project(*center), project(*rim),
                dimension.value, {id, "dimension:" + dimension.id, {}},
                dimension.kind == DimensionKind::EllipseMajorRadius ? "a=" : "b="});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Radius;
            continue;
        }
        if (dimension.kind == DimensionKind::Diameter) {
            const auto circle = std::find_if(circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            const SketchPoint* center{};
            double radius{};
            double default_angle{};
            if (circle != circles.end()) {
                center = find_point(circle->center_point_id);
                radius = circle->radius;
            } else {
                const auto arc = std::find_if(arcs.begin(), arcs.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                if (arc != arcs.end()) {
                    center = find_point(arc->center_point_id);
                    radius = arc->radius;
                    default_angle = (arc->start_angle + arc->end_angle) * 0.5;
                } else {
                    const auto corner = std::find_if(
                        corner_radii.begin(), corner_radii.end(),
                        [&](const auto& value) {
                            return value.id == dimension.geometry_id;
                        });
                    if (corner == corner_radii.end()) continue;
                    center = find_point(corner->vertex_id);
                    radius = corner->radius;
                }
            }
            const double angle = dimension.placement
                ? std::atan2((*dimension.placement)[1] - center->y,
                             (*dimension.placement)[0] - center->x)
                : default_angle;
            const auto rim = world_point(
                center->x + radius * std::cos(angle),
                center->y + radius * std::sin(angle));
            result.dimensions.push_back({
                project(*center), rim, project(*center), rim, dimension.value,
                {id, "dimension:" + dimension.id, {}}, "Ø"});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Diameter;
            continue;
        }
        if (dimension.kind == DimensionKind::Radius) {
            const auto circle = std::find_if(circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            const SketchPoint* center{};
            double radius{};
            double angle{};
            if (circle != circles.end()) {
                center = find_point(circle->center_point_id);
                radius = circle->radius;
            } else {
                const auto arc = std::find_if(arcs.begin(), arcs.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                if (arc != arcs.end()) {
                    center = find_point(arc->center_point_id);
                    radius = arc->radius;
                    angle = (arc->start_angle + arc->end_angle) * 0.5;
                } else {
                    const auto corner = std::find_if(
                        corner_radii.begin(), corner_radii.end(),
                        [&](const auto& value) {
                            return value.id == dimension.geometry_id;
                        });
                    if (corner == corner_radii.end()) continue;
                    center = find_point(corner->vertex_id);
                    radius = corner->radius;
                }
            }
            if (dimension.placement) {
                angle = std::atan2((*dimension.placement)[1] - center->y,
                                   (*dimension.placement)[0] - center->x);
            }
            const auto rim = world_point(
                center->x + radius * std::cos(angle),
                center->y + radius * std::sin(angle));
            result.dimensions.push_back({
                project(*center), rim, project(*center), rim, dimension.value,
                {id, "dimension:" + dimension.id, {}}, "R"});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Radius;
            continue;
        }
        if (dimension.kind == DimensionKind::DistancePointLine ||
            dimension.kind == DimensionKind::DistanceSymmetric) {
            const auto reference = sketch_axis_line(*this, dimension.geometry_id)
                ? sketch_axis_line(*this, dimension.geometry_id)
                : segment_or_external_line(*this, dimension.geometry_id);
            const auto* point = find_point(dimension.first_point_id);
            if (!reference || point == nullptr) continue;
            const double length_squared =
                reference->second[0] * reference->second[0] +
                reference->second[1] * reference->second[1];
            const double factor =
                ((point->x - reference->first[0]) * reference->second[0] +
                 (point->y - reference->first[1]) * reference->second[1]) /
                length_squared;
            const std::array projection{
                reference->first[0] + factor * reference->second[0],
                reference->first[1] + factor * reference->second[1]};
            const std::array first_anchor = dimension.kind ==
                    DimensionKind::DistanceSymmetric
                ? std::array{2.0 * projection[0] - point->x,
                             2.0 * projection[1] - point->y}
                : projection;
            const double length = std::sqrt(length_squared);
            double along_offset{};
            if (dimension.placement) {
                const std::array midpoint{
                    (first_anchor[0] + point->x) * 0.5,
                    (first_anchor[1] + point->y) * 0.5};
                along_offset =
                    (((*dimension.placement)[0] - midpoint[0]) *
                         reference->second[0] +
                     ((*dimension.placement)[1] - midpoint[1]) *
                         reference->second[1]) / length;
            }
            const std::array shift{
                reference->second[0] / length * along_offset,
                reference->second[1] / length * along_offset};
            result.dimensions.push_back({
                world_point(first_anchor[0], first_anchor[1]),
                world_point(point->x, point->y),
                world_point(first_anchor[0] + shift[0], first_anchor[1] + shift[1]),
                world_point(point->x + shift[0], point->y + shift[1]),
                dimension.value, {id, "dimension:" + dimension.id, {}},
                dimension.kind == DimensionKind::DistanceSymmetric ? "Ø" : ""});
            continue;
        }
        if (dimension.kind == DimensionKind::DistanceLine ||
            dimension.kind == DimensionKind::AngleBetween) {
            auto reference = sketch_axis_line(*this, dimension.geometry_id)
                ? sketch_axis_line(*this, dimension.geometry_id)
                : segment_or_external_line(*this, dimension.geometry_id);
            auto driven = segment_or_external_line(
                *this, dimension.second_geometry_id);
            if (dimension.kind == DimensionKind::AngleBetween) {
                if (const auto lines = angle_dimension_lines(*this, dimension)) {
                    reference = lines->first;
                    driven = lines->second;
                    if (dimension.angle_presentation_reversed) {
                        std::swap(reference, driven);
                    }
                }
            }
            if (dimension.kind == DimensionKind::AngleBetween &&
                dimension.second_geometry_id.empty()) {
                const auto* first = find_point(dimension.first_point_id);
                const auto* second = find_point(dimension.second_point_id);
                if (first != nullptr && second != nullptr) {
                    driven = std::pair{std::array{first->x, first->y},
                        std::array{second->x - first->x,
                                   second->y - first->y}};
                }
            }
            if (!reference || !driven) continue;
            const double rx = reference->second[0];
            const double ry = reference->second[1];
            if (dimension.kind == DimensionKind::DistanceLine) {
                std::array driven_anchor = driven->first;
                if (dimension.placement) {
                    const double driven_length_squared =
                        driven->second[0] * driven->second[0] +
                        driven->second[1] * driven->second[1];
                    const double driven_factor =
                        (((*dimension.placement)[0] - driven->first[0]) *
                             driven->second[0] +
                         ((*dimension.placement)[1] - driven->first[1]) *
                             driven->second[1]) /
                        driven_length_squared;
                    driven_anchor = {
                        driven->first[0] + driven_factor * driven->second[0],
                        driven->first[1] + driven_factor * driven->second[1]};
                }
                const double length_squared = rx * rx + ry * ry;
                const double factor =
                    ((driven_anchor[0] - reference->first[0]) * rx +
                     (driven_anchor[1] - reference->first[1]) * ry) /
                    length_squared;
                const std::array projection{
                    reference->first[0] + factor * rx,
                    reference->first[1] + factor * ry};
                result.dimensions.push_back({
                    world_point(projection[0], projection[1]),
                    world_point(driven_anchor[0], driven_anchor[1]),
                    world_point(projection[0], projection[1]),
                    world_point(driven_anchor[0], driven_anchor[1]),
                    dimension.value, {id, "dimension:" + dimension.id, {}}});
            } else {
                const double dx = driven->second[0];
                const double dy = driven->second[1];
                const double cross = rx * dy - ry * dx;
                if (std::abs(cross) <= 1.0e-12) continue;
                const double qx = driven->first[0] - reference->first[0];
                const double qy = driven->first[1] - reference->first[1];
                const double factor = (qx * dy - qy * dx) / cross;
                const std::array vertex{
                    reference->first[0] + factor * rx,
                    reference->first[1] + factor * ry};
                const double display_radius = dimension.placement
                    ? std::max(1.0, std::hypot(
                        (*dimension.placement)[0] - vertex[0],
                        (*dimension.placement)[1] - vertex[1]))
                    : 20.0;
                double ra = std::atan2(ry, rx);
                double sweep_radians = (cross < 0.0 ? -1.0 : 1.0) *
                    dimension.value * 3.14159265358979323846 / 180.0;
                double displayed_value = dimension.value;
                if (dimension.placement) {
                    constexpr double pi = 3.14159265358979323846;
                    constexpr double tau = 2.0 * pi;
                    const auto normalized = [tau](double angle) {
                        angle = std::fmod(angle, tau);
                        return angle < 0.0 ? angle + tau : angle;
                    };
                    const double reference_length = std::hypot(rx, ry);
                    const double driven_length = std::hypot(dx, dy);
                    std::array<double, 2> first_ray{
                        rx / reference_length, ry / reference_length};
                    std::array<double, 2> second_ray{
                        dx / driven_length, dy / driven_length};
                    const std::array cursor_vector{
                        (*dimension.placement)[0] - vertex[0],
                        (*dimension.placement)[1] - vertex[1]};
                    const double cursor_angle = normalized(std::atan2(
                        cursor_vector[1], cursor_vector[0]));
                    if (dimension.angle_sector < 0) {
                        if (cursor_vector[0] * first_ray[0] +
                                cursor_vector[1] * first_ray[1] < 0.0) {
                            first_ray[0] = -first_ray[0];
                            first_ray[1] = -first_ray[1];
                        }
                        if (cursor_vector[0] * second_ray[0] +
                                cursor_vector[1] * second_ray[1] < 0.0) {
                            second_ray[0] = -second_ray[0];
                            second_ray[1] = -second_ray[1];
                        }
                        ra = normalized(std::atan2(first_ray[1], first_ray[0]));
                        const double second_angle = normalized(
                            std::atan2(second_ray[1], second_ray[0]));
                        const double counterclockwise = normalized(second_angle - ra);
                        const double cursor_from_first = normalized(cursor_angle - ra);
                        sweep_radians = cursor_from_first <= counterclockwise
                            ? counterclockwise : counterclockwise - tau;
                        displayed_value = sweep_radians * 180.0 / pi;
                    } else {
                        if (dimension.angle_sector == 1) {
                            first_ray[0] = -first_ray[0];
                            first_ray[1] = -first_ray[1];
                        }
                        const double first_angle = normalized(
                            std::atan2(first_ray[1], first_ray[0]));
                        const double second_angle = normalized(
                            std::atan2(second_ray[1], second_ray[0]));
                        double candidate_sweep = normalized(
                            second_angle - first_angle);
                        if (candidate_sweep > pi) candidate_sweep -= tau;
                        const double candidate_middle =
                            first_angle + candidate_sweep * 0.5;
                        const double opposite_middle = candidate_middle + pi;
                        const bool opposite = std::cos(
                            cursor_angle - opposite_middle) >
                            std::cos(cursor_angle - candidate_middle);
                        ra = first_angle + (opposite ? pi : 0.0);
                        sweep_radians = candidate_sweep;
                        displayed_value = dimension.value;
                    }
                }
                const double da = ra + sweep_radians;
                result.dimensions.push_back({
                    world_point(vertex[0], vertex[1]),
                    world_point(vertex[0], vertex[1]),
                    world_point(vertex[0] + display_radius * std::cos(ra),
                                vertex[1] + display_radius * std::sin(ra)),
                    world_point(vertex[0] + display_radius * std::cos(da),
                                vertex[1] + display_radius * std::sin(da)),
                    displayed_value, {id, "dimension:" + dimension.id, {}},
                    "∠ ", "°"});
                result.dimensions.back().kind =
                    zima::kernel::ViewerDimensionKind::Angular;
                result.dimensions.back().plane_normal = resolved_normal;
                result.dimensions.back().sweep_degrees =
                    sweep_radians * 180.0 / 3.14159265358979323846;
                if (dimension.placement) {
                    result.dimensions.back().label_position = world_point(
                        (*dimension.placement)[0], (*dimension.placement)[1]);
                }
            }
            continue;
        }
        if (dimension.kind == DimensionKind::AngleThreePoint) {
            const auto* first = find_point(dimension.first_point_id);
            const auto* vertex = find_point(dimension.second_point_id);
            const auto* second = find_point(dimension.geometry_id);
            if (first == nullptr || vertex == nullptr || second == nullptr) continue;
            const double first_angle = std::atan2(
                first->y - vertex->y, first->x - vertex->x);
            const double second_angle = std::atan2(
                second->y - vertex->y, second->x - vertex->x);
            const double default_radius = std::clamp(
                std::min(std::hypot(first->x - vertex->x, first->y - vertex->y),
                         std::hypot(second->x - vertex->x, second->y - vertex->y)) *
                    0.45, 8.0, 30.0);
            const double display_radius = dimension.placement
                ? std::max(1.0, std::hypot(
                    (*dimension.placement)[0] - vertex->x,
                    (*dimension.placement)[1] - vertex->y))
                : default_radius;
            result.dimensions.push_back({
                project(*vertex), project(*vertex),
                world_point(vertex->x + display_radius * std::cos(first_angle),
                            vertex->y + display_radius * std::sin(first_angle)),
                world_point(vertex->x + display_radius * std::cos(second_angle),
                            vertex->y + display_radius * std::sin(second_angle)),
                dimension.value, {id, "dimension:" + dimension.id, {}},
                "∠ ", "°"});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Angular;
            result.dimensions.back().plane_normal = resolved_normal;
            result.dimensions.back().sweep_degrees = dimension.value;
            continue;
        }
        const bool coordinate_axis_dimension =
            has_coordinate_axis_reference(dimension);
        if (coordinate_axis_dimension) {
            const auto* point = find_point(dimension.first_point_id);
            if (point == nullptr) continue;
            const double magnitude = dimension.kind == DimensionKind::DistanceX
                ? std::abs(point->x) : std::abs(point->y);
            const double offset = std::clamp(magnitude * 0.15, 5.0, 25.0);
            if (dimension.kind == DimensionKind::DistanceX) {
                const double line_y = dimension.placement
                    ? (*dimension.placement)[1] : point->y + offset;
                result.dimensions.push_back({
                    world_point(0.0, point->y), world_point(point->x, point->y),
                    world_point(0.0, line_y), world_point(point->x, line_y),
                    dimension.value, {id, "dimension:" + dimension.id, {}}, {}});
            } else {
                const double line_x = dimension.placement
                    ? (*dimension.placement)[0] : point->x + offset;
                result.dimensions.push_back({
                    world_point(point->x, 0.0), world_point(point->x, point->y),
                    world_point(line_x, 0.0), world_point(line_x, point->y),
                    dimension.value, {id, "dimension:" + dimension.id, {}}, {}});
            }
            continue;
        }
        const auto dimension_point = [&](const std::string& point_id)
            -> std::optional<std::array<double, 2>> {
            if (const auto* point = find_point(point_id)) {
                return std::array{point->x, point->y};
            }
            return external_point_position(*this, point_id);
        };
        const auto first = dimension_point(dimension.first_point_id);
        const auto second = dimension_point(dimension.second_point_id);
        if (!first || !second) continue;
        const double dx = (*second)[0] - (*first)[0];
        const double dy = (*second)[1] - (*first)[1];
        const double magnitude = std::hypot(dx, dy);
        const double offset = std::clamp(magnitude * 0.15, 5.0, 25.0);
        if (dimension.kind == DimensionKind::Angle) {
            const double display_radius = dimension.placement
                ? std::max(1.0, std::hypot(
                    (*dimension.placement)[0] - (*first)[0],
                    (*dimension.placement)[1] - (*first)[1]))
                : std::clamp(magnitude * 0.35, 8.0, 30.0);
            const double radians = dimension.value *
                3.14159265358979323846 / 180.0;
            result.dimensions.push_back({
                world_point((*first)[0], (*first)[1]),
                world_point((*first)[0], (*first)[1]),
                world_point((*first)[0] + display_radius, (*first)[1]),
                world_point((*first)[0] + display_radius * std::cos(radians),
                            (*first)[1] + display_radius * std::sin(radians)),
                dimension.value, {id, "dimension:" + dimension.id, {}},
                "∠ ", "°"});
            result.dimensions.back().kind =
                zima::kernel::ViewerDimensionKind::Angular;
            result.dimensions.back().plane_normal = resolved_normal;
            result.dimensions.back().sweep_degrees = dimension.value;
            continue;
        }
        if (dimension.kind == DimensionKind::DistanceX) {
            const double line_y = dimension.placement
                ? (*dimension.placement)[1]
                : std::max((*first)[1], (*second)[1]) + offset;
            result.dimensions.push_back({
                world_point((*first)[0], (*first)[1]),
                world_point((*second)[0], (*second)[1]),
                world_point((*first)[0], line_y), world_point((*second)[0], line_y),
                dimension.value, {id, "dimension:" + dimension.id, {}}, {}});
            continue;
        }
        if (dimension.kind == DimensionKind::DistanceY) {
            const double line_x = dimension.placement
                ? (*dimension.placement)[0]
                : std::max((*first)[0], (*second)[0]) + offset;
            result.dimensions.push_back({
                world_point((*first)[0], (*first)[1]),
                world_point((*second)[0], (*second)[1]),
                world_point(line_x, (*first)[1]), world_point(line_x, (*second)[1]),
                dimension.value, {id, "dimension:" + dimension.id, {}}, {}});
            continue;
        }
        const double nx = magnitude > 1.0e-12 ? -dy / magnitude : 0.0;
        const double ny = magnitude > 1.0e-12 ? dx / magnitude : 1.0;
        const double placed_offset = dimension.placement
            ? ((*dimension.placement)[0] - ((*first)[0] + (*second)[0]) * 0.5) * nx +
              ((*dimension.placement)[1] - ((*first)[1] + (*second)[1]) * 0.5) * ny
            : offset;
        result.dimensions.push_back({
            world_point((*first)[0], (*first)[1]),
            world_point((*second)[0], (*second)[1]),
            world_point((*first)[0] + nx * placed_offset,
                        (*first)[1] + ny * placed_offset),
            world_point((*second)[0] + nx * placed_offset,
                        (*second)[1] + ny * placed_offset),
            dimension.value, {id, "dimension:" + dimension.id, {}}});
    }
    for (auto& rendered : result.dimensions) {
        if (!rendered.reference.semantic_key.starts_with("dimension:")) continue;
        const auto dimension_id = rendered.reference.semantic_key.substr(10);
        const auto dimension = std::find_if(dimensions.begin(), dimensions.end(),
            [&](const auto& value) { return value.id == dimension_id; });
        if (dimension == dimensions.end()) continue;
        rendered.driving = dimension->driving;
        rendered.locked = dimension->locked;
        rendered.label_prefix = dimension->prefix + rendered.label_prefix;
        if (!dimension->suffix.empty()) rendered.unit_suffix = dimension->suffix;
        if (dimension->tolerance_mode == "symmetric" &&
            !dimension->symmetric_tolerance.empty()) {
            rendered.unit_suffix += " ±" + dimension->symmetric_tolerance;
        } else if (dimension->tolerance_mode == "single_deviation" &&
                   !dimension->single_tolerance.empty()) {
            rendered.unit_suffix += " " + dimension->single_tolerance;
        } else if (dimension->tolerance_mode == "deviations") {
            if (!dimension->upper_tolerance.empty()) {
                rendered.unit_suffix += " +" + dimension->upper_tolerance;
            }
            if (!dimension->lower_tolerance.empty()) {
                rendered.unit_suffix += " /-" + dimension->lower_tolerance;
            }
        }
        for (const auto& point_id : {dimension->first_point_id,
                                     dimension->second_point_id}) {
            if (point_id.empty()) continue;
            rendered.participant_semantic_keys.push_back(
                point_id == "sketch_origin" ? "origin:point" : "point:" + point_id);
        }
        if (dimension->kind == DimensionKind::AngleThreePoint &&
            !dimension->geometry_id.empty()) {
            rendered.participant_semantic_keys.push_back(
                "point:" + dimension->geometry_id);
        }
        for (const auto& geometry_id : {
                dimension->kind == DimensionKind::AngleThreePoint
                    ? std::string{} : dimension->geometry_id,
                dimension->second_geometry_id}) {
            if (const auto key = geometry_semantic_key(geometry_id); !key.empty())
                rendered.participant_semantic_keys.push_back(key);
        }
    }
    // R0 intentionally has no evaluated arc, but its owned annotation remains
    // visible and editable at the persisted corner vertex.
    for (const auto& radius : corner_radii) {
        if (radius.suppressed || radius.radius > 1.0e-9 ||
            !radius.dimension_visible) continue;
        const auto* vertex = find_point(radius.vertex_id);
        if (vertex == nullptr) continue;
        const auto center = project(*vertex);
        result.dimensions.push_back({center, center, center, center, 0.0,
            {id, "corner_dimension:" + radius.id, {}}, "R"});
        result.dimensions.back().kind =
            zima::kernel::ViewerDimensionKind::Radius;
        if (radius.dimension_placement) {
            result.dimensions.back().line_second = world_point(
                (*radius.dimension_placement)[0],
                (*radius.dimension_placement)[1]);
        }
    }
    return result;
}

namespace {
// Sketches without a Plane container reference always compute their frame
// live from `plane`/`plane_offset` (never from the persisted resolved_*
// cache) so direct field assignment -- as done by every pre-existing
// caller/test that never touches plane_reference_owner_id -- keeps working
// unchanged, with no explicit refresh_default_frame() call required.
SketchFrame active_sketch_frame(const Sketch& sketch) {
    if (sketch.owner_container_id.empty() &&
        sketch.plane_reference_owner_id.empty()) {
        return default_sketch_frame(sketch.plane, sketch.plane_offset);
    }
    return {sketch.resolved_origin, sketch.resolved_x_axis,
            sketch.resolved_y_axis, sketch.resolved_normal};
}
}  // namespace

zima::kernel::Vec3 Sketch::world_point(double x, double y) const {
    require_finite(x, "sketch x");
    require_finite(y, "sketch y");
    const auto frame = active_sketch_frame(*this);
    return {frame.origin.x + x * frame.x_axis.x + y * frame.y_axis.x,
            frame.origin.y + x * frame.x_axis.y + y * frame.y_axis.y,
            frame.origin.z + x * frame.x_axis.z + y * frame.y_axis.z};
}

std::pair<zima::kernel::Vec3, zima::kernel::Vec3> Sketch::normal_ray(
    double x, double y) const {
    const auto point = world_point(x, y);
    const auto frame = active_sketch_frame(*this);
    return {
        {point.x + frame.normal.x,
         point.y + frame.normal.y,
         point.z + frame.normal.z},
        {-frame.normal.x, -frame.normal.y, -frame.normal.z}};
}

std::array<double, 2> Sketch::local_point(
    const zima::kernel::Vec3& point) const {
    require_finite(point.x, "world x");
    require_finite(point.y, "world y");
    require_finite(point.z, "world z");
    const auto frame = active_sketch_frame(*this);
    const zima::kernel::Vec3 relative{
        point.x - frame.origin.x,
        point.y - frame.origin.y,
        point.z - frame.origin.z};
    return {relative.x * frame.x_axis.x + relative.y * frame.x_axis.y +
                relative.z * frame.x_axis.z,
            relative.x * frame.y_axis.x + relative.y * frame.y_axis.y +
                relative.z * frame.y_axis.z};
}

std::optional<std::array<double, 2>> Sketch::intersect_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    constexpr double epsilon = 1.0e-12;
    const auto frame = active_sketch_frame(*this);
    const auto& normal = frame.normal;
    const double denominator = direction.x * normal.x + direction.y * normal.y +
        direction.z * normal.z;
    if (std::abs(denominator) <= epsilon) return std::nullopt;
    const double numerator =
        (frame.origin.x - origin.x) * normal.x +
        (frame.origin.y - origin.y) * normal.y +
        (frame.origin.z - origin.z) * normal.z;
    const double parameter = numerator / denominator;
    if (!std::isfinite(parameter) || parameter < 0.0) return std::nullopt;
    const zima::kernel::Vec3 point{
        origin.x + direction.x * parameter,
        origin.y + direction.y * parameter,
        origin.z + direction.z * parameter};
    return local_point(point);
}

zima::kernel::Vec3 Sketch::normal() const {
    return active_sketch_frame(*this).normal;
}

zima::kernel::Vec3 Sketch::x_axis() const {
    return active_sketch_frame(*this).x_axis;
}

zima::kernel::Vec3 Sketch::y_axis() const {
    return active_sketch_frame(*this).y_axis;
}

std::string Sketch::serialized() const {
    validate();
    nlohmann::json point_values = nlohmann::json::array();
    for (const auto& point : points) point_values.push_back({
        {"id", point.id}, {"x", point.x}, {"y", point.y},
        {"fixed", point.fixed}, {"construction", point.construction}});
    nlohmann::json segment_values = nlohmann::json::array();
    for (const auto& segment : segments) segment_values.push_back({
        {"id", segment.id}, {"first", segment.first_point_id},
        {"second", segment.second_point_id}, {"construction", segment.construction},
        {"centerline", segment.centerline}});
    nlohmann::json corner_radius_values = nlohmann::json::array();
    for (const auto& radius : corner_radii) corner_radius_values.push_back({
        {"id", radius.id}, {"vertex", radius.vertex_id},
        {"first_segment", radius.first_segment_id},
        {"second_segment", radius.second_segment_id},
        {"radius", radius.radius}, {"suppressed", radius.suppressed},
        {"dimension_visible", radius.dimension_visible},
        {"dimension_placement", radius.dimension_placement
            ? nlohmann::json(*radius.dimension_placement) : nlohmann::json(nullptr)},
        {"equal_radius_group", radius.equal_radius_group}});
    nlohmann::json circle_values = nlohmann::json::array();
    for (const auto& circle : circles) circle_values.push_back({
        {"id", circle.id}, {"center", circle.center_point_id},
        {"radius", circle.radius}, {"construction", circle.construction}});
    nlohmann::json arc_values = nlohmann::json::array();
    for (const auto& arc : arcs) arc_values.push_back({
        {"id", arc.id}, {"center", arc.center_point_id},
        {"start", arc.start_point_id}, {"end", arc.end_point_id},
        {"radius", arc.radius},
        {"start_angle", arc.start_angle}, {"end_angle", arc.end_angle},
        {"construction", arc.construction}});
    nlohmann::json ellipse_values = nlohmann::json::array();
    for (const auto& ellipse : ellipses) ellipse_values.push_back({
        {"id", ellipse.id}, {"center", ellipse.center_point_id},
        {"major_point", ellipse.major_point_id},
        {"minor_point", ellipse.minor_point_id},
        {"major_radius", ellipse.major_radius},
        {"minor_radius", ellipse.minor_radius}, {"rotation", ellipse.rotation},
        {"construction", ellipse.construction}, {"reversed", ellipse.reversed}});
    nlohmann::json elliptical_arc_values = nlohmann::json::array();
    for (const auto& arc : elliptical_arcs) elliptical_arc_values.push_back({
        {"id", arc.id}, {"center", arc.center_point_id},
        {"major_point", arc.major_point_id},
        {"minor_point", arc.minor_point_id},
        {"start", arc.start_point_id}, {"end", arc.end_point_id},
        {"major_radius", arc.major_radius},
        {"minor_radius", arc.minor_radius}, {"rotation", arc.rotation},
        {"start_parameter", arc.start_parameter},
        {"end_parameter", arc.end_parameter},
        {"construction", arc.construction}, {"reversed", arc.reversed}});
    nlohmann::json spline_values = nlohmann::json::array();
    for (const auto& spline : bsplines) spline_values.push_back({
        {"id", spline.id}, {"control_points", spline.control_point_ids},
        {"degree", spline.degree}, {"interpolating", spline.interpolating},
        {"closed", spline.closed},
        {"construction", spline.construction}});
    nlohmann::json import_block_values = nlohmann::json::array();
    for (const auto& block : import_blocks) import_block_values.push_back({
        {"id", block.id}, {"name", block.name}, {"source_path", block.source_path},
        {"geometry_ids", block.geometry_ids}, {"point_ids", block.point_ids},
        {"translation_x", block.translation_x},
        {"translation_y", block.translation_y}, {"rotation", block.rotation}});
    nlohmann::json text_values = nlohmann::json::array();
    for (const auto& text : texts) {
        nlohmann::json contours = nlohmann::json::array();
        for (const auto& contour : text.contours) {
            nlohmann::json points = nlohmann::json::array();
            for (const auto& point : contour) points.push_back({point[0], point[1]});
            contours.push_back(std::move(points));
        }
        text_values.push_back({
            {"id", text.id}, {"value", text.value},
            {"anchor_x", text.anchor_x}, {"anchor_y", text.anchor_y},
            {"height", text.height},
            {"horizontal", text_horizontal_name(text.horizontal)},
            {"vertical", text_vertical_name(text.vertical)},
            {"angle_degrees", text.angle_degrees}, {"flipped", text.flipped},
            {"color", text_color_name(text.color)}, {"font", text.font},
            {"contours", std::move(contours)}});
    }
    nlohmann::json external_reference_values = nlohmann::json::array();
    for (const auto& reference : external_references) {
        nlohmann::json points = nlohmann::json::array();
        for (const auto& point : reference.cached_points) {
            points.push_back({point[0], point[1]});
        }
        nlohmann::json paths = nlohmann::json::array();
        for (const auto& path : reference.cached_paths) {
            nlohmann::json path_points = nlohmann::json::array();
            for (const auto& point : path) {
                path_points.push_back({point[0], point[1]});
            }
            paths.push_back(std::move(path_points));
        }
        external_reference_values.push_back({
            {"id", reference.id},
            {"kind", external_reference_kind_name(reference.kind)},
            {"source_document_id", reference.source_document_id},
            {"source_owner_id", reference.source_owner_id},
            {"source_semantic_key", reference.source_semantic_key},
            {"source_instance_path", reference.source_instance_path},
            {"context_assembly_document_id",
             reference.context_assembly_document_id},
            {"context_instance_path", reference.context_instance_path},
            {"cached_points", std::move(points)},
            {"cached_paths", std::move(paths)},
            {"infinite", reference.infinite},
            {"broken", reference.broken}});
    }
    nlohmann::json constraint_values = nlohmann::json::array();
    for (const auto& constraint : constraints) constraint_values.push_back({
        {"id", constraint.id}, {"kind", constraint_name(constraint.kind)},
        {"first", constraint.first_point_id}, {"second", constraint.second_point_id},
        {"suppressed", constraint.suppressed}, {"geometry", constraint.geometry_id},
        {"second_geometry", constraint.second_geometry_id},
        {"tangent_internal", constraint.tangent_internal}});
    nlohmann::json dimension_values = nlohmann::json::array();
    for (const auto& dimension : dimensions) {
        nlohmann::json value{{"id", dimension.id}, {"kind", dimension_name(dimension.kind)},
            {"first", dimension.first_point_id}, {"second", dimension.second_point_id},
            {"value", dimension.value}, {"driving", dimension.driving},
            {"suppressed", dimension.suppressed}, {"geometry", dimension.geometry_id},
            {"second_geometry", dimension.second_geometry_id},
            {"angle_sector", dimension.angle_sector},
            {"angle_presentation_reversed",
             dimension.angle_presentation_reversed}};
        if (dimension.placement) value["placement"] = *dimension.placement;
        if (dimension.lower_limit) value["lower_limit"] = *dimension.lower_limit;
        if (dimension.upper_limit) value["upper_limit"] = *dimension.upper_limit;
        value["prefix"] = dimension.prefix;
        value["suffix"] = dimension.suffix;
        value["tolerance_mode"] = dimension.tolerance_mode;
        value["symmetric_tolerance"] = dimension.symmetric_tolerance;
        value["single_tolerance"] = dimension.single_tolerance;
        value["upper_tolerance"] = dimension.upper_tolerance;
        value["lower_tolerance"] = dimension.lower_tolerance;
        value["locked"] = dimension.locked;
        dimension_values.push_back(std::move(value));
    }
    const nlohmann::json root{{"format", "zima-cad-cpp-sketch"}, {"version", 30},
        {"id", id}, {"owner_container_id", owner_container_id},
        {"name", name}, {"suppressed", suppressed},
        {"plane", plane_name(plane)},
        {"plane_offset", plane_offset},
        {"plane_reference_owner_id", plane_reference_owner_id},
        {"resolved_origin", {resolved_origin.x, resolved_origin.y, resolved_origin.z}},
        {"resolved_x_axis", {resolved_x_axis.x, resolved_x_axis.y, resolved_x_axis.z}},
        {"resolved_y_axis", {resolved_y_axis.x, resolved_y_axis.y, resolved_y_axis.z}},
        {"resolved_normal", {resolved_normal.x, resolved_normal.y, resolved_normal.z}},
        {"points", std::move(point_values)},
        {"segments", std::move(segment_values)},
        {"corner_radii", std::move(corner_radius_values)},
        {"circles", std::move(circle_values)},
        {"arcs", std::move(arc_values)},
        {"ellipses", std::move(ellipse_values)},
        {"elliptical_arcs", std::move(elliptical_arc_values)},
        {"bsplines", std::move(spline_values)},
        {"import_blocks", std::move(import_block_values)},
        {"texts", std::move(text_values)},
        {"external_references", std::move(external_reference_values)},
        {"constraints", std::move(constraint_values)},
        {"dimensions", std::move(dimension_values)}};
    return root.dump(2);
}

Sketch Sketch::from_serialized(const std::string& value) {
    const auto root = nlohmann::json::parse(value);
    if (root.at("format") != "zima-cad-cpp-sketch" || root.at("version") != 30) {
        throw std::runtime_error("Unsupported sketch format");
    }
    Sketch sketch;
    sketch.id = root.at("id").get<std::string>();
    sketch.owner_container_id = root.at("owner_container_id").get<std::string>();
    sketch.name = root.at("name").get<std::string>();
    sketch.suppressed = root.at("suppressed").get<bool>();
    sketch.plane = plane_from_name(root.at("plane").get<std::string>());
    sketch.plane_offset = root.at("plane_offset").get<double>();
    sketch.plane_reference_owner_id =
        root.at("plane_reference_owner_id").get<std::string>();
    const auto read_vec3 = [](const nlohmann::json& array) {
        return zima::kernel::Vec3{array.at(0).get<double>(),
            array.at(1).get<double>(), array.at(2).get<double>()};
    };
    sketch.resolved_origin = read_vec3(root.at("resolved_origin"));
    sketch.resolved_x_axis = read_vec3(root.at("resolved_x_axis"));
    sketch.resolved_y_axis = read_vec3(root.at("resolved_y_axis"));
    sketch.resolved_normal = read_vec3(root.at("resolved_normal"));
    for (const auto& value : root.at("points")) sketch.points.push_back({
        value.at("id").get<std::string>(), value.at("x").get<double>(),
        value.at("y").get<double>(), value.at("fixed").get<bool>(),
        value.at("construction").get<bool>()});
    for (const auto& value : root.at("segments")) sketch.segments.push_back({
        value.at("id").get<std::string>(), value.at("first").get<std::string>(),
        value.at("second").get<std::string>(), value.at("construction").get<bool>(),
        value.at("centerline").get<bool>()});
    for (const auto& value : root.at("corner_radii")) sketch.corner_radii.push_back({
        value.at("id").get<std::string>(),
        value.at("vertex").get<std::string>(),
        value.at("first_segment").get<std::string>(),
        value.at("second_segment").get<std::string>(),
        value.at("radius").get<double>(), value.at("suppressed").get<bool>(),
        value.at("dimension_visible").get<bool>(),
        value.at("dimension_placement").is_null()
            ? std::optional<std::array<double, 2>>{}
            : std::optional{value.at("dimension_placement")
                  .get<std::array<double, 2>>()},
        value.at("equal_radius_group").get<std::string>()});
    for (const auto& value : root.at("circles")) sketch.circles.push_back({
        value.at("id").get<std::string>(), value.at("center").get<std::string>(),
        value.at("radius").get<double>(), value.at("construction").get<bool>()});
    for (const auto& value : root.at("arcs")) sketch.arcs.push_back({
        value.at("id").get<std::string>(), value.at("center").get<std::string>(),
        value.at("start").get<std::string>(), value.at("end").get<std::string>(),
        value.at("radius").get<double>(), value.at("start_angle").get<double>(),
        value.at("end_angle").get<double>(), value.at("construction").get<bool>()});
    for (const auto& value : root.at("ellipses")) sketch.ellipses.push_back({
        value.at("id").get<std::string>(), value.at("center").get<std::string>(),
        value.at("major_point").get<std::string>(),
        value.at("minor_point").get<std::string>(),
        value.at("major_radius").get<double>(),
        value.at("minor_radius").get<double>(), value.at("rotation").get<double>(),
        value.at("construction").get<bool>(), value.at("reversed").get<bool>()});
    for (const auto& value : root.at("elliptical_arcs")) {
        sketch.elliptical_arcs.push_back({
            value.at("id").get<std::string>(),
            value.at("center").get<std::string>(),
            value.at("major_point").get<std::string>(),
            value.at("minor_point").get<std::string>(),
            value.at("start").get<std::string>(),
            value.at("end").get<std::string>(),
            value.at("major_radius").get<double>(),
            value.at("minor_radius").get<double>(),
            value.at("rotation").get<double>(),
            value.at("start_parameter").get<double>(),
            value.at("end_parameter").get<double>(),
            value.at("construction").get<bool>(),
            value.at("reversed").get<bool>()});
    }
    for (const auto& value : root.at("bsplines")) sketch.bsplines.push_back({
        value.at("id").get<std::string>(),
        value.at("control_points").get<std::vector<std::string>>(),
        value.at("degree").get<unsigned>(),
        value.at("interpolating").get<bool>(), value.at("closed").get<bool>(),
        value.at("construction").get<bool>()});
    for (const auto& value : root.at("import_blocks")) sketch.import_blocks.push_back({
        value.at("id").get<std::string>(), value.at("name").get<std::string>(),
        value.at("source_path").get<std::string>(),
        value.at("geometry_ids").get<std::vector<std::string>>(),
        value.at("point_ids").get<std::vector<std::string>>(),
        value.at("translation_x").get<double>(),
        value.at("translation_y").get<double>(), value.at("rotation").get<double>()});
    for (const auto& value : root.at("texts")) {
        SketchText text;
        text.id = value.at("id").get<std::string>();
        text.value = value.at("value").get<std::string>();
        text.anchor_x = value.at("anchor_x").get<double>();
        text.anchor_y = value.at("anchor_y").get<double>();
        text.height = value.at("height").get<double>();
        text.horizontal = text_horizontal_from_name(
            value.at("horizontal").get<std::string>());
        text.vertical = text_vertical_from_name(
            value.at("vertical").get<std::string>());
        text.angle_degrees = value.at("angle_degrees").get<double>();
        text.flipped = value.at("flipped").get<bool>();
        text.color = text_color_from_name(value.at("color").get<std::string>());
        text.font = value.at("font").get<std::string>();
        for (const auto& contour_value : value.at("contours")) {
            std::vector<std::array<double, 2>> contour;
            for (const auto& point : contour_value) {
                contour.push_back({point.at(0).get<double>(),
                                   point.at(1).get<double>()});
            }
            text.contours.push_back(std::move(contour));
        }
        sketch.texts.push_back(std::move(text));
    }
    for (const auto& value : root.at("external_references")) {
        SketchExternalReference reference;
        reference.id = value.at("id").get<std::string>();
        reference.kind = external_reference_kind_from_name(
            value.at("kind").get<std::string>());
        reference.source_document_id =
            value.at("source_document_id").get<std::string>();
        reference.source_owner_id =
            value.at("source_owner_id").get<std::string>();
        reference.source_semantic_key =
            value.at("source_semantic_key").get<std::string>();
        reference.source_instance_path =
            value.at("source_instance_path").get<std::string>();
        reference.context_assembly_document_id =
            value.at("context_assembly_document_id").get<std::string>();
        reference.context_instance_path =
            value.at("context_instance_path").get<std::string>();
        for (const auto& point : value.at("cached_points")) {
            reference.cached_points.push_back({
                point.at(0).get<double>(), point.at(1).get<double>()});
        }
        for (const auto& path_value : value.at("cached_paths")) {
            std::vector<std::array<double, 2>> path;
            for (const auto& point : path_value) {
                path.push_back({point.at(0).get<double>(),
                                point.at(1).get<double>()});
            }
            reference.cached_paths.push_back(std::move(path));
        }
        reference.infinite = value.value("infinite", false);
        reference.broken = value.at("broken").get<bool>();
        sketch.external_references.push_back(std::move(reference));
    }
    for (const auto& value : root.at("constraints")) sketch.constraints.push_back({
        value.at("id").get<std::string>(), constraint_from_name(value.at("kind")),
        value.at("first").get<std::string>(), value.at("second").get<std::string>(),
        value.at("suppressed").get<bool>(), value.at("geometry").get<std::string>(),
        value.at("second_geometry").get<std::string>(),
        value.at("tangent_internal").get<bool>()});
    for (const auto& value : root.at("dimensions")) {
        SketchDimension dimension{value.at("id").get<std::string>(),
            dimension_from_name(value.at("kind")), value.at("first").get<std::string>(),
            value.at("second").get<std::string>(), value.at("value").get<double>(),
            value.at("driving").get<bool>(), value.at("suppressed").get<bool>()};
        dimension.geometry_id = value.at("geometry").get<std::string>();
        dimension.second_geometry_id =
            value.at("second_geometry").get<std::string>();
        dimension.angle_sector = value.value("angle_sector", -1);
        dimension.angle_presentation_reversed =
            value.value("angle_presentation_reversed", false);
        if (value.contains("placement")) {
            dimension.placement = value.at("placement")
                .get<std::array<double, 2>>();
        }
        if (value.contains("lower_limit")) dimension.lower_limit = value.at("lower_limit").get<double>();
        if (value.contains("upper_limit")) dimension.upper_limit = value.at("upper_limit").get<double>();
        dimension.prefix = value.value("prefix", std::string{});
        dimension.suffix = value.value("suffix", std::string{});
        dimension.tolerance_mode = value.value("tolerance_mode", std::string{});
        dimension.symmetric_tolerance =
            value.value("symmetric_tolerance", std::string{});
        dimension.single_tolerance =
            value.value("single_tolerance", std::string{});
        dimension.upper_tolerance =
            value.value("upper_tolerance", std::string{});
        dimension.lower_tolerance =
            value.value("lower_tolerance", std::string{});
        dimension.locked = value.at("locked").get<bool>();
        sketch.dimensions.push_back(std::move(dimension));
    }
    sketch.validate();
    return sketch;
}

void Sketch::save(const std::filesystem::path& path) const {
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot write sketch");
        output << serialized() << '\n';
        if (!output) throw std::runtime_error("Sketch write failed");
    }
    std::filesystem::rename(temporary, path);
}

Sketch Sketch::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open sketch");
    std::ostringstream value;
    value << input.rdbuf();
    return from_serialized(value.str());
}

}  // namespace zima::sketcher
