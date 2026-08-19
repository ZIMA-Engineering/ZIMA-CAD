#include <zima/document/part_document.hpp>
#include <zima/document/viewer_packet_json.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace zima::document {
namespace {

std::string make_id() {
    std::mt19937_64 generator(
        static_cast<std::mt19937_64::result_type>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<unsigned long long> distribution;
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << distribution(generator) << std::setw(16) << distribution(generator);
    return stream.str();
}

void require_positive(double value, const char* field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::runtime_error(std::string(field) + " must be finite and positive");
    }
}

void require_finite(double value, const char* field) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
}

void validate_placement(const Placement& placement) {
    require_finite(placement.x, "placement x");
    require_finite(placement.y, "placement y");
    require_finite(placement.z, "placement z");
    require_finite(placement.rotation_x, "rotation x");
    require_finite(placement.rotation_y, "rotation y");
    require_finite(placement.rotation_z, "rotation z");
}

void require_default_sketch_feature_placement(const Placement& placement) {
    if (placement.x != 0.0 || placement.y != 0.0 || placement.z != 0.0 ||
        placement.rotation_x != 0.0 || placement.rotation_y != 0.0 ||
        placement.rotation_z != 0.0) {
        throw std::runtime_error(
            "Sketch feature placement is defined by its source Sketch");
    }
}

void validate_extrusion_direction(ExtrusionDirection direction) {
    switch (direction) {
        case ExtrusionDirection::Forward:
        case ExtrusionDirection::Reverse:
        case ExtrusionDirection::Symmetric:
            return;
    }
    throw std::runtime_error("Invalid Extrusion direction");
}

zima::kernel::ExtrusionRequest extrusion_request(
    const zima::sketcher::Sketch& sketch, double height,
    ExtrusionDirection direction_mode) {
    require_positive(height, "extrusion height");
    validate_extrusion_direction(direction_mode);
    zima::kernel::ExtrusionRequest request;
    request.direction = sketch.plane == zima::sketcher::SketchPlane::XY
        ? zima::kernel::Vec3{0.0, 0.0, height}
        : sketch.plane == zima::sketcher::SketchPlane::XZ
            ? zima::kernel::Vec3{0.0, -height, 0.0}
            : zima::kernel::Vec3{height, 0.0, 0.0};
    if (direction_mode == ExtrusionDirection::Reverse) {
        request.direction.x = -request.direction.x;
        request.direction.y = -request.direction.y;
        request.direction.z = -request.direction.z;
    }
    const auto finalize = [&](zima::kernel::ExtrusionRequest value) {
        if (direction_mode != ExtrusionDirection::Symmetric) return value;
        const zima::kernel::Vec3 offset{
            -0.5 * value.direction.x,
            -0.5 * value.direction.y,
            -0.5 * value.direction.z};
        const auto shift = [&](auto& profile_variant) {
            std::visit([&](auto& profile) {
                using Profile = std::decay_t<decltype(profile)>;
                if constexpr (std::is_same_v<Profile,
                                  zima::kernel::ExtrusionRequest::PolygonProfile>) {
                    for (auto& point : profile.vertices) {
                        point.x += offset.x;
                        point.y += offset.y;
                        point.z += offset.z;
                    }
                } else if constexpr (std::is_same_v<Profile,
                                         zima::kernel::ExtrusionRequest::CircleProfile>) {
                    profile.center.x += offset.x;
                    profile.center.y += offset.y;
                    profile.center.z += offset.z;
                } else if constexpr (std::is_same_v<Profile,
                                         zima::kernel::ExtrusionRequest::EllipseProfile>) {
                    profile.center.x += offset.x;
                    profile.center.y += offset.y;
                    profile.center.z += offset.z;
                } else {
                    for (auto& curve : profile.curves) {
                        std::visit([&](auto& exact_curve) {
                            const auto shift_point = [&](auto& point) {
                                point.x += offset.x;
                                point.y += offset.y;
                                point.z += offset.z;
                            };
                            shift_point(exact_curve.start);
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::ArcCurve>) {
                                shift_point(exact_curve.middle);
                            }
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                                shift_point(exact_curve.center);
                            }
                            if constexpr (std::is_same_v<
                                              std::decay_t<decltype(exact_curve)>,
                                              zima::kernel::ExtrusionRequest::BSplineCurve>) {
                                for (auto& point : exact_curve.control_points) {
                                    shift_point(point);
                                }
                            }
                            shift_point(exact_curve.end);
                        }, curve);
                    }
                }
            }, profile_variant);
        };
        shift(value.outer_profile);
        for (auto& profile : value.inner_profiles) shift(profile);
        return value;
    };
    std::vector<const zima::sketcher::SketchSegment*> profile_segments;
    for (const auto& segment : sketch.segments) {
        if (!segment.construction) profile_segments.push_back(&segment);
    }
    std::vector<const zima::sketcher::SketchCircle*> profile_circles;
    for (const auto& circle : sketch.circles) {
        if (!circle.construction) profile_circles.push_back(&circle);
    }
    std::vector<const zima::sketcher::SketchArc*> profile_arcs;
    for (const auto& arc : sketch.arcs) {
        if (!arc.construction) profile_arcs.push_back(&arc);
    }
    std::vector<const zima::sketcher::SketchEllipse*> profile_ellipses;
    for (const auto& ellipse : sketch.ellipses) {
        if (!ellipse.construction) profile_ellipses.push_back(&ellipse);
    }
    std::vector<const zima::sketcher::SketchEllipticalArc*>
        profile_elliptical_arcs;
    for (const auto& arc : sketch.elliptical_arcs) {
        if (!arc.construction) profile_elliptical_arcs.push_back(&arc);
    }
    std::vector<const zima::sketcher::SketchBSpline*> profile_splines;
    for (const auto& spline : sketch.bsplines) {
        if (!spline.construction) profile_splines.push_back(&spline);
    }
    const auto exact_spline = [&](const auto& spline) {
        zima::kernel::ExtrusionRequest::BSplineCurve curve;
        curve.degree = spline.degree;
        curve.periodic = spline.closed;
        for (const auto& point_id : spline.control_point_ids) {
            const auto* point = sketch.find_point(point_id);
            curve.control_points.push_back(sketch.world_point(point->x, point->y));
        }
        curve.start = curve.control_points.front();
        curve.end = spline.closed ? curve.start : curve.control_points.back();
        return curve;
    };
    const auto exact_elliptical_arc = [&](const auto& arc) {
        const auto* center = sketch.find_point(arc.center_point_id);
        const auto* major = sketch.find_point(arc.major_point_id);
        const auto* minor = sketch.find_point(arc.minor_point_id);
        const auto* start = sketch.find_point(arc.start_point_id);
        const auto* end = sketch.find_point(arc.end_point_id);
        const auto center_world = sketch.world_point(center->x, center->y);
        const bool swap_axes = arc.major_radius < arc.minor_radius;
        const auto axis_world = swap_axes
            ? sketch.world_point(minor->x, minor->y)
            : sketch.world_point(major->x, major->y);
        const double major_radius = swap_axes ? arc.minor_radius : arc.major_radius;
        zima::kernel::ExtrusionRequest::EllipticalArcCurve curve;
        curve.start = sketch.world_point(start->x, start->y);
        curve.end = sketch.world_point(end->x, end->y);
        curve.center = center_world;
        curve.major_axis_direction = {
            (axis_world.x - center_world.x) / major_radius,
            (axis_world.y - center_world.y) / major_radius,
            (axis_world.z - center_world.z) / major_radius};
        curve.major_radius = major_radius;
        curve.minor_radius = swap_axes ? arc.major_radius : arc.minor_radius;
        curve.start_parameter = arc.start_parameter -
            (swap_axes ? 3.14159265358979323846 / 2.0 : 0.0);
        curve.end_parameter = arc.end_parameter -
            (swap_axes ? 3.14159265358979323846 / 2.0 : 0.0);
        curve.reversed = arc.reversed !=
            (direction_mode == ExtrusionDirection::Reverse);
        return curve;
    };
    const auto closed_spline = std::find_if(profile_splines.begin(), profile_splines.end(),
        [](const auto* spline) { return spline->closed; });
    if (closed_spline != profile_splines.end()) {
        if (std::count_if(profile_splines.begin(), profile_splines.end(),
                [](const auto* spline) { return spline->closed; }) != 1 ||
            profile_splines.size() != 1 || !profile_segments.empty() ||
            !profile_arcs.empty() || !profile_ellipses.empty() ||
            !profile_elliptical_arcs.empty()) {
            throw std::runtime_error(
                "Closed B-spline profile must be one standalone outer loop");
        }
        zima::kernel::ExtrusionRequest::CurvedProfile outer;
        outer.curves.push_back(exact_spline(**closed_spline));
        request.outer_profile = std::move(outer);
        for (const auto* circle : profile_circles) {
            const auto* center = sketch.find_point(circle->center_point_id);
            request.inner_profiles.push_back(
                zima::kernel::ExtrusionRequest::CircleProfile{
                    sketch.world_point(center->x, center->y), circle->radius});
        }
        return finalize(std::move(request));
    }
    if (!profile_ellipses.empty()) {
        if (profile_ellipses.size() != 1 || !profile_segments.empty() ||
            !profile_circles.empty() || !profile_arcs.empty() ||
            !profile_elliptical_arcs.empty()) {
            throw std::runtime_error(
                "Ellipse profile must currently be one standalone closed loop");
        }
        const auto* ellipse = profile_ellipses.front();
        const auto* center = sketch.find_point(ellipse->center_point_id);
        const auto* major = sketch.find_point(ellipse->major_point_id);
        const auto* minor = sketch.find_point(ellipse->minor_point_id);
        auto center_world = sketch.world_point(center->x, center->y);
        auto axis_point = ellipse->major_radius >= ellipse->minor_radius
            ? sketch.world_point(major->x, major->y)
            : sketch.world_point(minor->x, minor->y);
        const double major_radius = std::max(
            ellipse->major_radius, ellipse->minor_radius);
        const double minor_radius = std::min(
            ellipse->major_radius, ellipse->minor_radius);
        request.outer_profile = zima::kernel::ExtrusionRequest::EllipseProfile{
            center_world,
            {(axis_point.x - center_world.x) / major_radius,
             (axis_point.y - center_world.y) / major_radius,
             (axis_point.z - center_world.z) / major_radius},
            major_radius, minor_radius};
        return finalize(std::move(request));
    }
    if (!profile_arcs.empty() || !profile_elliptical_arcs.empty() ||
        !profile_splines.empty()) {
        struct CurveRecord {
            std::string id;
            std::string start_point_id;
            std::string end_point_id;
            std::array<double, 2> start;
            std::array<double, 2> end;
            std::variant<zima::kernel::ExtrusionRequest::LineCurve,
                         zima::kernel::ExtrusionRequest::ArcCurve,
                         zima::kernel::ExtrusionRequest::EllipticalArcCurve,
                         zima::kernel::ExtrusionRequest::BSplineCurve> curve;
            std::size_t start_node{};
            std::size_t end_node{};
        };
        std::vector<CurveRecord> curves;
        for (const auto* segment : profile_segments) {
            const auto* first = sketch.find_point(segment->first_point_id);
            const auto* second = sketch.find_point(segment->second_point_id);
            curves.push_back({segment->id, segment->first_point_id,
                segment->second_point_id, {first->x, first->y},
                {second->x, second->y},
                zima::kernel::ExtrusionRequest::LineCurve{
                    sketch.world_point(first->x, first->y),
                    sketch.world_point(second->x, second->y)}});
        }
        for (const auto* arc : profile_arcs) {
            const auto* center = sketch.find_point(arc->center_point_id);
            const auto* start_point = sketch.find_point(arc->start_point_id);
            const auto* end_point = sketch.find_point(arc->end_point_id);
            const double middle_angle = 0.5 * (arc->start_angle + arc->end_angle);
            const std::array<double, 2> start{
                start_point->x, start_point->y};
            const std::array<double, 2> middle{
                center->x + arc->radius * std::cos(middle_angle),
                center->y + arc->radius * std::sin(middle_angle)};
            const std::array<double, 2> end{
                end_point->x, end_point->y};
            curves.push_back({arc->id, arc->start_point_id, arc->end_point_id,
                start, end,
                zima::kernel::ExtrusionRequest::ArcCurve{
                    sketch.world_point(start[0], start[1]),
                    sketch.world_point(middle[0], middle[1]),
                    sketch.world_point(end[0], end[1])}});
        }
        for (const auto* arc : profile_elliptical_arcs) {
            const auto* start = sketch.find_point(arc->start_point_id);
            const auto* end = sketch.find_point(arc->end_point_id);
            curves.push_back({arc->id, arc->start_point_id, arc->end_point_id,
                {start->x, start->y}, {end->x, end->y},
                exact_elliptical_arc(*arc)});
        }
        for (const auto* spline : profile_splines) {
            const auto* first = sketch.find_point(spline->control_point_ids.front());
            const auto* last = sketch.find_point(spline->control_point_ids.back());
            curves.push_back({spline->id, spline->control_point_ids.front(),
                spline->control_point_ids.back(), {first->x, first->y},
                {last->x, last->y}, exact_spline(*spline)});
        }
        std::vector<std::string> nodes;
        std::vector<std::vector<std::size_t>> incident_curves;
        const auto node_for = [&](const std::string& point_id) {
            for (std::size_t index = 0; index < nodes.size(); ++index) {
                if (nodes[index] == point_id) return index;
            }
            nodes.push_back(point_id);
            incident_curves.emplace_back();
            return nodes.size() - 1;
        };
        for (std::size_t index = 0; index < curves.size(); ++index) {
            curves[index].start_node = node_for(curves[index].start_point_id);
            curves[index].end_node = node_for(curves[index].end_point_id);
            incident_curves[curves[index].start_node].push_back(index);
            incident_curves[curves[index].end_node].push_back(index);
        }
        for (const auto& incident : incident_curves) {
            if (incident.size() != 2) {
                throw std::runtime_error(
                    "Curved Extrusion profile must be one closed non-branching loop");
            }
        }
        const auto first_curve = static_cast<std::size_t>(std::distance(
            curves.begin(), std::min_element(curves.begin(), curves.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                })));
        const std::size_t first_node = curves[first_curve].start_node;
        std::size_t current_node = first_node;
        std::optional<std::size_t> previous_curve;
        std::unordered_set<std::size_t> visited;
        zima::kernel::ExtrusionRequest::CurvedProfile ordered;
        do {
            auto candidates = incident_curves[current_node];
            std::sort(candidates.begin(), candidates.end(), [&](auto left, auto right) {
                return curves[left].id < curves[right].id;
            });
            const std::size_t next = previous_curve && candidates.front() == *previous_curve
                ? candidates.back() : candidates.front();
            if (!visited.insert(next).second) {
                throw std::runtime_error(
                    "Curved Extrusion profile contains multiple loops");
            }
            auto exact_curve = curves[next].curve;
            const bool forward = curves[next].start_node == current_node;
            if (!forward) {
                std::visit([](auto& curve) {
                    std::swap(curve.start, curve.end);
                    if constexpr (std::is_same_v<std::decay_t<decltype(curve)>,
                                      zima::kernel::ExtrusionRequest::EllipticalArcCurve>) {
                        const double old_start = curve.start_parameter;
                        const double old_end = curve.end_parameter;
                        curve.reversed = !curve.reversed;
                        curve.start_parameter = -old_end;
                        curve.end_parameter = -old_start;
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(curve)>,
                                      zima::kernel::ExtrusionRequest::BSplineCurve>) {
                        std::reverse(curve.control_points.begin(),
                                     curve.control_points.end());
                    }
                },
                           exact_curve);
            }
            ordered.curves.push_back(std::move(exact_curve));
            current_node = forward
                ? curves[next].end_node : curves[next].start_node;
            previous_curve = next;
        } while (current_node != first_node);
        if (visited.size() != curves.size()) {
            throw std::runtime_error(
                "Curved Extrusion profile contains disconnected loops");
        }
        request.outer_profile = std::move(ordered);
        for (const auto* circle : profile_circles) {
            const auto* center = sketch.find_point(circle->center_point_id);
            request.inner_profiles.push_back(
                zima::kernel::ExtrusionRequest::CircleProfile{
                    sketch.world_point(center->x, center->y), circle->radius});
        }
        return finalize(std::move(request));
    }
    const auto circle_profile = [&](const auto* circle) {
        const auto* center = sketch.find_point(circle->center_point_id);
        return zima::kernel::ExtrusionRequest::CircleProfile{
            sketch.world_point(center->x, center->y), circle->radius};
    };
    if (profile_segments.empty()) {
        if (profile_circles.empty()) {
            throw std::runtime_error("Extrusion profile has no closed geometry");
        }
        const auto outer = *std::max_element(
            profile_circles.begin(), profile_circles.end(),
            [](const auto* left, const auto* right) {
                return left->radius < right->radius;
            });
        const auto* outer_center = sketch.find_point(outer->center_point_id);
        request.outer_profile = circle_profile(outer);
        std::vector<const zima::sketcher::SketchCircle*> holes;
        for (const auto* circle : profile_circles) {
            if (circle == outer) continue;
            const auto* center = sketch.find_point(circle->center_point_id);
            const double distance = std::hypot(
                center->x - outer_center->x, center->y - outer_center->y);
            if (distance + circle->radius >= outer->radius - 1.0e-9) {
                throw std::runtime_error(
                    "Circular extrusion loops must be strictly nested");
            }
            holes.push_back(circle);
            request.inner_profiles.push_back(circle_profile(circle));
        }
        for (std::size_t first = 0; first < holes.size(); ++first) {
            const auto* first_center = sketch.find_point(holes[first]->center_point_id);
            for (std::size_t second = first + 1; second < holes.size(); ++second) {
                const auto* second_center =
                    sketch.find_point(holes[second]->center_point_id);
                if (std::hypot(first_center->x - second_center->x,
                               first_center->y - second_center->y) <=
                    holes[first]->radius + holes[second]->radius + 1.0e-9) {
                    throw std::runtime_error(
                        "Extrusion holes must not overlap or contain each other");
                }
            }
        }
        return finalize(std::move(request));
    }
    if (profile_segments.size() < 3) {
        throw std::runtime_error("Extrusion profile requires at least three segments");
    }
    std::unordered_map<std::string, std::vector<const zima::sketcher::SketchSegment*>>
        incident;
    for (const auto* segment : profile_segments) {
        incident[segment->first_point_id].push_back(segment);
        incident[segment->second_point_id].push_back(segment);
    }
    for (const auto& [point_id, segments] : incident) {
        if (segments.size() != 2) {
            throw std::runtime_error(
                "Extrusion profile must be one closed non-branching loop");
        }
    }
    std::string current = std::min_element(
        incident.begin(), incident.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        })->first;
    const std::string start = current;
    const zima::sketcher::SketchSegment* previous{};
    std::unordered_set<std::string> visited_segments;
    std::vector<std::array<double, 2>> polygon;
    do {
        const auto* point = sketch.find_point(current);
        polygon.push_back({point->x, point->y});
        std::get<zima::kernel::ExtrusionRequest::PolygonProfile>(
            request.outer_profile)
            .vertices.push_back(sketch.world_point(point->x, point->y));
        auto candidates = incident.at(current);
        std::sort(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
            return left->id < right->id;
        });
        const auto* next = candidates.front() == previous
            ? candidates.back() : candidates.front();
        if (!visited_segments.insert(next->id).second) {
            throw std::runtime_error("Extrusion profile contains multiple loops");
        }
        current = next->first_point_id == current
            ? next->second_point_id : next->first_point_id;
        previous = next;
    } while (current != start);
    if (visited_segments.size() != profile_segments.size()) {
        throw std::runtime_error("Extrusion profile contains disconnected loops");
    }
    const auto orientation = [](const auto& first, const auto& second,
                                const auto& third) {
        return (second[0] - first[0]) * (third[1] - first[1]) -
            (second[1] - first[1]) * (third[0] - first[0]);
    };
    const auto lies_on_segment = [](const auto& point, const auto& first,
                                    const auto& second) {
        return point[0] >= std::min(first[0], second[0]) - 1.0e-9 &&
            point[0] <= std::max(first[0], second[0]) + 1.0e-9 &&
            point[1] >= std::min(first[1], second[1]) - 1.0e-9 &&
            point[1] <= std::max(first[1], second[1]) + 1.0e-9;
    };
    for (std::size_t first = 0; first < polygon.size(); ++first) {
        const std::size_t first_next = (first + 1) % polygon.size();
        for (std::size_t second = first + 1; second < polygon.size(); ++second) {
            const std::size_t second_next = (second + 1) % polygon.size();
            if (first == second_next || first_next == second) continue;
            const double first_a = orientation(
                polygon[first], polygon[first_next], polygon[second]);
            const double first_b = orientation(
                polygon[first], polygon[first_next], polygon[second_next]);
            const double second_a = orientation(
                polygon[second], polygon[second_next], polygon[first]);
            const double second_b = orientation(
                polygon[second], polygon[second_next], polygon[first_next]);
            const bool proper_crossing =
                ((first_a > 1.0e-9 && first_b < -1.0e-9) ||
                 (first_a < -1.0e-9 && first_b > 1.0e-9)) &&
                ((second_a > 1.0e-9 && second_b < -1.0e-9) ||
                 (second_a < -1.0e-9 && second_b > 1.0e-9));
            const bool touching =
                (std::abs(first_a) <= 1.0e-9 && lies_on_segment(
                    polygon[second], polygon[first], polygon[first_next])) ||
                (std::abs(first_b) <= 1.0e-9 && lies_on_segment(
                    polygon[second_next], polygon[first], polygon[first_next])) ||
                (std::abs(second_a) <= 1.0e-9 && lies_on_segment(
                    polygon[first], polygon[second], polygon[second_next])) ||
                (std::abs(second_b) <= 1.0e-9 && lies_on_segment(
                    polygon[first_next], polygon[second], polygon[second_next]));
            if (proper_crossing || touching) {
                throw std::runtime_error(
                    "Extrusion outer profile must not self-intersect");
            }
        }
    }
    double signed_area{};
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto& first = polygon[index];
        const auto& second = polygon[(index + 1) % polygon.size()];
        signed_area += first[0] * second[1] - second[0] * first[1];
    }
    if (std::abs(signed_area) <= 1.0e-12) {
        throw std::runtime_error("Extrusion outer profile has zero area");
    }
    if (signed_area < 0.0) {
        std::reverse(polygon.begin(), polygon.end());
        auto& vertices =
            std::get<zima::kernel::ExtrusionRequest::PolygonProfile>(
                request.outer_profile).vertices;
        std::reverse(vertices.begin(), vertices.end());
    }
    const auto circle_inside_polygon = [&](const auto* circle) {
        const auto* center = sketch.find_point(circle->center_point_id);
        bool inside = false;
        double minimum_edge_distance = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const auto& first = polygon[index];
            const auto& second = polygon[(index + 1) % polygon.size()];
            if ((first[1] > center->y) != (second[1] > center->y) &&
                center->x < (second[0] - first[0]) * (center->y - first[1]) /
                        (second[1] - first[1]) + first[0]) {
                inside = !inside;
            }
            const double dx = second[0] - first[0];
            const double dy = second[1] - first[1];
            const double length_squared = dx * dx + dy * dy;
            if (length_squared <= 1.0e-18) {
                throw std::runtime_error(
                    "Extrusion profile contains a zero-length segment");
            }
            const double parameter = std::clamp(
                ((center->x - first[0]) * dx + (center->y - first[1]) * dy) /
                    length_squared,
                0.0, 1.0);
            minimum_edge_distance = std::min(minimum_edge_distance, std::hypot(
                center->x - (first[0] + parameter * dx),
                center->y - (first[1] + parameter * dy)));
        }
        return inside && minimum_edge_distance > circle->radius + 1.0e-9;
    };
    for (const auto* circle : profile_circles) {
        if (!circle_inside_polygon(circle)) {
            throw std::runtime_error(
                "Circular extrusion hole must lie strictly inside the outer loop");
        }
        request.inner_profiles.push_back(circle_profile(circle));
    }
    for (std::size_t first = 0; first < profile_circles.size(); ++first) {
        const auto* first_center =
            sketch.find_point(profile_circles[first]->center_point_id);
        for (std::size_t second = first + 1; second < profile_circles.size(); ++second) {
            const auto* second_center =
                sketch.find_point(profile_circles[second]->center_point_id);
            if (std::hypot(first_center->x - second_center->x,
                           first_center->y - second_center->y) <=
                profile_circles[first]->radius +
                    profile_circles[second]->radius + 1.0e-9) {
                throw std::runtime_error("Extrusion holes must not overlap");
            }
        }
    }
    return finalize(std::move(request));
}

zima::kernel::RevolutionRequest revolution_request(
    const zima::sketcher::Sketch& sketch,
    RevolutionAxis axis, double angle_degrees) {
    if (!std::isfinite(angle_degrees) || angle_degrees <= 0.0 ||
        angle_degrees > 360.0) {
        throw std::runtime_error("Revolution angle must be in (0, 360] degrees");
    }
    const auto source = extrusion_request(
        sketch, 1.0, ExtrusionDirection::Forward);
    zima::kernel::RevolutionRequest request;
    request.outer_profile = source.outer_profile;
    request.inner_profiles = source.inner_profiles;
    request.profile_normal = source.direction;
    request.axis_point = sketch.world_point(0.0, 0.0);
    if (axis == RevolutionAxis::SketchX) {
        request.axis_direction = sketch.plane == zima::sketcher::SketchPlane::YZ
            ? zima::kernel::Vec3{0.0, 1.0, 0.0}
            : zima::kernel::Vec3{1.0, 0.0, 0.0};
    } else if (axis == RevolutionAxis::SketchY) {
        request.axis_direction = sketch.plane == zima::sketcher::SketchPlane::XY
            ? zima::kernel::Vec3{0.0, 1.0, 0.0}
            : zima::kernel::Vec3{0.0, 0.0, 1.0};
    } else {
        throw std::runtime_error("Invalid Revolution axis");
    }
    request.angle_degrees = angle_degrees;
    return request;
}

}  // namespace

PartDocument PartDocument::create_default() {
    PartDocument document;
    document.document_id = make_id();
    return document;
}

HistoryContainer PartDocument::create_box_container() {
    HistoryContainer container;
    container.id = make_id();
    return container;
}

HistoryContainer PartDocument::create_cylinder_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Válec";
    container.feature_kind = FeatureKind::Cylinder;
    return container;
}

HistoryContainer PartDocument::create_sphere_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Koule";
    container.feature_kind = FeatureKind::Sphere;
    return container;
}

HistoryContainer PartDocument::create_cone_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Kužel";
    container.feature_kind = FeatureKind::Cone;
    return container;
}

HistoryContainer PartDocument::create_pyramid_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Jehlan";
    container.feature_kind = FeatureKind::Pyramid;
    return container;
}

HistoryContainer PartDocument::create_wedge_container() {
    HistoryContainer container;
    container.id = make_id();
    container.name = "Klín";
    container.feature_kind = FeatureKind::Wedge;
    return container;
}

ConstructionObject PartDocument::create_construction(ConstructionKind kind) {
    ConstructionObject object;
    object.id = make_id();
    object.kind = kind;
    object.name = kind == ConstructionKind::Point ? "Konstrukční bod"
        : kind == ConstructionKind::Axis ? "Konstrukční osa"
                                         : "Konstrukční rovina";
    return object;
}

ConstructionObject* PartDocument::find_construction(const std::string& id) {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

const ConstructionObject* PartDocument::find_construction(
    const std::string& id) const {
    const auto found = std::find_if(constructions.begin(), constructions.end(),
        [&](const auto& object) { return object.id == id; });
    return found == constructions.end() ? nullptr : &*found;
}

zima::kernel::ViewerMesh PartDocument::construction_viewer_mesh() const {
    zima::kernel::ViewerMesh mesh;
    const auto normalized = [](zima::kernel::Vec3 value) {
        const double length = std::sqrt(value.x * value.x + value.y * value.y +
                                        value.z * value.z);
        return zima::kernel::Vec3{value.x / length, value.y / length,
                                  value.z / length};
    };
    const auto cross = [](const auto& a, const auto& b) {
        return zima::kernel::Vec3{a.y * b.z - a.z * b.y,
                                  a.z * b.x - a.x * b.z,
                                  a.x * b.y - a.y * b.x};
    };
    for (const auto& object : constructions) {
        if (object.kind == ConstructionKind::Point) {
            mesh.points.push_back({object.origin, {object.id, "point", {}}});
            continue;
        }
        const auto normal = normalized(object.direction);
        if (object.kind == ConstructionKind::Axis) {
            mesh.axes.push_back({object.origin, normal, object.display_size,
                                 {object.id, "axis", {}}});
            continue;
        }
        const auto helper = std::abs(normal.z) < 0.9
            ? zima::kernel::Vec3{0.0, 0.0, 1.0}
            : zima::kernel::Vec3{0.0, 1.0, 0.0};
        const auto first = normalized(cross(normal, helper));
        const auto second = normalized(cross(normal, first));
        const double half = object.display_size * 0.5;
        std::array<zima::kernel::Vec3, 4> corners;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            const double a = index == 0 || index == 3 ? -half : half;
            const double b = index < 2 ? -half : half;
            corners[index] = {object.origin.x + a * first.x + b * second.x,
                              object.origin.y + a * first.y + b * second.y,
                              object.origin.z + a * first.z + b * second.z};
        }
        mesh.edges.push_back({{corners[0], corners[1], corners[2], corners[3],
                               corners[0]}, {object.id, "border", {}}, false, true});
        auto& references = mesh.original_references;
        const auto offset = static_cast<std::uint32_t>(references.vertices.size());
        references.vertices.insert(references.vertices.end(), corners.begin(), corners.end());
        references.triangles.insert(references.triangles.end(),
            {offset, offset + 1, offset + 2, offset, offset + 2, offset + 3});
        references.triangle_references.insert(references.triangle_references.end(),
            2, {object.id, "plane", {}});
    }
    return mesh;
}

std::vector<zima::kernel::ViewerEdge> PartDocument::extrusion_preview_edges(
    const HistoryContainer& container, double through_all_span) const {
    if (container.feature_kind != FeatureKind::Extrusion) return {};
    const auto sketch = std::find_if(sketches.begin(), sketches.end(),
        [&](const auto& value) { return value.id == container.extrusion.sketch_id; });
    if (sketch == sketches.end()) return {};
    auto request = extrusion_request(
        *sketch, container.extrusion.height, container.extrusion.direction);
    const double length = std::sqrt(request.direction.x * request.direction.x +
                                    request.direction.y * request.direction.y +
                                    request.direction.z * request.direction.z);
    const zima::kernel::Vec3 unit{request.direction.x / length,
                                  request.direction.y / length,
                                  request.direction.z / length};
    const auto surface_distance = [&](const zima::kernel::Vec3& point) {
        const auto& triangles = container.extrusion.target_surface_triangles;
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < triangles.size(); index += 3) {
            const auto& v0 = triangles[index];
            const auto& v1 = triangles[index + 1];
            const auto& v2 = triangles[index + 2];
            const zima::kernel::Vec3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
            const zima::kernel::Vec3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
            const zima::kernel::Vec3 h{unit.y * edge2.z - unit.z * edge2.y,
                                       unit.z * edge2.x - unit.x * edge2.z,
                                       unit.x * edge2.y - unit.y * edge2.x};
            const double determinant = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
            if (std::abs(determinant) <= 1e-12) continue;
            const double inverse = 1.0 / determinant;
            const zima::kernel::Vec3 s{point.x - v0.x, point.y - v0.y, point.z - v0.z};
            const double u = inverse * (s.x * h.x + s.y * h.y + s.z * h.z);
            if (u < -1e-9 || u > 1.0 + 1e-9) continue;
            const zima::kernel::Vec3 q{s.y * edge1.z - s.z * edge1.y,
                                       s.z * edge1.x - s.x * edge1.z,
                                       s.x * edge1.y - s.y * edge1.x};
            const double v = inverse * (unit.x * q.x + unit.y * q.y + unit.z * q.z);
            if (v < -1e-9 || u + v > 1.0 + 1e-9) continue;
            const double distance = inverse *
                (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
            if (distance <= 1e-9) continue;
            if (distance < nearest - 1e-7) {
                nearest = distance;
            }
        }
        if (!std::isfinite(nearest)) {
            throw std::runtime_error("Extrusion profile misses target surface");
        }
        return nearest;
    };
    const auto endpoint = [&](const zima::kernel::Vec3& point) {
        if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
            const auto& normal = container.extrusion.target_plane_normal;
            const double denominator = unit.x * normal.x + unit.y * normal.y +
                                       unit.z * normal.z;
            if (std::abs(denominator) <= 1e-12) {
                throw std::runtime_error("Extrusion direction is parallel to target plane");
            }
            const double distance =
                ((container.extrusion.target_plane_origin.x - point.x) * normal.x +
                 (container.extrusion.target_plane_origin.y - point.y) * normal.y +
                 (container.extrusion.target_plane_origin.z - point.z) * normal.z) /
                denominator;
            if (!std::isfinite(distance) || distance <= 1e-9) {
                throw std::runtime_error("Extrusion profile crosses target plane");
            }
            return zima::kernel::Vec3{point.x + unit.x * distance,
                                      point.y + unit.y * distance,
                                      point.z + unit.z * distance};
        }
        if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
            const double distance = surface_distance(point);
            return zima::kernel::Vec3{point.x + unit.x * distance,
                                      point.y + unit.y * distance,
                                      point.z + unit.z * distance};
        }
        return zima::kernel::Vec3{point.x + request.direction.x,
                                  point.y + request.direction.y,
                                  point.z + request.direction.z};
    };
    std::vector<zima::kernel::ViewerEdge> result;
    for (const auto& source : sketch->viewer_mesh().edges) {
        if (source.points.size() < 2) continue;
        zima::kernel::ViewerEdge start = source;
        start.reference = {container.id, "preview:start", {}};
        if (container.extrusion.direction == ExtrusionDirection::Symmetric) {
            for (auto& point : start.points) {
                point.x -= 0.5 * request.direction.x;
                point.y -= 0.5 * request.direction.y;
                point.z -= 0.5 * request.direction.z;
            }
        }
        zima::kernel::ViewerEdge end;
        end.reference = {container.id, "preview:end", {}};
        end.points.reserve(source.points.size());
        if (container.extrusion.extent == ExtrusionExtent::ThroughAll) {
            start.points = source.points;
            for (auto& point : start.points) {
                point.x -= unit.x * through_all_span;
                point.y -= unit.y * through_all_span;
                point.z -= unit.z * through_all_span;
                end.points.push_back({point.x + unit.x * 2.0 * through_all_span,
                                      point.y + unit.y * 2.0 * through_all_span,
                                      point.z + unit.z * 2.0 * through_all_span});
            }
        } else {
            for (const auto& point : start.points) end.points.push_back(endpoint(point));
        }
        result.push_back(start);
        result.push_back(end);
        const std::size_t samples[] = {0, source.points.size() - 1};
        for (const auto index : samples) {
            result.push_back({{start.points[index], end.points[index]},
                              {container.id, "preview:side", {}}});
        }
    }
    return result;
}

HistoryContainer PartDocument::create_extrusion_container(std::string sketch_id) {
    if (sketch_id.empty()) throw std::invalid_argument("Extrusion Sketch ID is required");
    HistoryContainer container;
    container.id = make_id();
    container.name = "Vytažení";
    container.feature_kind = FeatureKind::Extrusion;
    container.extrusion.sketch_id = std::move(sketch_id);
    return container;
}

HistoryContainer PartDocument::create_revolution_container(std::string sketch_id) {
    if (sketch_id.empty()) throw std::invalid_argument("Revolution Sketch ID is required");
    HistoryContainer container;
    container.id = make_id();
    container.name = "Rotace";
    container.feature_kind = FeatureKind::Revolution;
    container.revolution.sketch_id = std::move(sketch_id);
    return container;
}

HistoryContainer PartDocument::create_fillet_container(
    std::vector<zima::kernel::EdgeReference> edges) {
    if (edges.empty() || std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return !edge.valid() || !edge.instance_path.empty();
        })) {
        throw std::invalid_argument("Fillet requires a local original edge reference");
    }
    HistoryContainer container;
    container.id = make_id();
    container.name = "Zaoblení";
    container.feature_kind = FeatureKind::Fillet;
    container.edge_treatment.edges = std::move(edges);
    return container;
}

HistoryContainer PartDocument::create_chamfer_container(
    std::vector<zima::kernel::EdgeReference> edges) {
    if (edges.empty() || std::any_of(edges.begin(), edges.end(), [](const auto& edge) {
            return !edge.valid() || !edge.instance_path.empty();
        })) {
        throw std::invalid_argument("Chamfer requires a local original edge reference");
    }
    HistoryContainer container;
    container.id = make_id();
    container.name = "Sražení";
    container.feature_kind = FeatureKind::Chamfer;
    container.edge_treatment.edges = std::move(edges);
    return container;
}

HistoryContainer PartDocument::create_imported_step_container(
    std::filesystem::path source_path, std::string component_path,
    std::string component_name) {
    if (source_path.empty()) throw std::invalid_argument("STEP source path is empty");
    HistoryContainer container;
    container.id = make_id();
    container.name = component_name.empty() ? source_path.stem().string()
                                            : std::move(component_name);
    container.feature_kind = FeatureKind::ImportedStep;
    container.imported_step.source_path = source_path.generic_string();
    container.imported_step.component_path = std::move(component_path);
    return container;
}

HistoryContainer* PartDocument::find_container(const std::string& id) {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    return found == history.end() ? nullptr : &*found;
}

const HistoryContainer* PartDocument::find_container(const std::string& id) const {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    return found == history.end() ? nullptr : &*found;
}

std::optional<std::size_t> PartDocument::history_index(
    const std::string& id) const {
    const auto found = std::find_if(history.begin(), history.end(),
        [&](const HistoryContainer& container) { return container.id == id; });
    if (found == history.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(history.begin(), found));
}

std::vector<zima::kernel::HistoryOperation> PartDocument::kernel_operations() const {
    std::vector<zima::kernel::HistoryOperation> operations;
    operations.reserve(history.size());
    for (const auto& container : history) {
        zima::kernel::Vec3 translation{
            container.placement.x, container.placement.y, container.placement.z};
        zima::kernel::Vec3 rotation{
            container.placement.rotation_x, container.placement.rotation_y,
            container.placement.rotation_z};
        zima::kernel::PrimitiveRequest primitive;
        if (container.feature_kind == FeatureKind::Box) {
            zima::kernel::BoxRequest box{
                container.box.length, container.box.width, container.box.height};
            box.translation = translation;
            box.rotation_degrees = rotation;
            primitive = box;
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            zima::kernel::CylinderRequest cylinder;
            cylinder.radius = container.cylinder.radius;
            cylinder.height = container.cylinder.height;
            cylinder.translation = translation;
            cylinder.rotation_degrees = rotation;
            primitive = cylinder;
        } else if (container.feature_kind == FeatureKind::Sphere) {
            zima::kernel::SphereRequest sphere;
            sphere.radius = container.sphere.radius;
            sphere.translation = translation;
            sphere.rotation_degrees = rotation;
            primitive = sphere;
        } else if (container.feature_kind == FeatureKind::Cone) {
            zima::kernel::ConeRequest cone;
            cone.bottom_radius = container.cone.bottom_radius;
            cone.top_radius = container.cone.top_radius;
            cone.height = container.cone.height;
            cone.translation = translation;
            cone.rotation_degrees = rotation;
            primitive = cone;
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            zima::kernel::PyramidRequest pyramid;
            pyramid.length = container.pyramid.length;
            pyramid.width = container.pyramid.width;
            pyramid.height = container.pyramid.height;
            pyramid.translation = translation;
            pyramid.rotation_degrees = rotation;
            primitive = pyramid;
        } else if (container.feature_kind == FeatureKind::Wedge) {
            zima::kernel::WedgeRequest wedge;
            wedge.length = container.wedge.length;
            wedge.width = container.wedge.width;
            wedge.height = container.wedge.height;
            wedge.top_offset = container.wedge.top_offset;
            wedge.translation = translation;
            wedge.rotation_degrees = rotation;
            primitive = wedge;
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            require_default_sketch_feature_placement(container.placement);
            const auto sketch = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& value) {
                    return value.id == container.extrusion.sketch_id;
                });
            if (sketch == sketches.end()) {
                throw std::runtime_error("Extrusion references a missing Sketch");
            }
            auto extrusion = extrusion_request(
                *sketch, container.extrusion.height,
                container.extrusion.direction);
            extrusion.extent = container.extrusion.extent == ExtrusionExtent::UpToPlane
                ? zima::kernel::ExtrusionRequest::Extent::UpToPlane
                : container.extrusion.extent == ExtrusionExtent::UpToSurface
                    ? zima::kernel::ExtrusionRequest::Extent::UpToSurface
                : container.extrusion.extent == ExtrusionExtent::ThroughAll
                    ? zima::kernel::ExtrusionRequest::Extent::ThroughAll
                    : zima::kernel::ExtrusionRequest::Extent::Blind;
            extrusion.target_face = container.extrusion.target_face;
            extrusion.target_is_datum = std::any_of(
                constructions.begin(), constructions.end(), [&](const auto& object) {
                    return object.id == container.extrusion.target_face.owner_id &&
                           object.kind == ConstructionKind::Plane;
                });
            if ((container.extrusion.extent == ExtrusionExtent::UpToPlane ||
                 container.extrusion.extent == ExtrusionExtent::UpToSurface) &&
                !extrusion.target_is_datum &&
                std::none_of(operations.begin(), operations.end(), [&](const auto& prior) {
                    return prior.owner_id == container.extrusion.target_face.owner_id;
                })) {
                throw std::runtime_error(
                    "Extrusion target must belong to prior history or a datum plane");
            }
            extrusion.target_plane_origin = container.extrusion.target_plane_origin;
            extrusion.target_plane_normal = container.extrusion.target_plane_normal;
            extrusion.target_surface_triangles =
                container.extrusion.target_surface_triangles;
            primitive = std::move(extrusion);
        } else if (container.feature_kind == FeatureKind::Revolution) {
            require_default_sketch_feature_placement(container.placement);
            const auto sketch = std::find_if(sketches.begin(), sketches.end(),
                [&](const auto& value) {
                    return value.id == container.revolution.sketch_id;
                });
            if (sketch == sketches.end()) {
                throw std::runtime_error("Revolution references a missing Sketch");
            }
            primitive = revolution_request(
                *sketch, container.revolution.axis,
                container.revolution.angle_degrees);
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            zima::kernel::StepRequest step{
                container.imported_step.source_path,
                container.imported_step.component_path};
            primitive = std::move(step);
        } else if (container.feature_kind == FeatureKind::Fillet) {
            require_default_sketch_feature_placement(container.placement);
            primitive = zima::kernel::FilletRequest{
                container.edge_treatment.edges,
                container.edge_treatment.origin,
                container.edge_treatment.size};
        } else {
            require_default_sketch_feature_placement(container.placement);
            primitive = zima::kernel::ChamferRequest{
                container.edge_treatment.edges,
                container.edge_treatment.origin,
                container.edge_treatment.size};
        }
        operations.push_back({
            container.id,
            std::move(primitive),
            container.combine_mode == CombineMode::Subtract
                ? zima::kernel::BooleanOperation::Subtract
                : zima::kernel::BooleanOperation::Add,
        });
    }
    return operations;
}

PartDocument PartDocument::load(
    const std::filesystem::path& path,
    std::vector<zima::kernel::BodyResult>* calculated_boundaries) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open document: " + path.string());
    }
    nlohmann::json root;
    input >> root;
    if (root.at("format").get<std::string>() != "zima-cad-cpp" ||
        root.at("format_version").get<int>() != 14) {
        throw std::runtime_error("Unsupported ZIMA-CAD Part document format");
    }
    PartDocument document;
    document.document_id = root.at("document_id").get<std::string>();
    document.name = root.at("name").get<std::string>();
    const auto& source_history = root.at("history");
    if (!source_history.is_array()) {
        throw std::runtime_error("Document history must be an array");
    }
    std::unordered_set<std::string> container_ids;
    for (const auto& source : source_history) {
        const std::string type = source.at("type").get<std::string>();
        if (type != "box" && type != "cylinder" && type != "sphere" &&
            type != "cone" && type != "pyramid" && type != "wedge" &&
            type != "extrusion" &&
            type != "revolution" && type != "imported_step" &&
            type != "fillet" && type != "chamfer") {
            throw std::runtime_error("Unsupported history feature type");
        }
        HistoryContainer container;
        container.feature_kind = type == "cylinder" ? FeatureKind::Cylinder
            : type == "sphere" ? FeatureKind::Sphere
            : type == "cone" ? FeatureKind::Cone
            : type == "pyramid" ? FeatureKind::Pyramid
            : type == "wedge" ? FeatureKind::Wedge
            : type == "extrusion" ? FeatureKind::Extrusion
            : type == "revolution" ? FeatureKind::Revolution
            : type == "imported_step" ? FeatureKind::ImportedStep
            : type == "fillet" ? FeatureKind::Fillet
            : type == "chamfer" ? FeatureKind::Chamfer : FeatureKind::Box;
        container.id = source.at("id").get<std::string>();
        container.name = source.at("name").get<std::string>();
        if (container.id.empty() || !container_ids.insert(container.id).second) {
            throw std::runtime_error("History container IDs must be non-empty and unique");
        }
        if (container.name.empty()) {
            throw std::runtime_error("History container name must not be empty");
        }
        const std::string combine = source.value("combine", "add");
        if (combine != "add" && combine != "subtract") {
            throw std::runtime_error("Invalid history combination mode");
        }
        container.combine_mode = combine == "subtract"
            ? CombineMode::Subtract : CombineMode::Add;
        if (container.feature_kind == FeatureKind::Box) {
            container.box.length = source.at("length").get<double>();
            container.box.width = source.at("width").get<double>();
            container.box.height = source.at("height").get<double>();
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            container.cylinder.radius = source.at("radius").get<double>();
            container.cylinder.height = source.at("height").get<double>();
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        } else if (container.feature_kind == FeatureKind::Sphere) {
            container.sphere.radius = source.at("radius").get<double>();
            require_positive(container.sphere.radius, "radius");
        } else if (container.feature_kind == FeatureKind::Cone) {
            container.cone.bottom_radius = source.at("bottom_radius").get<double>();
            container.cone.top_radius = source.at("top_radius").get<double>();
            container.cone.height = source.at("height").get<double>();
            if (!std::isfinite(container.cone.bottom_radius) ||
                !std::isfinite(container.cone.top_radius) ||
                container.cone.bottom_radius < 0.0 || container.cone.top_radius < 0.0 ||
                (container.cone.bottom_radius <= 0.0 && container.cone.top_radius <= 0.0)) {
                throw std::runtime_error("Invalid Cone radii");
            }
            require_positive(container.cone.height, "height");
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            container.pyramid = {source.at("length").get<double>(),
                source.at("width").get<double>(), source.at("height").get<double>()};
            require_positive(container.pyramid.length, "length");
            require_positive(container.pyramid.width, "width");
            require_positive(container.pyramid.height, "height");
        } else if (container.feature_kind == FeatureKind::Wedge) {
            container.wedge = {source.at("length").get<double>(),
                source.at("width").get<double>(), source.at("height").get<double>(),
                source.at("top_offset").get<double>()};
            require_positive(container.wedge.length, "length");
            require_positive(container.wedge.width, "width");
            require_positive(container.wedge.height, "height");
            if (!std::isfinite(container.wedge.top_offset) ||
                container.wedge.top_offset < 0.0 ||
                container.wedge.top_offset > container.wedge.length) {
                throw std::runtime_error("Invalid Wedge top offset");
            }
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            container.extrusion.sketch_id = source.at("sketch_id").get<std::string>();
            container.extrusion.height = source.at("height").get<double>();
            const std::string direction =
                source.at("direction").get<std::string>();
            if (direction == "forward") {
                container.extrusion.direction = ExtrusionDirection::Forward;
            } else if (direction == "reverse") {
                container.extrusion.direction = ExtrusionDirection::Reverse;
            } else if (direction == "symmetric") {
                container.extrusion.direction = ExtrusionDirection::Symmetric;
            } else {
                throw std::runtime_error("Invalid Extrusion direction");
            }
            if (container.extrusion.sketch_id.empty()) {
                throw std::runtime_error("Extrusion Sketch ID is required");
            }
            require_positive(container.extrusion.height, "extrusion height");
            validate_extrusion_direction(container.extrusion.direction);
            const std::string extent = source.at("extent").get<std::string>();
            container.extrusion.extent = extent == "up_to_plane"
                ? ExtrusionExtent::UpToPlane
                : extent == "up_to_surface" ? ExtrusionExtent::UpToSurface
                : extent == "through_all" ? ExtrusionExtent::ThroughAll
                : extent == "blind" ? ExtrusionExtent::Blind
                : throw std::runtime_error("Invalid Extrusion extent");
            if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
                if (container.extrusion.direction == ExtrusionDirection::Symmetric) {
                    throw std::runtime_error(
                        "Up-to-plane Extrusion requires forward or reverse direction");
                }
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(), {}};
                container.extrusion.target_plane_origin = {
                    source.at("target_origin_x").get<double>(),
                    source.at("target_origin_y").get<double>(),
                    source.at("target_origin_z").get<double>()};
                container.extrusion.target_plane_normal = {
                    source.at("target_normal_x").get<double>(),
                    source.at("target_normal_y").get<double>(),
                    source.at("target_normal_z").get<double>()};
                const auto& normal = container.extrusion.target_plane_normal;
                const double normal_length = std::sqrt(
                    normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (!container.extrusion.target_face.valid() || normal_length <= 1e-12) {
                    throw std::runtime_error("Invalid Extrusion target plane");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
                container.extrusion.target_face = {
                    source.at("target_owner").get<std::string>(),
                    source.at("target_key").get<std::string>(), {}};
                for (const auto& point : source.at("target_triangles")) {
                    container.extrusion.target_surface_triangles.push_back({
                        point.at(0).get<double>(), point.at(1).get<double>(),
                        point.at(2).get<double>()});
                }
                if (!container.extrusion.target_face.valid() ||
                    container.extrusion.target_surface_triangles.empty() ||
                    container.extrusion.target_surface_triangles.size() % 3 != 0) {
                    throw std::runtime_error("Invalid Extrusion target surface");
                }
            }
            if (container.extrusion.extent == ExtrusionExtent::ThroughAll &&
                container.combine_mode != CombineMode::Subtract) {
                throw std::runtime_error("Through-all Extrusion must subtract");
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            container.revolution.sketch_id =
                source.at("sketch_id").get<std::string>();
            container.revolution.angle_degrees =
                source.at("angle_degrees").get<double>();
            const std::string axis = source.at("axis").get<std::string>();
            if (axis == "sketch_x") {
                container.revolution.axis = RevolutionAxis::SketchX;
            } else if (axis == "sketch_y") {
                container.revolution.axis = RevolutionAxis::SketchY;
            } else {
                throw std::runtime_error("Invalid Revolution axis");
            }
            if (container.revolution.sketch_id.empty() ||
                !std::isfinite(container.revolution.angle_degrees) ||
                container.revolution.angle_degrees <= 0.0 ||
                container.revolution.angle_degrees > 360.0) {
                throw std::runtime_error("Invalid Revolution parameters");
            }
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            container.imported_step.source_path = source.at("source_path").get<std::string>();
            container.imported_step.component_path =
                source.at("component_path").get<std::string>();
            if (container.imported_step.source_path.empty() ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid imported STEP parameters");
            }
        } else {
            for (const auto& edge : source.at("edges")) {
                container.edge_treatment.edges.push_back({
                    edge.at("owner").get<std::string>(),
                    edge.at("key").get<std::string>(), {}});
            }
            const std::string origin = source.at("edge_origin").get<std::string>();
            if (origin == "original_entity") {
                container.edge_treatment.origin =
                    zima::kernel::EdgeSelectionOrigin::OriginalEntity;
            } else if (origin == "operational_body") {
                container.edge_treatment.origin =
                    zima::kernel::EdgeSelectionOrigin::OperationalBody;
            } else {
                throw std::runtime_error("Invalid edge treatment origin");
            }
            container.edge_treatment.size = source.at("size").get<double>();
            if (container.edge_treatment.edges.empty() ||
                std::any_of(container.edge_treatment.edges.begin(),
                    container.edge_treatment.edges.end(), [](const auto& edge) {
                        return !edge.valid() || !edge.instance_path.empty();
                    }) ||
                !std::isfinite(container.edge_treatment.size) ||
                container.edge_treatment.size <= 0.0 ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid Fillet/Chamfer parameters");
            }
        }
        if (source.contains("placement")) {
            const auto& placement = source.at("placement");
            container.placement.x = placement.value("x", 0.0);
            container.placement.y = placement.value("y", 0.0);
            container.placement.z = placement.value("z", 0.0);
            container.placement.rotation_x = placement.value("rotation_x", 0.0);
            container.placement.rotation_y = placement.value("rotation_y", 0.0);
            container.placement.rotation_z = placement.value("rotation_z", 0.0);
        }
        validate_placement(container.placement);
        if (container.feature_kind == FeatureKind::Extrusion ||
            container.feature_kind == FeatureKind::Revolution ||
            container.feature_kind == FeatureKind::Fillet ||
            container.feature_kind == FeatureKind::Chamfer) {
            require_default_sketch_feature_placement(container.placement);
        }
        document.history.push_back(std::move(container));
    }
    std::unordered_set<std::string> sketch_ids;
    for (const auto& source : root.at("sketches")) {
        auto sketch = zima::sketcher::Sketch::from_serialized(source.dump());
        if (!sketch_ids.insert(sketch.id).second) {
            throw std::runtime_error("Sketch IDs must be unique in a Part");
        }
        document.sketches.push_back(std::move(sketch));
    }
    std::unordered_set<std::string> construction_ids;
    for (const auto& source : root.at("constructions")) {
        ConstructionObject object;
        object.id = source.at("id").get<std::string>();
        object.name = source.at("name").get<std::string>();
        const auto type = source.at("type").get<std::string>();
        object.kind = type == "point" ? ConstructionKind::Point
            : type == "axis" ? ConstructionKind::Axis
            : type == "plane" ? ConstructionKind::Plane
                               : throw std::runtime_error("Invalid construction type");
        object.origin = {source.at("x").get<double>(),
                         source.at("y").get<double>(),
                         source.at("z").get<double>()};
        object.direction = {source.at("direction_x").get<double>(),
                            source.at("direction_y").get<double>(),
                            source.at("direction_z").get<double>()};
        object.display_size = source.at("display_size").get<double>();
        const double direction_length = std::sqrt(
            object.direction.x * object.direction.x +
            object.direction.y * object.direction.y +
            object.direction.z * object.direction.z);
        if (object.id.empty() || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) || !std::isfinite(direction_length) ||
            (object.kind != ConstructionKind::Point && direction_length <= 0.0) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0) {
            throw std::runtime_error("Invalid construction object");
        }
        document.constructions.push_back(std::move(object));
    }
    for (const auto& container : document.history) {
        if (container.feature_kind == FeatureKind::Extrusion &&
            std::none_of(document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) {
                    return sketch.id == container.extrusion.sketch_id;
                })) {
            throw std::runtime_error("Extrusion references a missing Sketch");
        }
        if (container.feature_kind == FeatureKind::Revolution &&
            std::none_of(document.sketches.begin(), document.sketches.end(),
                [&](const auto& sketch) {
                    return sketch.id == container.revolution.sketch_id;
                })) {
            throw std::runtime_error("Revolution references a missing Sketch");
        }
    }
    if (!document.history.empty() &&
        document.history.front().combine_mode == CombineMode::Subtract) {
        throw std::runtime_error("The first history container cannot subtract");
    }
    std::vector<zima::kernel::BodyResult> loaded_boundaries;
    for (const auto& boundary : root.at("calculated_boundaries")) {
        loaded_boundaries.push_back(load_body_result(boundary));
    }
    if (!loaded_boundaries.empty() &&
        loaded_boundaries.size() != document.history.size()) {
        throw std::runtime_error(
            "Calculated history boundaries do not match document history");
    }
    std::unordered_set<std::string> available_owners;
    const auto expected_operations = document.kernel_operations();
    for (std::size_t boundary_index = 0;
         boundary_index < loaded_boundaries.size(); ++boundary_index) {
        available_owners.insert(document.history[boundary_index].id);
        if (loaded_boundaries[boundary_index].source_fingerprint !=
            zima::kernel::history_fingerprint(
                expected_operations, boundary_index + 1)) {
            throw std::runtime_error(
                "Calculated history boundary does not match its parameters");
        }
        const auto validate_reference = [&](const auto& reference, bool may_be_invalid) {
            const bool owner_empty = reference.owner_id.empty();
            const bool key_empty = reference.semantic_key.empty();
            if (owner_empty != key_empty || (!may_be_invalid && owner_empty) ||
                (!owner_empty && !available_owners.contains(reference.owner_id)) ||
                !reference.instance_path.empty()) {
                throw std::runtime_error(
                    "Calculated topology reference has an invalid history owner");
            }
        };
        const auto& mesh = loaded_boundaries[boundary_index].mesh;
        for (const auto& reference : mesh.triangle_references) {
            validate_reference(reference, true);
        }
        for (const auto& edge : mesh.edges) {
            validate_reference(edge.reference, true);
        }
        for (const auto& point : mesh.points) {
            validate_reference(point.reference, false);
        }
        for (const auto& axis : mesh.axes) {
            validate_reference(axis.reference, false);
        }
        for (const auto& dimension : mesh.dimensions) {
            validate_reference(dimension.reference, false);
        }
        const auto& references = mesh.original_references;
        if (references.triangles.size() % 3 != 0 ||
            references.triangle_references.size() != references.triangles.size() / 3) {
            throw std::runtime_error(
                "Calculated original-reference triangles are not aligned");
        }
        for (const auto index : references.triangles) {
            if (index >= references.vertices.size()) {
                throw std::runtime_error(
                    "Calculated original-reference triangle is invalid");
            }
        }
        for (const auto& reference : references.triangle_references) {
            validate_reference(reference, false);
        }
        for (const auto& edge : references.edges) {
            validate_reference(edge.reference, false);
        }
        for (const auto& point : references.points) {
            validate_reference(point.reference, false);
        }
        for (const auto& axis : references.axes) {
            validate_reference(axis.reference, false);
        }
    }
    if (calculated_boundaries != nullptr) {
        *calculated_boundaries = std::move(loaded_boundaries);
    }
    return document;
}

void PartDocument::save(
    const std::filesystem::path& path,
    const std::vector<zima::kernel::BodyResult>& calculated_boundaries) const {
    nlohmann::json serialized_history = nlohmann::json::array();
    std::unordered_set<std::string> container_ids;
    for (const auto& container : history) {
        if (container.id.empty() || !container_ids.insert(container.id).second) {
            throw std::runtime_error("History container IDs must be non-empty and unique");
        }
        if (container.name.empty()) {
            throw std::runtime_error("History container name must not be empty");
        }
        if (container.feature_kind == FeatureKind::Box) {
            require_positive(container.box.length, "length");
            require_positive(container.box.width, "width");
            require_positive(container.box.height, "height");
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            require_positive(container.cylinder.radius, "radius");
            require_positive(container.cylinder.height, "height");
        } else if (container.feature_kind == FeatureKind::Sphere) {
            require_positive(container.sphere.radius, "radius");
        } else if (container.feature_kind == FeatureKind::Cone) {
            if (!std::isfinite(container.cone.bottom_radius) ||
                !std::isfinite(container.cone.top_radius) ||
                container.cone.bottom_radius < 0.0 || container.cone.top_radius < 0.0 ||
                (container.cone.bottom_radius <= 0.0 && container.cone.top_radius <= 0.0)) {
                throw std::runtime_error("Invalid Cone radii");
            }
            require_positive(container.cone.height, "height");
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            require_positive(container.pyramid.length, "length");
            require_positive(container.pyramid.width, "width");
            require_positive(container.pyramid.height, "height");
        } else if (container.feature_kind == FeatureKind::Wedge) {
            require_positive(container.wedge.length, "length");
            require_positive(container.wedge.width, "width");
            require_positive(container.wedge.height, "height");
            if (!std::isfinite(container.wedge.top_offset) ||
                container.wedge.top_offset < 0.0 ||
                container.wedge.top_offset > container.wedge.length) {
                throw std::runtime_error("Invalid Wedge top offset");
            }
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            if (container.extrusion.sketch_id.empty() ||
                std::none_of(sketches.begin(), sketches.end(), [&](const auto& sketch) {
                    return sketch.id == container.extrusion.sketch_id;
                })) {
                throw std::runtime_error("Extrusion references a missing Sketch");
            }
            require_positive(container.extrusion.height, "extrusion height");
            validate_extrusion_direction(container.extrusion.direction);
            if (container.extrusion.extent == ExtrusionExtent::UpToPlane) {
                const auto& normal = container.extrusion.target_plane_normal;
                if (container.extrusion.direction == ExtrusionDirection::Symmetric ||
                    !container.extrusion.target_face.valid() ||
                    std::sqrt(normal.x * normal.x + normal.y * normal.y +
                              normal.z * normal.z) <= 1e-12) {
                    throw std::runtime_error("Invalid Extrusion target plane");
                }
            } else if (container.extrusion.extent == ExtrusionExtent::UpToSurface) {
                if (container.extrusion.direction == ExtrusionDirection::Symmetric ||
                    !container.extrusion.target_face.valid() ||
                    container.extrusion.target_surface_triangles.empty() ||
                    container.extrusion.target_surface_triangles.size() % 3 != 0) {
                    throw std::runtime_error("Invalid Extrusion target surface");
                }
            } else if (container.extrusion.extent == ExtrusionExtent::ThroughAll &&
                       container.combine_mode != CombineMode::Subtract) {
                throw std::runtime_error("Through-all Extrusion must subtract");
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            if (container.revolution.sketch_id.empty() ||
                std::none_of(sketches.begin(), sketches.end(), [&](const auto& sketch) {
                    return sketch.id == container.revolution.sketch_id;
                }) || (container.revolution.axis != RevolutionAxis::SketchX &&
                       container.revolution.axis != RevolutionAxis::SketchY) ||
                !std::isfinite(container.revolution.angle_degrees) ||
                container.revolution.angle_degrees <= 0.0 ||
                container.revolution.angle_degrees > 360.0) {
                throw std::runtime_error("Invalid Revolution parameters");
            }
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            if (container.imported_step.source_path.empty() ||
                container.combine_mode != CombineMode::Add) {
                throw std::runtime_error("Invalid imported STEP parameters");
            }
        } else if (container.edge_treatment.edges.empty() ||
                   std::any_of(container.edge_treatment.edges.begin(),
                       container.edge_treatment.edges.end(), [](const auto& edge) {
                           return !edge.valid() || !edge.instance_path.empty();
                       }) ||
                   !std::isfinite(container.edge_treatment.size) ||
                   container.edge_treatment.size <= 0.0 ||
                   container.combine_mode != CombineMode::Add) {
            throw std::runtime_error("Invalid Fillet/Chamfer parameters");
        }
        validate_placement(container.placement);
        if (container.feature_kind == FeatureKind::Extrusion ||
            container.feature_kind == FeatureKind::Revolution ||
            container.feature_kind == FeatureKind::Fillet ||
            container.feature_kind == FeatureKind::Chamfer) {
            require_default_sketch_feature_placement(container.placement);
        }
        nlohmann::json serialized = {
            {"id", container.id},
            {"type", container.feature_kind == FeatureKind::Box ? "box"
                : container.feature_kind == FeatureKind::Cylinder
                    ? "cylinder"
                : container.feature_kind == FeatureKind::Sphere
                    ? "sphere"
                : container.feature_kind == FeatureKind::Cone
                    ? "cone"
                : container.feature_kind == FeatureKind::Pyramid
                    ? "pyramid"
                : container.feature_kind == FeatureKind::Wedge
                    ? "wedge"
                : container.feature_kind == FeatureKind::Extrusion
                    ? "extrusion"
                : container.feature_kind == FeatureKind::Revolution
                    ? "revolution"
                : container.feature_kind == FeatureKind::ImportedStep
                    ? "imported_step"
                : container.feature_kind == FeatureKind::Fillet
                    ? "fillet" : "chamfer"},
            {"name", container.name},
            {"combine", container.combine_mode == CombineMode::Subtract
                ? "subtract" : "add"},
        };
        if (container.feature_kind != FeatureKind::Extrusion &&
            container.feature_kind != FeatureKind::Revolution &&
            container.feature_kind != FeatureKind::Fillet &&
            container.feature_kind != FeatureKind::Chamfer) {
            serialized["placement"] = {
                {"x", container.placement.x},
                {"y", container.placement.y},
                {"z", container.placement.z},
                {"rotation_x", container.placement.rotation_x},
                {"rotation_y", container.placement.rotation_y},
                {"rotation_z", container.placement.rotation_z},
            };
        }
        if (container.feature_kind == FeatureKind::Box) {
            serialized["length"] = container.box.length;
            serialized["width"] = container.box.width;
            serialized["height"] = container.box.height;
        } else if (container.feature_kind == FeatureKind::Cylinder) {
            serialized["radius"] = container.cylinder.radius;
            serialized["height"] = container.cylinder.height;
        } else if (container.feature_kind == FeatureKind::Sphere) {
            serialized["radius"] = container.sphere.radius;
        } else if (container.feature_kind == FeatureKind::Cone) {
            serialized["bottom_radius"] = container.cone.bottom_radius;
            serialized["top_radius"] = container.cone.top_radius;
            serialized["height"] = container.cone.height;
        } else if (container.feature_kind == FeatureKind::Pyramid) {
            serialized["length"] = container.pyramid.length;
            serialized["width"] = container.pyramid.width;
            serialized["height"] = container.pyramid.height;
        } else if (container.feature_kind == FeatureKind::Wedge) {
            serialized["length"] = container.wedge.length;
            serialized["width"] = container.wedge.width;
            serialized["height"] = container.wedge.height;
            serialized["top_offset"] = container.wedge.top_offset;
        } else if (container.feature_kind == FeatureKind::Extrusion) {
            serialized["sketch_id"] = container.extrusion.sketch_id;
            serialized["height"] = container.extrusion.height;
            serialized["direction"] =
                container.extrusion.direction == ExtrusionDirection::Forward
                    ? "forward"
                : container.extrusion.direction == ExtrusionDirection::Reverse
                    ? "reverse" : "symmetric";
            serialized["extent"] = container.extrusion.extent == ExtrusionExtent::UpToPlane
                ? "up_to_plane"
                : container.extrusion.extent == ExtrusionExtent::UpToSurface
                    ? "up_to_surface"
                : container.extrusion.extent == ExtrusionExtent::ThroughAll
                    ? "through_all" : "blind";
            serialized["target_owner"] = container.extrusion.target_face.owner_id;
            serialized["target_key"] = container.extrusion.target_face.semantic_key;
            serialized["target_origin_x"] = container.extrusion.target_plane_origin.x;
            serialized["target_origin_y"] = container.extrusion.target_plane_origin.y;
            serialized["target_origin_z"] = container.extrusion.target_plane_origin.z;
            serialized["target_normal_x"] = container.extrusion.target_plane_normal.x;
            serialized["target_normal_y"] = container.extrusion.target_plane_normal.y;
            serialized["target_normal_z"] = container.extrusion.target_plane_normal.z;
            serialized["target_triangles"] = nlohmann::json::array();
            for (const auto& point : container.extrusion.target_surface_triangles) {
                serialized["target_triangles"].push_back({point.x, point.y, point.z});
            }
        } else if (container.feature_kind == FeatureKind::Revolution) {
            serialized["sketch_id"] = container.revolution.sketch_id;
            serialized["axis"] = container.revolution.axis == RevolutionAxis::SketchX
                ? "sketch_x" : "sketch_y";
            serialized["angle_degrees"] = container.revolution.angle_degrees;
        } else if (container.feature_kind == FeatureKind::ImportedStep) {
            serialized["source_path"] = container.imported_step.source_path;
            serialized["component_path"] = container.imported_step.component_path;
        } else {
            serialized["edges"] = nlohmann::json::array();
            for (const auto& edge : container.edge_treatment.edges) {
                serialized["edges"].push_back({
                    {"owner", edge.owner_id}, {"key", edge.semantic_key}});
            }
            serialized["edge_origin"] = container.edge_treatment.origin ==
                    zima::kernel::EdgeSelectionOrigin::OriginalEntity
                ? "original_entity" : "operational_body";
            serialized["size"] = container.edge_treatment.size;
        }
        serialized_history.push_back(std::move(serialized));
    }
    if (!history.empty() && history.front().combine_mode == CombineMode::Subtract) {
        throw std::runtime_error("The first history container cannot subtract");
    }
    if (!calculated_boundaries.empty() &&
        calculated_boundaries.size() != history.size()) {
        throw std::runtime_error(
            "Calculated history boundaries do not match document history");
    }
    const auto expected_operations = kernel_operations();
    for (std::size_t index = 0; index < calculated_boundaries.size(); ++index) {
        if (calculated_boundaries[index].source_fingerprint !=
            zima::kernel::history_fingerprint(expected_operations, index + 1)) {
            throw std::runtime_error(
                "Calculated history boundary does not match its parameters");
        }
    }
    nlohmann::json serialized_boundaries = nlohmann::json::array();
    for (const auto& boundary : calculated_boundaries) {
        serialized_boundaries.push_back(serialize_body_result(boundary));
    }
    nlohmann::json serialized_sketches = nlohmann::json::array();
    std::unordered_set<std::string> sketch_ids;
    for (const auto& sketch : sketches) {
        if (sketch.id.empty() || !sketch_ids.insert(sketch.id).second) {
            throw std::runtime_error("Sketch IDs must be non-empty and unique in a Part");
        }
        serialized_sketches.push_back(nlohmann::json::parse(sketch.serialized()));
    }
    nlohmann::json serialized_constructions = nlohmann::json::array();
    std::unordered_set<std::string> construction_ids;
    for (const auto& object : constructions) {
        const double direction_length = std::sqrt(
            object.direction.x * object.direction.x +
            object.direction.y * object.direction.y +
            object.direction.z * object.direction.z);
        if (object.id.empty() || object.name.empty() ||
            !construction_ids.insert(object.id).second ||
            !std::isfinite(object.origin.x) || !std::isfinite(object.origin.y) ||
            !std::isfinite(object.origin.z) || !std::isfinite(direction_length) ||
            (object.kind != ConstructionKind::Point && direction_length <= 0.0) ||
            !std::isfinite(object.display_size) || object.display_size <= 0.0) {
            throw std::runtime_error("Invalid construction object");
        }
        serialized_constructions.push_back({
            {"id", object.id}, {"name", object.name},
            {"type", object.kind == ConstructionKind::Point ? "point"
                : object.kind == ConstructionKind::Axis ? "axis" : "plane"},
            {"x", object.origin.x}, {"y", object.origin.y}, {"z", object.origin.z},
            {"direction_x", object.direction.x},
            {"direction_y", object.direction.y},
            {"direction_z", object.direction.z},
            {"display_size", object.display_size}});
    }
    const nlohmann::json root = {
        {"format", "zima-cad-cpp"},
        {"format_version", 14},
        {"document_id", document_id},
        {"type", "part"},
        {"name", name},
        {"history", std::move(serialized_history)},
        {"sketches", std::move(serialized_sketches)},
        {"constructions", std::move(serialized_constructions)},
        {"calculated_boundaries", std::move(serialized_boundaries)},
    };
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Cannot write document: " + path.string());
        }
        output << std::setw(2) << root << '\n';
        if (!output) {
            throw std::runtime_error("Document write failed: " + path.string());
        }
    }
    std::filesystem::rename(temporary, path);
}

}  // namespace zima::document
