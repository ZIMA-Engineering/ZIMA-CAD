#include <zima/sketcher/sketch.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace zima::sketcher {
namespace {

std::string make_id() {
    std::mt19937_64 generator(static_cast<std::mt19937_64::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
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

const char* constraint_name(ConstraintKind kind) {
    switch (kind) {
    case ConstraintKind::Horizontal: return "horizontal";
    case ConstraintKind::Vertical: return "vertical";
    case ConstraintKind::Coincident: return "coincident";
    case ConstraintKind::Parallel: return "parallel";
    case ConstraintKind::Perpendicular: return "perpendicular";
    case ConstraintKind::EqualLength: return "equal_length";
    case ConstraintKind::PointOnCircle: return "point_on_circle";
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
    if (name == "point_on_circle") return ConstraintKind::PointOnCircle;
    throw std::runtime_error("Unknown sketch constraint");
}

const char* dimension_name(DimensionKind kind) {
    switch (kind) {
    case DimensionKind::Distance: return "distance";
    case DimensionKind::DistanceX: return "distance_x";
    case DimensionKind::DistanceY: return "distance_y";
    case DimensionKind::Radius: return "radius";
    case DimensionKind::Diameter: return "diameter";
    case DimensionKind::Angle: return "angle";
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
    if (name == "radius") return DimensionKind::Radius;
    if (name == "diameter") return DimensionKind::Diameter;
    if (name == "angle") return DimensionKind::Angle;
    if (name == "ellipse_major_radius") return DimensionKind::EllipseMajorRadius;
    if (name == "ellipse_minor_radius") return DimensionKind::EllipseMinorRadius;
    if (name == "ellipse_rotation") return DimensionKind::EllipseRotation;
    throw std::runtime_error("Unknown sketch dimension");
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) throw std::runtime_error(std::string(field) + " must be finite");
}

double wrapped_degrees(double value) {
    while (value > 180.0) value -= 360.0;
    while (value < -180.0) value += 360.0;
    return value;
}

bool is_segment_pair_constraint(ConstraintKind kind) {
    return kind == ConstraintKind::Parallel ||
        kind == ConstraintKind::Perpendicular ||
        kind == ConstraintKind::EqualLength;
}

}  // namespace

Sketch Sketch::create_default() {
    Sketch sketch;
    sketch.id = make_id();
    return sketch;
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
    const auto found = std::find_if(points.begin(), points.end(),
        [&](const auto& point) { return point.id == point_id; });
    return found == points.end() ? nullptr : &*found;
}

const SketchPoint* Sketch::find_point(const std::string& point_id) const {
    return const_cast<Sketch*>(this)->find_point(point_id);
}

void Sketch::validate() const {
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
        if (segment.id.empty() || !ids.insert(segment.id).second ||
            segment.first_point_id == segment.second_point_id ||
            find_point(segment.first_point_id) == nullptr ||
            find_point(segment.second_point_id) == nullptr) {
            throw std::runtime_error("Sketch segment is invalid");
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
                minor->x - (center->x - ellipse.minor_radius * std::sin(ellipse.rotation)),
                minor->y - (center->y + ellipse.minor_radius * std::cos(ellipse.rotation))) >
                1.0e-7) {
            throw std::runtime_error("Sketch ellipse axis references are inconsistent");
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
    for (const auto& constraint : constraints) {
        const auto owned_segment = std::find_if(
            segments.begin(), segments.end(), [&](const auto& segment) {
                return segment.id == constraint.geometry_id;
            });
        const auto second_owned_segment = std::find_if(
            segments.begin(), segments.end(), [&](const auto& segment) {
                return segment.id == constraint.second_geometry_id;
            });
        const auto owned_circle = std::find_if(
            circles.begin(), circles.end(), [&](const auto& circle) {
                return circle.id == constraint.geometry_id;
            });
        const bool pair_constraint = is_segment_pair_constraint(constraint.kind);
        const bool segment_constraint = constraint.kind == ConstraintKind::Horizontal ||
            constraint.kind == ConstraintKind::Vertical;
        const bool point_on_circle =
            constraint.kind == ConstraintKind::PointOnCircle;
        const bool points_valid = pair_constraint
            ? constraint.first_point_id.empty() && constraint.second_point_id.empty()
            : point_on_circle
                ? find_point(constraint.first_point_id) != nullptr &&
                  constraint.second_point_id.empty()
            : find_point(constraint.first_point_id) != nullptr &&
              find_point(constraint.second_point_id) != nullptr;
        if (constraint.id.empty() || !ids.insert(constraint.id).second || !points_valid ||
            (segment_constraint && (owned_segment == segments.end() ||
             owned_segment->first_point_id != constraint.first_point_id ||
             owned_segment->second_point_id != constraint.second_point_id)) ||
            (constraint.kind == ConstraintKind::Coincident &&
             (!constraint.geometry_id.empty() || !constraint.second_geometry_id.empty())) ||
            (point_on_circle && (owned_circle == circles.end() ||
             owned_circle->center_point_id == constraint.first_point_id ||
             !constraint.second_geometry_id.empty())) ||
            (segment_constraint && !constraint.second_geometry_id.empty()) ||
            (pair_constraint && (owned_segment == segments.end() ||
             second_owned_segment == segments.end() ||
             constraint.geometry_id == constraint.second_geometry_id))) {
            throw std::runtime_error("Sketch constraint is invalid");
        }
    }
    ids.clear();
    for (const auto& dimension : dimensions) {
        const bool radial_dimension = dimension.kind == DimensionKind::Radius ||
            dimension.kind == DimensionKind::Diameter;
        const bool ellipse_dimension =
            dimension.kind == DimensionKind::EllipseMajorRadius ||
            dimension.kind == DimensionKind::EllipseMinorRadius ||
            dimension.kind == DimensionKind::EllipseRotation;
        const bool circle_geometry = std::any_of(
            circles.begin(), circles.end(), [&](const auto& circle) {
                return circle.id == dimension.geometry_id;
            });
        const bool geometry_valid = radial_dimension &&
            (circle_geometry || (dimension.kind == DimensionKind::Radius &&
             std::any_of(arcs.begin(), arcs.end(), [&](const auto& arc) {
                return arc.id == dimension.geometry_id;
             })));
        const bool ellipse_geometry_valid = ellipse_dimension && std::any_of(
            ellipses.begin(), ellipses.end(), [&](const auto& ellipse) {
                return ellipse.id == dimension.geometry_id;
            });
        const auto owned_segment = std::find_if(
            segments.begin(), segments.end(), [&](const auto& segment) {
                return segment.id == dimension.geometry_id;
            });
        const bool segment_geometry_valid = !radial_dimension && !ellipse_dimension &&
            owned_segment != segments.end() &&
            owned_segment->first_point_id == dimension.first_point_id &&
            owned_segment->second_point_id == dimension.second_point_id;
        if (dimension.id.empty() || !ids.insert(dimension.id).second ||
            (radial_dimension ? !geometry_valid
                : ellipse_dimension ? !ellipse_geometry_valid
                : !segment_geometry_valid)) {
            throw std::runtime_error("Sketch dimension is invalid");
        }
        require_finite(dimension.value, "dimension value");
        if ((dimension.kind == DimensionKind::Distance || radial_dimension ||
             dimension.kind == DimensionKind::EllipseMajorRadius ||
             dimension.kind == DimensionKind::EllipseMinorRadius) &&
            dimension.value < 0.0) {
            throw std::runtime_error("Distance must not be negative");
        }
        if ((dimension.kind == DimensionKind::Angle ||
             dimension.kind == DimensionKind::EllipseRotation) &&
            (dimension.value < -180.0 || dimension.value > 180.0)) {
            throw std::runtime_error("Angle must lie between -180 and 180 degrees");
        }
        if ((dimension.kind == DimensionKind::Angle ||
             dimension.kind == DimensionKind::EllipseRotation) &&
            ((dimension.lower_limit && (*dimension.lower_limit < -180.0 ||
                                        *dimension.lower_limit > 180.0)) ||
             (dimension.upper_limit && (*dimension.upper_limit < -180.0 ||
                                        *dimension.upper_limit > 180.0)))) {
            throw std::runtime_error("Angle limits must lie between -180 and 180 degrees");
        }
        if (dimension.lower_limit) require_finite(*dimension.lower_limit, "lower limit");
        if (dimension.upper_limit) require_finite(*dimension.upper_limit, "upper limit");
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
          found->kind == DimensionKind::Radius ||
          found->kind == DimensionKind::Diameter ||
          found->kind == DimensionKind::EllipseMajorRadius ||
          found->kind == DimensionKind::EllipseMinorRadius) && value < 0.0) ||
        ((found->kind == DimensionKind::Angle ||
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

void Sketch::set_point_fixed(const std::string& point_id, bool fixed) {
    auto next = *this;
    auto* point = next.find_point(point_id);
    if (point == nullptr) throw std::invalid_argument("Sketch point does not exist");
    point->fixed = fixed;
    next.validate();
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Point fixation conflicts with existing geometry");
    }
    *this = std::move(next);
}

bool Sketch::move_point(const std::string& point_id, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    auto next = *this;
    auto* point = next.find_point(point_id);
    if (point == nullptr || point->fixed) return false;
    const double original_x = point->x;
    const double original_y = point->y;
    point->x = x;
    point->y = y;
    const double translation_x = x - original_x;
    const double translation_y = y - original_y;
    std::set<std::string> translated_circle_points;
    for (const auto& circle : next.circles) {
        if (circle.center_point_id != point_id) continue;
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
        if (circle == next.circles.end()) return false;
        const auto* center = next.find_point(circle->center_point_id);
        const double requested_radius = std::hypot(x - center->x, y - center->y);
        if (requested_radius <= 1.0e-12) return false;
        const bool driven = std::any_of(
            next.dimensions.begin(), next.dimensions.end(), [&](const auto& dimension) {
                return !dimension.suppressed && dimension.driving &&
                    (dimension.kind == DimensionKind::Radius ||
                     dimension.kind == DimensionKind::Diameter) &&
                    dimension.geometry_id == circle->id;
            });
        if (!driven) circle->radius = requested_radius;
    }
    constexpr double full_turn = 2.0 * 3.14159265358979323846;
    const auto driving_radius = [&](const SketchArc& arc) {
        return std::find_if(next.dimensions.begin(), next.dimensions.end(),
            [&](const auto& dimension) {
                return !dimension.suppressed && dimension.driving &&
                    dimension.kind == DimensionKind::Radius &&
                    dimension.geometry_id == arc.id;
            });
    };
    for (auto& arc : next.arcs) {
        auto* center = next.find_point(arc.center_point_id);
        auto* start = next.find_point(arc.start_point_id);
        auto* end = next.find_point(arc.end_point_id);
        if (arc.center_point_id == point_id) {
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
            continue;
        }
        const bool moving_start = arc.start_point_id == point_id;
        const bool moving_end = arc.end_point_id == point_id;
        if (!moving_start && !moving_end) continue;
        double angle = std::atan2(y - center->y, x - center->x);
        const auto driver = driving_radius(arc);
        const double requested_radius = std::hypot(x - center->x, y - center->y);
        if (requested_radius <= 1.0e-12) return false;
        const double radius = driver == next.dimensions.end()
            ? requested_radius : driver->value;
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
    }
    for (auto& ellipse : next.ellipses) {
        auto* center = next.find_point(ellipse.center_point_id);
        auto* major = next.find_point(ellipse.major_point_id);
        auto* minor = next.find_point(ellipse.minor_point_id);
        if (ellipse.center_point_id == point_id) {
            const double dx = x - original_x;
            const double dy = y - original_y;
            if ((major->fixed || minor->fixed) &&
                (std::abs(dx) > 1.0e-12 || std::abs(dy) > 1.0e-12)) return false;
            major->x += dx;
            major->y += dy;
            minor->x += dx;
            minor->y += dy;
        } else if (ellipse.major_point_id == point_id) {
            const double radius = std::hypot(x - center->x, y - center->y);
            if (radius <= 1.0e-12 || minor->fixed) return false;
            ellipse.major_radius = radius;
            ellipse.rotation = std::atan2(y - center->y, x - center->x);
            minor->x = center->x - ellipse.minor_radius * std::sin(ellipse.rotation);
            minor->y = center->y + ellipse.minor_radius * std::cos(ellipse.rotation);
        } else if (ellipse.minor_point_id == point_id) {
            const double projected =
                -(x - center->x) * std::sin(ellipse.rotation) +
                 (y - center->y) * std::cos(ellipse.rotation);
            if (std::abs(projected) <= 1.0e-12) return false;
            ellipse.minor_radius = std::abs(projected);
            minor->x = center->x - ellipse.minor_radius * std::sin(ellipse.rotation);
            minor->y = center->y + ellipse.minor_radius * std::cos(ellipse.rotation);
        }
    }
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Conflicting || solved.status == SolveStatus::Invalid) {
        return false;
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
                if (dimension.kind == DimensionKind::Diameter) return false;
                const auto arc = std::find_if(next.arcs.begin(), next.arcs.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                measured = arc->radius;
            }
        } else {
            const auto* first = next.find_point(dimension.first_point_id);
            const auto* second = next.find_point(dimension.second_point_id);
            const double dx = second->x - first->x;
            const double dy = second->y - first->y;
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
    auto point = create_point(x, y);
    point.construction = construction;
    const auto point_id = point.id;
    next.points.push_back(std::move(point));
    next.validate();
    *this = std::move(next);
    return point_id;
}

std::string Sketch::add_segment(
    double first_x, double first_y, double second_x, double second_y,
    double snap_tolerance, bool construction) {
    for (const double value : {first_x, first_y, second_x, second_y, snap_tolerance}) {
        require_finite(value, "segment coordinate");
    }
    if (snap_tolerance < 0.0 ||
        std::hypot(second_x - first_x, second_y - first_y) <= 1.0e-12) {
        throw std::invalid_argument("Sketch segment length or snap tolerance is invalid");
    }
    auto next = *this;
    const auto endpoint = [&](double x, double y) {
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
    const auto first_id = endpoint(first_x, first_y);
    const auto second_id = endpoint(second_x, second_y);
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
    *this = std::move(next);
    return id;
}

std::string Sketch::add_coincident_constraint(
    const std::string& first_point_id, const std::string& second_point_id) {
    if (first_point_id.empty() || second_point_id.empty() ||
        first_point_id == second_point_id || find_point(first_point_id) == nullptr ||
        find_point(second_point_id) == nullptr) {
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
    if (first == segments.end() || second == segments.end()) {
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
    *this = std::move(next);
    return id;
}

std::string Sketch::add_point_on_circle_constraint(
    const std::string& point_id, const std::string& circle_id) {
    const auto* point = find_point(point_id);
    const auto circle = std::find_if(circles.begin(), circles.end(),
        [&](const auto& value) { return value.id == circle_id; });
    if (point == nullptr || circle == circles.end() ||
        circle->center_point_id == point_id) {
        throw std::invalid_argument("Point-on-circle constraint input is invalid");
    }
    if (std::any_of(constraints.begin(), constraints.end(), [&](const auto& constraint) {
            return !constraint.suppressed &&
                constraint.kind == ConstraintKind::PointOnCircle &&
                constraint.first_point_id == point_id &&
                constraint.geometry_id == circle_id;
        })) {
        throw std::invalid_argument("Point already lies on this circle by constraint");
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
        throw std::runtime_error("Point-on-circle constraint conflicts with existing geometry");
    }
    *this = std::move(next);
    return id;
}

void Sketch::remove_geometry(const std::string& geometry_id) {
    if (geometry_id.empty()) throw std::invalid_argument("Geometry ID is required");
    auto next = *this;
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
    const auto spline_count = std::erase_if(next.bsplines,
        [&](const auto& value) { return value.id == geometry_id; });
    if (segment_count + circle_count + arc_count + ellipse_count + spline_count != 1) {
        throw std::invalid_argument("Sketch geometry does not exist");
    }
    std::erase_if(next.constraints,
        [&](const auto& value) {
            return value.geometry_id == geometry_id ||
                value.second_geometry_id == geometry_id;
        });
    std::erase_if(next.dimensions,
        [&](const auto& value) { return value.geometry_id == geometry_id; });
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
    std::erase_if(next.bsplines,
        [&](const auto& value) { return geometry_removed(value.id); });
    std::erase_if(next.constraints, [&](const auto& value) {
        return value.first_point_id == point_id || value.second_point_id == point_id ||
            geometry_removed(value.geometry_id) ||
            geometry_removed(value.second_geometry_id);
    });
    std::erase_if(next.dimensions, [&](const auto& value) {
        return value.first_point_id == point_id || value.second_point_id == point_id ||
            geometry_removed(value.geometry_id);
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
    static_cast<void>(next.add_segment_constraint(ids[0], ConstraintKind::Horizontal));
    static_cast<void>(next.add_segment_constraint(ids[1], ConstraintKind::Vertical));
    static_cast<void>(next.add_segment_constraint(ids[2], ConstraintKind::Horizontal));
    static_cast<void>(next.add_segment_constraint(ids[3], ConstraintKind::Vertical));
    next.validate();
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
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_arc(
    double center_x, double center_y, double start_x, double start_y,
    double end_x, double end_y, bool construction, double snap_tolerance) {
    for (const double value : {center_x, center_y, start_x, start_y,
                               end_x, end_y, snap_tolerance}) {
        require_finite(value, "arc parameter");
    }
    const double radius = std::hypot(start_x - center_x, start_y - center_y);
    if (radius <= 1.0e-12 || snap_tolerance < 0.0 ||
        std::hypot(end_x - center_x, end_y - center_y) <= 1.0e-12) {
        throw std::invalid_argument("Sketch arc points or snap tolerance are invalid");
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
    next.validate();
    *this = std::move(next);
    return id;
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
    next.validate();
    *this = std::move(next);
    return id;
}

std::string Sketch::add_bspline(
    const std::vector<std::array<double, 2>>& control_points,
    unsigned degree, bool closed, bool construction, double snap_tolerance) {
    if (degree < 1 || control_points.size() < static_cast<std::size_t>(degree) + 1 ||
        !std::isfinite(snap_tolerance) || snap_tolerance < 0.0) {
        throw std::invalid_argument("Sketch B-spline degree or control points are invalid");
    }
    auto next = *this;
    SketchBSpline spline;
    spline.id = make_id();
    spline.degree = degree;
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
    result.geometry_id = segment->id;
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
    if (arc == arcs.end()) throw std::invalid_argument("Sketch arc does not exist");
    SketchDimension result;
    result.id = make_id();
    result.kind = DimensionKind::Radius;
    result.value = arc->radius;
    result.geometry_id = arc->id;
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
    const auto dimension_kind = dimension.kind;
    const auto dimension_geometry_id = dimension.geometry_id;
    const double dimension_value = dimension.value;
    auto next = *this;
    const auto existing = std::find_if(next.dimensions.begin(), next.dimensions.end(),
        [&](const auto& value) { return value.id == dimension.id; });
    if (existing == next.dimensions.end()) {
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
                        : value.first_point_id == dimension.first_point_id &&
                          value.second_point_id == dimension.second_point_id);
            });
        if (same_driver) throw std::invalid_argument("Segment already owns this driving dimension");
        next.dimensions.push_back(std::move(dimension));
    } else {
        *existing = std::move(dimension);
    }
    if (dimension_kind == DimensionKind::Radius ||
        dimension_kind == DimensionKind::Diameter) {
        const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) { return value.id == dimension_geometry_id; });
        if (circle != next.circles.end()) {
            circle->radius = dimension_kind == DimensionKind::Diameter
                ? dimension_value * 0.5 : dimension_value;
        } else {
            if (dimension_kind == DimensionKind::Diameter) {
                throw std::invalid_argument("Diameter dimension circle does not exist");
            }
            const auto arc = std::find_if(next.arcs.begin(), next.arcs.end(),
                [&](const auto& value) { return value.id == dimension_geometry_id; });
            if (arc == next.arcs.end()) {
                throw std::invalid_argument("Radius dimension geometry does not exist");
            }
            arc->radius = dimension_value;
            const auto* center = next.find_point(arc->center_point_id);
            auto* start = next.find_point(arc->start_point_id);
            auto* end = next.find_point(arc->end_point_id);
            start->x = center->x + arc->radius * std::cos(arc->start_angle);
            start->y = center->y + arc->radius * std::sin(arc->start_angle);
            end->x = center->x + arc->radius * std::cos(arc->end_angle);
            end->y = center->y + arc->radius * std::sin(arc->end_angle);
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
            point->x = center->x - dimension_value * std::sin(ellipse->rotation);
            point->y = center->y + dimension_value * std::cos(ellipse->rotation);
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
        major->x = center->x + ellipse->major_radius * std::cos(ellipse->rotation);
        major->y = center->y + ellipse->major_radius * std::sin(ellipse->rotation);
        minor->x = center->x - ellipse->minor_radius * std::sin(ellipse->rotation);
        minor->y = center->y + ellipse->minor_radius * std::cos(ellipse->rotation);
    }
    next.validate();
    const auto result = next.solve();
    if (result.status == SolveStatus::Conflicting || result.status == SolveStatus::Invalid) {
        throw std::runtime_error("Sketch dimension conflicts with existing geometry");
    }
    *this = std::move(next);
}

SolveResult Sketch::solve(std::size_t maximum_iterations) {
    try { validate(); } catch (const std::exception&) { return {SolveStatus::Invalid, 0, 0.0}; }
    if (maximum_iterations == 0) return {SolveStatus::Invalid, 0, 0.0};
    const auto original_points = points;
    const auto original_circles = circles;
    const auto original_arcs = arcs;
    constexpr double tolerance = 1.0e-8;
    double maximum_residual{};
    const auto move_pair = [](SketchPoint& first, SketchPoint& second,
                              double dx, double dy) {
        if (first.fixed && second.fixed) return false;
        if (first.fixed) { second.x += dx; second.y += dy; }
        else if (second.fixed) { first.x -= dx; first.y -= dy; }
        else {
            first.x -= dx * 0.5; first.y -= dy * 0.5;
            second.x += dx * 0.5; second.y += dy * 0.5;
        }
        return true;
    };
    for (std::size_t iteration = 0; iteration < maximum_iterations; ++iteration) {
        maximum_residual = 0.0;
        bool immovable_conflict = false;
        for (const auto& constraint : constraints) {
            if (constraint.suppressed) continue;
            if (constraint.kind == ConstraintKind::PointOnCircle) {
                auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                auto* point = find_point(constraint.first_point_id);
                const auto* center = find_point(circle->center_point_id);
                const double dx = point->x - center->x;
                const double dy = point->y - center->y;
                const double distance = std::hypot(dx, dy);
                if (distance <= tolerance) {
                    immovable_conflict = true;
                    continue;
                }
                const double residual = distance - circle->radius;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                if (std::abs(residual) > tolerance) {
                    if (point->fixed) {
                        circle->radius = distance;
                    } else {
                        point->x = center->x + dx * circle->radius / distance;
                        point->y = center->y + dy * circle->radius / distance;
                    }
                }
                continue;
            }
            if (is_segment_pair_constraint(constraint.kind)) {
                const auto first_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) { return value.id == constraint.geometry_id; });
                const auto second_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                const auto* reference_first = find_point(first_segment->first_point_id);
                const auto* reference_second = find_point(first_segment->second_point_id);
                auto* driven_first = find_point(second_segment->first_point_id);
                auto* driven_second = find_point(second_segment->second_point_id);
                const double rx = reference_second->x - reference_first->x;
                const double ry = reference_second->y - reference_first->y;
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
            auto* first = find_point(constraint.first_point_id);
            auto* second = find_point(constraint.second_point_id);
            double dx{};
            double dy{};
            if (constraint.kind == ConstraintKind::Horizontal) dy = first->y - second->y;
            else if (constraint.kind == ConstraintKind::Vertical) dx = first->x - second->x;
            else { dx = first->x - second->x; dy = first->y - second->y; }
            const double residual = std::hypot(dx, dy);
            maximum_residual = std::max(maximum_residual, residual);
            if (residual > tolerance && !move_pair(*first, *second, dx, dy)) {
                immovable_conflict = true;
            }
        }
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
                major->x = center->x + ellipse->major_radius * std::cos(desired);
                major->y = center->y + ellipse->major_radius * std::sin(desired);
                minor->x = center->x - ellipse->minor_radius * std::sin(desired);
                minor->y = center->y + ellipse->minor_radius * std::cos(desired);
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
                    : ellipse->rotation + 3.14159265358979323846 / 2.0;
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
                    radius = &arc->radius;
                }
                const double measured = dimension.kind == DimensionKind::Diameter
                    ? *radius * 2.0 : *radius;
                const double residual = measured - dimension.value;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                *radius = dimension.kind == DimensionKind::Diameter
                    ? dimension.value * 0.5 : dimension.value;
                continue;
            }
            auto* first = find_point(dimension.first_point_id);
            auto* second = find_point(dimension.second_point_id);
            double dx = second->x - first->x;
            double dy = second->y - first->y;
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
            if (std::abs(residual) > tolerance &&
                !move_pair(*first, *second, correction_x, correction_y)) {
                immovable_conflict = true;
            }
        }
        if (immovable_conflict) {
            points = original_points;
            circles = original_circles;
            arcs = original_arcs;
            return {SolveStatus::Conflicting, 0, maximum_residual};
        }
        if (maximum_residual <= tolerance) break;
    }
    if (maximum_residual > tolerance) {
        points = original_points;
        circles = original_circles;
        arcs = original_arcs;
        return {SolveStatus::Conflicting, 0, maximum_residual};
    }
    for (const auto& segment : segments) {
        const auto* first = find_point(segment.first_point_id);
        const auto* second = find_point(segment.second_point_id);
        if (std::hypot(second->x - first->x, second->y - first->y) <= tolerance) {
            points = original_points;
            circles = original_circles;
            arcs = original_arcs;
            return {SolveStatus::Conflicting, 0, 0.0};
        }
    }
    const auto residuals = [&]() {
        std::vector<double> result;
        for (const auto& constraint : constraints) {
            if (constraint.suppressed) continue;
            if (constraint.kind == ConstraintKind::PointOnCircle) {
                const auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) {
                        return value.id == constraint.geometry_id;
                    });
                const auto* point = find_point(constraint.first_point_id);
                const auto* center = find_point(circle->center_point_id);
                result.push_back(std::hypot(
                    point->x - center->x, point->y - center->y) - circle->radius);
                continue;
            }
            if (is_segment_pair_constraint(constraint.kind)) {
                const auto first_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) { return value.id == constraint.geometry_id; });
                const auto second_segment = std::find_if(segments.begin(), segments.end(),
                    [&](const auto& value) {
                        return value.id == constraint.second_geometry_id;
                    });
                const auto* a = find_point(first_segment->first_point_id);
                const auto* b = find_point(first_segment->second_point_id);
                const auto* c = find_point(second_segment->first_point_id);
                const auto* d = find_point(second_segment->second_point_id);
                const double rx = b->x - a->x;
                const double ry = b->y - a->y;
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
            const auto* first = find_point(constraint.first_point_id);
            const auto* second = find_point(constraint.second_point_id);
            if (constraint.kind == ConstraintKind::Horizontal) {
                result.push_back(first->y - second->y);
            } else if (constraint.kind == ConstraintKind::Vertical) {
                result.push_back(first->x - second->x);
            } else {
                result.push_back(first->x - second->x);
                result.push_back(first->y - second->y);
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
                    if (dimension.kind == DimensionKind::Diameter) {
                        throw std::runtime_error("Diameter dimension arc is invalid");
                    }
                    const auto arc = std::find_if(arcs.begin(), arcs.end(),
                        [&](const auto& value) { return value.id == dimension.geometry_id; });
                    result.push_back(arc->radius - dimension.value);
                }
                continue;
            }
            const auto* first = find_point(dimension.first_point_id);
            const auto* second = find_point(dimension.second_point_id);
            const double dx = second->x - first->x;
            const double dy = second->y - first->y;
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
    for (auto& point : points) {
        if (!point.fixed) {
            variables.push_back(&point.x);
            variables.push_back(&point.y);
        }
    }
    for (auto& circle : circles) variables.push_back(&circle.radius);
    for (auto& arc : arcs) variables.push_back(&arc.radius);
    const auto base = residuals();
    std::vector<std::vector<double>> jacobian(
        base.size(), std::vector<double>(variables.size()));
    constexpr double step = 1.0e-6;
    for (std::size_t column = 0; column < variables.size(); ++column) {
        *variables[column] += step;
        const auto shifted = residuals();
        *variables[column] -= step;
        for (std::size_t row = 0; row < base.size(); ++row) {
            jacobian[row][column] = (shifted[row] - base[row]) / step;
        }
    }
    std::size_t rank{};
    for (std::size_t column = 0; column < variables.size() && rank < jacobian.size();
         ++column) {
        auto pivot = rank;
        for (std::size_t row = rank + 1; row < jacobian.size(); ++row) {
            if (std::abs(jacobian[row][column]) > std::abs(jacobian[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(jacobian[pivot][column]) < 1.0e-7) continue;
        std::swap(jacobian[rank], jacobian[pivot]);
        const double divisor = jacobian[rank][column];
        for (std::size_t index = column; index < variables.size(); ++index) {
            jacobian[rank][index] /= divisor;
        }
        for (std::size_t row = 0; row < jacobian.size(); ++row) {
            if (row == rank) continue;
            const double factor = jacobian[row][column];
            for (std::size_t index = column; index < variables.size(); ++index) {
                jacobian[row][index] -= factor * jacobian[rank][index];
            }
        }
        ++rank;
    }
    const std::size_t dof = variables.size() > rank ? variables.size() - rank : 0;
    return {dof == 0 ? SolveStatus::Solved : SolveStatus::UnderConstrained,
            dof, maximum_residual};
}

zima::kernel::ViewerMesh Sketch::viewer_mesh() const {
    validate();
    const auto project = [&](const SketchPoint& point) {
        return world_point(point.x, point.y);
    };
    zima::kernel::ViewerMesh result;
    result.points.reserve(points.size());
    for (const auto& point : points) {
        result.points.push_back({project(point), {id, "point:" + point.id, {}}});
    }
    result.edges.reserve(segments.size());
    for (const auto& segment : segments) {
        result.edges.push_back({
            {project(*find_point(segment.first_point_id)),
             project(*find_point(segment.second_point_id))},
            {id, "segment:" + segment.id, {}}, segment.construction, true});
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
        const std::size_t count = spline.control_point_ids.size();
        const std::size_t degree = spline.degree;
        if (spline.closed) {
            std::vector<std::array<double, 2>> controls;
            controls.reserve(count + degree);
            for (std::size_t index = 0; index < count + degree; ++index) {
                const auto* point = find_point(spline.control_point_ids[index % count]);
                controls.push_back({point->x, point->y});
            }
            zima::kernel::ViewerEdge edge;
            edge.reference = {id, "bspline:" + spline.id, {}};
            edge.construction = spline.construction;
            edge.overlay = true;
            constexpr std::size_t samples = 128;
            edge.points.reserve(samples + 1);
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
                        const double denominator = static_cast<double>(degree - level + 1);
                        const double weight = (parameter -
                            static_cast<double>(knot_index)) / denominator;
                        values[index][0] = (1.0 - weight) * values[index - 1][0] +
                            weight * values[index][0];
                        values[index][1] = (1.0 - weight) * values[index - 1][1] +
                            weight * values[index][1];
                        if (index == level) break;
                    }
                }
                edge.points.push_back(world_point(values[degree][0], values[degree][1]));
            }
            edge.points.back() = edge.points.front();
            result.edges.push_back(std::move(edge));
            continue;
        }
        std::vector<double> knots(count + degree + 1, 1.0);
        for (std::size_t index = 0; index <= degree; ++index) knots[index] = 0.0;
        const std::size_t spans = count - degree;
        for (std::size_t index = degree + 1; index < count; ++index) {
            knots[index] = static_cast<double>(index - degree) /
                static_cast<double>(spans);
        }
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "bspline:" + spline.id, {}};
        edge.construction = spline.construction;
        edge.overlay = true;
        constexpr std::size_t samples = 128;
        edge.points.reserve(samples + 1);
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
                const auto* point = find_point(
                    spline.control_point_ids[span - degree + index]);
                values[index] = {point->x, point->y};
            }
            for (std::size_t level = 1; level <= degree; ++level) {
                for (std::size_t index = degree; index >= level; --index) {
                    const std::size_t knot_index = span - degree + index;
                    const double denominator =
                        knots[knot_index + degree - level + 1] - knots[knot_index];
                    const double weight = denominator <= 1.0e-15 ? 0.0
                        : (parameter - knots[knot_index]) / denominator;
                    values[index][0] = (1.0 - weight) * values[index - 1][0] +
                        weight * values[index][0];
                    values[index][1] = (1.0 - weight) * values[index - 1][1] +
                        weight * values[index][1];
                    if (index == level) break;
                }
            }
            edge.points.push_back(world_point(values[degree][0], values[degree][1]));
        }
        result.edges.push_back(std::move(edge));
    }
    for (const auto& ellipse : ellipses) {
        const auto* center = find_point(ellipse.center_point_id);
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
                    local_y * std::sin(ellipse.rotation),
                center->y + local_x * std::sin(ellipse.rotation) +
                    local_y * std::cos(ellipse.rotation)));
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
    result.dimensions.reserve(dimensions.size());
    for (const auto& dimension : dimensions) {
        if (dimension.suppressed) continue;
        if (dimension.kind == DimensionKind::EllipseRotation) {
            const auto ellipse = std::find_if(ellipses.begin(), ellipses.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (ellipse == ellipses.end()) continue;
            const auto* center = find_point(ellipse->center_point_id);
            const auto* major = find_point(ellipse->major_point_id);
            result.dimensions.push_back({
                project(*center), project(*major), project(*center), project(*major),
                dimension.value, {id, "dimension:" + dimension.id, {}}, "∠", " °"});
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
            continue;
        }
        if (dimension.kind == DimensionKind::Diameter) {
            const auto circle = std::find_if(circles.begin(), circles.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (circle == circles.end()) continue;
            const auto* center = find_point(circle->center_point_id);
            const auto first_rim = world_point(center->x - circle->radius, center->y);
            const auto second_rim = world_point(center->x + circle->radius, center->y);
            result.dimensions.push_back({
                first_rim, second_rim, first_rim, second_rim, dimension.value,
                {id, "dimension:" + dimension.id, {}}, "Ø"});
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
                if (arc == arcs.end()) continue;
                center = find_point(arc->center_point_id);
                radius = arc->radius;
                angle = (arc->start_angle + arc->end_angle) * 0.5;
            }
            const auto rim = world_point(
                center->x + radius * std::cos(angle),
                center->y + radius * std::sin(angle));
            result.dimensions.push_back({
                project(*center), rim, project(*center), rim, dimension.value,
                {id, "dimension:" + dimension.id, {}}, "R"});
            continue;
        }
        const auto* first = find_point(dimension.first_point_id);
        const auto* second = find_point(dimension.second_point_id);
        const double dx = second->x - first->x;
        const double dy = second->y - first->y;
        const double magnitude = std::hypot(dx, dy);
        const double offset = std::clamp(magnitude * 0.15, 5.0, 25.0);
        if (dimension.kind == DimensionKind::Angle) {
            const double display_radius = std::clamp(magnitude * 0.35, 8.0, 30.0);
            const double radians = dimension.value *
                3.14159265358979323846 / 180.0;
            result.dimensions.push_back({
                project(*first), project(*first),
                world_point(first->x + display_radius, first->y),
                world_point(first->x + display_radius * std::cos(radians),
                            first->y + display_radius * std::sin(radians)),
                dimension.value, {id, "dimension:" + dimension.id, {}},
                "∠ ", "°"});
            continue;
        }
        if (dimension.kind == DimensionKind::DistanceX) {
            const double line_y = std::max(first->y, second->y) + offset;
            result.dimensions.push_back({
                project(*first), project(*second),
                world_point(first->x, line_y), world_point(second->x, line_y),
                dimension.value, {id, "dimension:" + dimension.id, {}}, "X "});
            continue;
        }
        if (dimension.kind == DimensionKind::DistanceY) {
            const double line_x = std::max(first->x, second->x) + offset;
            result.dimensions.push_back({
                project(*first), project(*second),
                world_point(line_x, first->y), world_point(line_x, second->y),
                dimension.value, {id, "dimension:" + dimension.id, {}}, "Y "});
            continue;
        }
        const double nx = magnitude > 1.0e-12 ? -dy / magnitude : 0.0;
        const double ny = magnitude > 1.0e-12 ? dx / magnitude : 1.0;
        result.dimensions.push_back({
            project(*first), project(*second),
            world_point(first->x + nx * offset, first->y + ny * offset),
            world_point(second->x + nx * offset, second->y + ny * offset),
            dimension.value, {id, "dimension:" + dimension.id, {}}});
    }
    return result;
}

zima::kernel::Vec3 Sketch::world_point(double x, double y) const {
    require_finite(x, "sketch x");
    require_finite(y, "sketch y");
    if (plane == SketchPlane::XY) return {x, y, plane_offset};
    if (plane == SketchPlane::XZ) return {x, plane_offset, y};
    return {plane_offset, x, y};
}

std::optional<std::array<double, 2>> Sketch::intersect_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    constexpr double epsilon = 1.0e-12;
    const double denominator = plane == SketchPlane::XY ? direction.z
        : plane == SketchPlane::XZ ? direction.y : direction.x;
    if (std::abs(denominator) <= epsilon) return std::nullopt;
    const double coordinate = plane == SketchPlane::XY ? origin.z
        : plane == SketchPlane::XZ ? origin.y : origin.x;
    const double parameter = (plane_offset - coordinate) / denominator;
    if (!std::isfinite(parameter) || parameter < 0.0) return std::nullopt;
    const zima::kernel::Vec3 point{
        origin.x + direction.x * parameter,
        origin.y + direction.y * parameter,
        origin.z + direction.z * parameter};
    if (plane == SketchPlane::XY) return std::array<double, 2>{point.x, point.y};
    if (plane == SketchPlane::XZ) return std::array<double, 2>{point.x, point.z};
    return std::array<double, 2>{point.y, point.z};
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
        {"second", segment.second_point_id}, {"construction", segment.construction}});
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
        {"construction", ellipse.construction}});
    nlohmann::json spline_values = nlohmann::json::array();
    for (const auto& spline : bsplines) spline_values.push_back({
        {"id", spline.id}, {"control_points", spline.control_point_ids},
        {"degree", spline.degree}, {"closed", spline.closed},
        {"construction", spline.construction}});
    nlohmann::json import_block_values = nlohmann::json::array();
    for (const auto& block : import_blocks) import_block_values.push_back({
        {"id", block.id}, {"name", block.name}, {"source_path", block.source_path},
        {"geometry_ids", block.geometry_ids}, {"point_ids", block.point_ids},
        {"translation_x", block.translation_x},
        {"translation_y", block.translation_y}, {"rotation", block.rotation}});
    nlohmann::json constraint_values = nlohmann::json::array();
    for (const auto& constraint : constraints) constraint_values.push_back({
        {"id", constraint.id}, {"kind", constraint_name(constraint.kind)},
        {"first", constraint.first_point_id}, {"second", constraint.second_point_id},
        {"suppressed", constraint.suppressed}, {"geometry", constraint.geometry_id},
        {"second_geometry", constraint.second_geometry_id}});
    nlohmann::json dimension_values = nlohmann::json::array();
    for (const auto& dimension : dimensions) {
        nlohmann::json value{{"id", dimension.id}, {"kind", dimension_name(dimension.kind)},
            {"first", dimension.first_point_id}, {"second", dimension.second_point_id},
            {"value", dimension.value}, {"driving", dimension.driving},
            {"suppressed", dimension.suppressed}, {"geometry", dimension.geometry_id}};
        if (dimension.lower_limit) value["lower_limit"] = *dimension.lower_limit;
        if (dimension.upper_limit) value["upper_limit"] = *dimension.upper_limit;
        dimension_values.push_back(std::move(value));
    }
    const nlohmann::json root{{"format", "zima-cad-cpp-sketch"}, {"version", 7},
        {"id", id}, {"name", name}, {"plane", plane_name(plane)},
        {"plane_offset", plane_offset}, {"points", std::move(point_values)},
        {"segments", std::move(segment_values)},
        {"circles", std::move(circle_values)},
        {"arcs", std::move(arc_values)},
        {"ellipses", std::move(ellipse_values)},
        {"bsplines", std::move(spline_values)},
        {"import_blocks", std::move(import_block_values)},
        {"constraints", std::move(constraint_values)},
        {"dimensions", std::move(dimension_values)}};
    return root.dump(2);
}

Sketch Sketch::from_serialized(const std::string& value) {
    const auto root = nlohmann::json::parse(value);
    if (root.at("format") != "zima-cad-cpp-sketch" || root.at("version") != 7) {
        throw std::runtime_error("Unsupported sketch format");
    }
    Sketch sketch;
    sketch.id = root.at("id").get<std::string>();
    sketch.name = root.at("name").get<std::string>();
    sketch.plane = plane_from_name(root.at("plane").get<std::string>());
    sketch.plane_offset = root.at("plane_offset").get<double>();
    for (const auto& value : root.at("points")) sketch.points.push_back({
        value.at("id").get<std::string>(), value.at("x").get<double>(),
        value.at("y").get<double>(), value.at("fixed").get<bool>(),
        value.at("construction").get<bool>()});
    for (const auto& value : root.at("segments")) sketch.segments.push_back({
        value.at("id").get<std::string>(), value.at("first").get<std::string>(),
        value.at("second").get<std::string>(), value.at("construction").get<bool>()});
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
        value.at("construction").get<bool>()});
    for (const auto& value : root.at("bsplines")) sketch.bsplines.push_back({
        value.at("id").get<std::string>(),
        value.at("control_points").get<std::vector<std::string>>(),
        value.at("degree").get<unsigned>(), value.at("closed").get<bool>(),
        value.at("construction").get<bool>()});
    for (const auto& value : root.at("import_blocks")) sketch.import_blocks.push_back({
        value.at("id").get<std::string>(), value.at("name").get<std::string>(),
        value.at("source_path").get<std::string>(),
        value.at("geometry_ids").get<std::vector<std::string>>(),
        value.at("point_ids").get<std::vector<std::string>>(),
        value.at("translation_x").get<double>(),
        value.at("translation_y").get<double>(), value.at("rotation").get<double>()});
    for (const auto& value : root.at("constraints")) sketch.constraints.push_back({
        value.at("id").get<std::string>(), constraint_from_name(value.at("kind")),
        value.at("first").get<std::string>(), value.at("second").get<std::string>(),
        value.at("suppressed").get<bool>(), value.at("geometry").get<std::string>(),
        value.at("second_geometry").get<std::string>()});
    for (const auto& value : root.at("dimensions")) {
        SketchDimension dimension{value.at("id").get<std::string>(),
            dimension_from_name(value.at("kind")), value.at("first").get<std::string>(),
            value.at("second").get<std::string>(), value.at("value").get<double>(),
            value.at("driving").get<bool>(), value.at("suppressed").get<bool>()};
        dimension.geometry_id = value.at("geometry").get<std::string>();
        if (value.contains("lower_limit")) dimension.lower_limit = value.at("lower_limit").get<double>();
        if (value.contains("upper_limit")) dimension.upper_limit = value.at("upper_limit").get<double>();
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
