#include <zima/sketcher/sketch.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
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
    }
    throw std::invalid_argument("Unknown sketch constraint");
}

ConstraintKind constraint_from_name(const std::string& name) {
    if (name == "horizontal") return ConstraintKind::Horizontal;
    if (name == "vertical") return ConstraintKind::Vertical;
    if (name == "coincident") return ConstraintKind::Coincident;
    throw std::runtime_error("Unknown sketch constraint");
}

const char* dimension_name(DimensionKind kind) {
    switch (kind) {
    case DimensionKind::Distance: return "distance";
    case DimensionKind::DistanceX: return "distance_x";
    case DimensionKind::DistanceY: return "distance_y";
    case DimensionKind::Radius: return "radius";
    }
    throw std::invalid_argument("Unknown sketch dimension");
}

DimensionKind dimension_from_name(const std::string& name) {
    if (name == "distance") return DimensionKind::Distance;
    if (name == "distance_x") return DimensionKind::DistanceX;
    if (name == "distance_y") return DimensionKind::DistanceY;
    if (name == "radius") return DimensionKind::Radius;
    throw std::runtime_error("Unknown sketch dimension");
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) throw std::runtime_error(std::string(field) + " must be finite");
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
        if (arc.id.empty() || !ids.insert(arc.id).second ||
            find_point(arc.center_point_id) == nullptr ||
            !std::isfinite(arc.radius) || arc.radius <= 0.0 ||
            !std::isfinite(arc.start_angle) || !std::isfinite(arc.end_angle) ||
            arc.end_angle <= arc.start_angle ||
            arc.end_angle - arc.start_angle >= 2.0 * 3.14159265358979323846) {
            throw std::runtime_error("Sketch arc is invalid");
        }
    }
    ids.clear();
    for (const auto& constraint : constraints) {
        if (constraint.id.empty() || !ids.insert(constraint.id).second ||
            find_point(constraint.first_point_id) == nullptr ||
            find_point(constraint.second_point_id) == nullptr) {
            throw std::runtime_error("Sketch constraint is invalid");
        }
    }
    ids.clear();
    for (const auto& dimension : dimensions) {
        const bool radius_dimension = dimension.kind == DimensionKind::Radius;
        const bool geometry_valid = radius_dimension &&
            (std::any_of(circles.begin(), circles.end(), [&](const auto& circle) {
                return circle.id == dimension.geometry_id;
            }) || std::any_of(arcs.begin(), arcs.end(), [&](const auto& arc) {
                return arc.id == dimension.geometry_id;
            }));
        if (dimension.id.empty() || !ids.insert(dimension.id).second ||
            (radius_dimension ? !geometry_valid
                : find_point(dimension.first_point_id) == nullptr ||
                  find_point(dimension.second_point_id) == nullptr)) {
            throw std::runtime_error("Sketch dimension is invalid");
        }
        require_finite(dimension.value, "dimension value");
        if ((dimension.kind == DimensionKind::Distance || radius_dimension) &&
            dimension.value < 0.0) {
            throw std::runtime_error("Distance must not be negative");
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
}

bool Sketch::set_dimension_value(const std::string& dimension_id, double value) {
    if (!std::isfinite(value)) return false;
    const auto found = std::find_if(dimensions.begin(), dimensions.end(),
        [&](const auto& dimension) { return dimension.id == dimension_id; });
    if (found == dimensions.end() ||
        ((found->kind == DimensionKind::Distance ||
          found->kind == DimensionKind::Radius) && value < 0.0) ||
        (found->lower_limit && value < *found->lower_limit) ||
        (found->upper_limit && value > *found->upper_limit)) return false;
    found->value = value;
    return true;
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
    point->x = x;
    point->y = y;
    const auto solved = next.solve();
    if (solved.status == SolveStatus::Conflicting || solved.status == SolveStatus::Invalid) {
        return false;
    }
    for (auto& dimension : next.dimensions) {
        if (dimension.suppressed || dimension.driving) continue;
        double measured{};
        if (dimension.kind == DimensionKind::Radius) {
            const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
                [&](const auto& value) { return value.id == dimension.geometry_id; });
            if (circle != next.circles.end()) {
                measured = circle->radius;
            } else {
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
                : std::hypot(dx, dy);
        }
        if ((dimension.lower_limit && measured < *dimension.lower_limit) ||
            (dimension.upper_limit && measured > *dimension.upper_limit)) {
            return false;
        }
        dimension.value = measured;
    }
    next.validate();
    *this = std::move(next);
    return true;
}

std::string Sketch::add_segment(
    double first_x, double first_y, double second_x, double second_y,
    double snap_tolerance) {
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
    auto segment = create_segment(first_id, second_id);
    const auto segment_id = segment.id;
    next.segments.push_back(std::move(segment));
    next.validate();
    *this = std::move(next);
    return segment_id;
}

std::string Sketch::add_segment_constraint(
    const std::string& segment_id, ConstraintKind kind) {
    if (kind == ConstraintKind::Coincident) {
        throw std::invalid_argument("Coincident is a point-to-point constraint");
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
        make_id(), kind, segment->first_point_id, segment->second_point_id};
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
    SketchArc arc{make_id(), center_id, radius, start_angle, end_angle, construction};
    const auto id = arc.id;
    next.arcs.push_back(std::move(arc));
    next.validate();
    *this = std::move(next);
    return id;
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
        : kind == DimensionKind::DistanceY ? dy : std::hypot(dx, dy);
    return {make_id(), kind, first->id, second->id, value};
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
                    value.kind == dimension.kind &&
                    (dimension.kind == DimensionKind::Radius
                        ? value.geometry_id == dimension.geometry_id
                        : value.first_point_id == dimension.first_point_id &&
                          value.second_point_id == dimension.second_point_id);
            });
        if (same_driver) throw std::invalid_argument("Segment already owns this driving dimension");
        next.dimensions.push_back(std::move(dimension));
    } else {
        *existing = std::move(dimension);
    }
    if (dimension_kind == DimensionKind::Radius) {
        const auto circle = std::find_if(next.circles.begin(), next.circles.end(),
            [&](const auto& value) { return value.id == dimension_geometry_id; });
        if (circle != next.circles.end()) {
            circle->radius = dimension_value;
        } else {
            const auto arc = std::find_if(next.arcs.begin(), next.arcs.end(),
                [&](const auto& value) { return value.id == dimension_geometry_id; });
            if (arc == next.arcs.end()) {
                throw std::invalid_argument("Radius dimension geometry does not exist");
            }
            arc->radius = dimension_value;
        }
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
            if (dimension.kind == DimensionKind::Radius) {
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
                const double residual = *radius - dimension.value;
                maximum_residual = std::max(maximum_residual, std::abs(residual));
                *radius = dimension.value;
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
            if (dimension.kind == DimensionKind::Radius) {
                const auto circle = std::find_if(circles.begin(), circles.end(),
                    [&](const auto& value) { return value.id == dimension.geometry_id; });
                if (circle != circles.end()) {
                    result.push_back(circle->radius - dimension.value);
                } else {
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
            {id, "segment:" + segment.id, {}}});
    }
    constexpr std::size_t circle_samples = 96;
    for (const auto& circle : circles) {
        const auto* center = find_point(circle.center_point_id);
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "circle:" + circle.id, {}};
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
    for (const auto& arc : arcs) {
        const auto* center = find_point(arc.center_point_id);
        zima::kernel::ViewerEdge edge;
        edge.reference = {id, "arc:" + arc.id, {}};
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
        {"id", arc.id}, {"center", arc.center_point_id}, {"radius", arc.radius},
        {"start_angle", arc.start_angle}, {"end_angle", arc.end_angle},
        {"construction", arc.construction}});
    nlohmann::json constraint_values = nlohmann::json::array();
    for (const auto& constraint : constraints) constraint_values.push_back({
        {"id", constraint.id}, {"kind", constraint_name(constraint.kind)},
        {"first", constraint.first_point_id}, {"second", constraint.second_point_id},
        {"suppressed", constraint.suppressed}});
    nlohmann::json dimension_values = nlohmann::json::array();
    for (const auto& dimension : dimensions) {
        nlohmann::json value{{"id", dimension.id}, {"kind", dimension_name(dimension.kind)},
            {"first", dimension.first_point_id}, {"second", dimension.second_point_id},
            {"value", dimension.value}, {"driving", dimension.driving},
            {"suppressed", dimension.suppressed}};
        if (!dimension.geometry_id.empty()) value["geometry"] = dimension.geometry_id;
        if (dimension.lower_limit) value["lower_limit"] = *dimension.lower_limit;
        if (dimension.upper_limit) value["upper_limit"] = *dimension.upper_limit;
        dimension_values.push_back(std::move(value));
    }
    const nlohmann::json root{{"format", "zima-cad-cpp-sketch"}, {"version", 1},
        {"id", id}, {"name", name}, {"plane", plane_name(plane)},
        {"plane_offset", plane_offset}, {"points", std::move(point_values)},
        {"segments", std::move(segment_values)},
        {"circles", std::move(circle_values)},
        {"arcs", std::move(arc_values)},
        {"constraints", std::move(constraint_values)},
        {"dimensions", std::move(dimension_values)}};
    return root.dump(2);
}

Sketch Sketch::from_serialized(const std::string& value) {
    const auto root = nlohmann::json::parse(value);
    if (root.at("format") != "zima-cad-cpp-sketch" || root.at("version") != 1) {
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
        value.at("radius").get<double>(), value.at("start_angle").get<double>(),
        value.at("end_angle").get<double>(), value.at("construction").get<bool>()});
    for (const auto& value : root.at("constraints")) sketch.constraints.push_back({
        value.at("id").get<std::string>(), constraint_from_name(value.at("kind")),
        value.at("first").get<std::string>(), value.at("second").get<std::string>(),
        value.at("suppressed").get<bool>()});
    for (const auto& value : root.at("dimensions")) {
        SketchDimension dimension{value.at("id").get<std::string>(),
            dimension_from_name(value.at("kind")), value.at("first").get<std::string>(),
            value.at("second").get<std::string>(), value.at("value").get<double>(),
            value.at("driving").get<bool>(), value.at("suppressed").get<bool>()};
        dimension.geometry_id = value.value("geometry", std::string{});
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
