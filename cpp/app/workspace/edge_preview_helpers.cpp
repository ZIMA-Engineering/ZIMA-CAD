#include "edge_treatment_preview.hpp"
#include <zima/document/edge_treatment_selection.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>

namespace zima::app::workspace_detail {

struct EdgeTreatmentSection {
    zima::kernel::ViewerEdge wire;
    zima::kernel::Vec3 first_tangent;
    zima::kernel::Vec3 second_tangent;
    std::optional<zima::kernel::Vec3> radius_center;
    std::optional<zima::kernel::Vec3> radius_rim;
    zima::kernel::Vec3 plane_normal;
    double first_distance{};
    double second_distance{};
    bool curved_support{};
};

void edge_preview_swap_section_sides(EdgeTreatmentSection& section) {
    std::swap(section.first_tangent, section.second_tangent);
    std::swap(section.first_distance, section.second_distance);
    std::ranges::reverse(section.wire.points);
}

double edge_preview_length(const zima::kernel::Vec3& value) {
    return std::hypot(std::hypot(value.x, value.y), value.z);
}

zima::kernel::Vec3 edge_preview_difference(
    const zima::kernel::Vec3& left, const zima::kernel::Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double edge_preview_dot(
    const zima::kernel::Vec3& left, const zima::kernel::Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

std::optional<zima::kernel::Vec3> edge_preview_normalized(
    const zima::kernel::Vec3& value) {
    const double length = edge_preview_length(value);
    if (length <= 1.0e-9) return std::nullopt;
    return zima::kernel::Vec3{
        value.x / length, value.y / length, value.z / length};
}

zima::kernel::Vec3 edge_preview_offset(const zima::kernel::Vec3& point,
    const zima::kernel::Vec3& direction, double distance) {
    return {point.x + direction.x * distance,
            point.y + direction.y * distance,
            point.z + direction.z * distance};
}

bool edge_preview_same_point(
    const zima::kernel::Vec3& first, const zima::kernel::Vec3& second) {
    return edge_preview_length(edge_preview_difference(first, second)) <= 1.0e-6;
}

int edge_preview_compare_point(
    const zima::kernel::Vec3& first,
    const zima::kernel::Vec3& second) {
    constexpr double tolerance = 1.0e-9;
    const std::array<double, 3> first_values{first.x, first.y, first.z};
    const std::array<double, 3> second_values{second.x, second.y, second.z};
    for (std::size_t axis = 0; axis < first_values.size(); ++axis) {
        if (first_values[axis] < second_values[axis] - tolerance) {
            return -1;
        }
        if (first_values[axis] > second_values[axis] + tolerance) {
            return 1;
        }
    }
    return 0;
}

bool edge_preview_has_face_guides(const zima::kernel::ViewerEdge& edge) {
    return edge.points.size() >= 2 &&
        edge.edge_treatment_side_directions.size() == 2 &&
        edge.edge_treatment_side_directions[0].size() == edge.points.size() &&
        edge.edge_treatment_side_directions[1].size() == edge.points.size();
}

void edge_preview_reverse(zima::kernel::ViewerEdge& edge) {
    std::ranges::reverse(edge.points);
    for (auto& directions : edge.edge_treatment_side_directions) {
        std::ranges::reverse(directions);
    }
    std::ranges::reverse(edge.edge_treatment_endpoint_references);
}

std::vector<EdgeTreatmentPreviewPath> ordered_edge_treatment_preview_paths(
    std::vector<zima::kernel::ViewerEdge> edges) {
    std::erase_if(edges, [](const auto& edge) {
        return !edge_preview_has_face_guides(edge);
    });
    std::vector<EdgeTreatmentPreviewPath> paths;
    while (!edges.empty()) {
        const auto connection_count = [&](const zima::kernel::Vec3& point,
                                           std::size_t excluded) {
            std::size_t count{};
            for (std::size_t index = 0; index < edges.size(); ++index) {
                if (index == excluded) continue;
                if (edge_preview_same_point(point, edges[index].points.front()) ||
                    edge_preview_same_point(point, edges[index].points.back())) {
                    ++count;
                }
            }
            return count;
        };
        bool has_free_endpoint{};
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (connection_count(edges[index].points.front(), index) == 0 ||
                connection_count(edges[index].points.back(), index) == 0) {
                has_free_endpoint = true;
                break;
            }
        }
        std::size_t seed{};
        bool reverse_seed{};
        bool found_seed{};
        zima::kernel::Vec3 seed_start{};
        zima::kernel::Vec3 seed_end{};
        for (std::size_t index = 0; index < edges.size(); ++index) {
            for (const bool reverse : {false, true}) {
                const auto& start = reverse ? edges[index].points.back() :
                                              edges[index].points.front();
                const auto& end = reverse ? edges[index].points.front() :
                                            edges[index].points.back();
                if (has_free_endpoint &&
                    connection_count(start, index) != 0) {
                    continue;
                }
                const int start_order = found_seed ?
                    edge_preview_compare_point(start, seed_start) : -1;
                if (!found_seed || start_order < 0 ||
                    (start_order == 0 &&
                     edge_preview_compare_point(end, seed_end) > 0)) {
                    seed = index;
                    reverse_seed = reverse;
                    seed_start = start;
                    seed_end = end;
                    found_seed = true;
                }
            }
        }
        auto seed_edge = std::move(edges[seed]);
        edges.erase(edges.begin() + static_cast<std::ptrdiff_t>(seed));
        if (reverse_seed) edge_preview_reverse(seed_edge);
        EdgeTreatmentPreviewPath path{{std::move(seed_edge)}};
        while (!edges.empty()) {
            const auto end = path.edges.back().points.back();
            std::optional<std::pair<std::size_t, bool>> continuation;
            for (std::size_t index = 0; index < edges.size(); ++index) {
                const bool front = edge_preview_same_point(
                    end, edges[index].points.front());
                const bool back = edge_preview_same_point(
                    end, edges[index].points.back());
                if (!front && !back) continue;
                if (continuation) {
                    // A branch is not one unambiguous route. Finish this path;
                    // the remaining branch becomes its own preview cage.
                    continuation.reset();
                    break;
                }
                continuation = std::pair{index, back};
            }
            if (!continuation) break;
            auto next = std::move(edges[continuation->first]);
            edges.erase(edges.begin() +
                static_cast<std::ptrdiff_t>(continuation->first));
            if (continuation->second) edge_preview_reverse(next);
            path.edges.push_back(std::move(next));
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

std::vector<zima::kernel::Vec3> edge_treatment_preview_path_nodes(
    const EdgeTreatmentPreviewPath& path) {
    std::vector<zima::kernel::Vec3> nodes;
    if (path.edges.empty() || path.edges.front().points.empty()) return nodes;
    nodes.push_back(path.edges.front().points.front());
    for (const auto& edge : path.edges) {
        if (edge.points.empty()) continue;
        const auto& endpoint = edge.points.back();
        if (edge_preview_same_point(endpoint, nodes.back())) continue;
        if (edge_preview_same_point(endpoint, nodes.front())) continue;
        nodes.push_back(endpoint);
    }
    return nodes;
}

zima::kernel::Vec3 edge_preview_blend_direction(
    const zima::kernel::Vec3& first, const zima::kernel::Vec3& second,
    double ratio) {
    const auto normalized_first = edge_preview_normalized(first);
    const auto normalized_second = edge_preview_normalized(second);
    if (!normalized_first || !normalized_second) return first;
    const double cosine = std::clamp(edge_preview_dot(
        *normalized_first, *normalized_second), -1.0, 1.0);
    if (cosine > 0.9995 || cosine < -0.9995) {
        const zima::kernel::Vec3 blended{
            normalized_first->x * (1.0 - ratio) + normalized_second->x * ratio,
            normalized_first->y * (1.0 - ratio) + normalized_second->y * ratio,
            normalized_first->z * (1.0 - ratio) + normalized_second->z * ratio};
        return edge_preview_normalized(blended).value_or(*normalized_first);
    }
    const double angle = std::acos(cosine);
    const double denominator = std::sin(angle);
    const double first_weight = std::sin((1.0 - ratio) * angle) / denominator;
    const double second_weight = std::sin(ratio * angle) / denominator;
    return {normalized_first->x * first_weight +
                normalized_second->x * second_weight,
            normalized_first->y * first_weight +
                normalized_second->y * second_weight,
            normalized_first->z * first_weight +
                normalized_second->z * second_weight};
}

std::optional<EdgeTreatmentSection> edge_treatment_section(
    const zima::kernel::Vec3& point,
    const zima::kernel::Vec3& raw_first_direction,
    const zima::kernel::Vec3& raw_second_direction,
    const zima::document::EdgeTreatmentParameters& parameters,
    bool fillet, double radius) {
    const bool flip = !fillet && parameters.flip;
    const auto first_direction = edge_preview_normalized(
        flip ? raw_second_direction : raw_first_direction);
    const auto second_direction = edge_preview_normalized(
        flip ? raw_first_direction : raw_second_direction);
    if (!first_direction || !second_direction) return std::nullopt;
    const double raw_cosine = edge_preview_dot(
        *first_direction, *second_direction);
    // Exactly tangent supports have no finite line/line corner. Their
    // curvature decides the real blend and is handled from the calculated
    // support-face sections below. Clamping this case invents a huge remote
    // radius cage (for example a plane meeting an earlier Fillet cylinder).
    if (raw_cosine > 1.0 - 1.0e-8) return std::nullopt;
    const double cosine = std::clamp(raw_cosine, -0.999999, 0.999999);
    const double half_angle = std::acos(cosine) * 0.5;
    if (half_angle <= 1.0e-5 ||
        std::abs(std::tan(half_angle)) <= 1.0e-9) return std::nullopt;
    double first_distance{};
    double second_distance{};
    if (fillet) {
        if (!std::isfinite(radius) || radius <= 1.0e-9) return std::nullopt;
        first_distance = radius / std::tan(half_angle);
        second_distance = first_distance;
    } else {
        first_distance = parameters.primary_size;
        using Mode = zima::document::EdgeTreatmentParameters::ChamferMode;
        if (parameters.chamfer_mode == Mode::EqualDistance) {
            second_distance = first_distance;
        } else if (parameters.chamfer_mode == Mode::TwoDistances) {
            second_distance = parameters.secondary_size;
        } else {
            const double corner_angle = std::acos(cosine);
            const double chamfer_angle = parameters.angle_degrees *
                std::numbers::pi / 180.0;
            const double denominator =
                std::sin(corner_angle + chamfer_angle);
            if (!std::isfinite(chamfer_angle) || chamfer_angle <= 0.0 ||
                std::abs(denominator) <= 1.0e-9) return std::nullopt;
            second_distance = first_distance * std::sin(chamfer_angle) /
                denominator;
        }
        if (!std::isfinite(first_distance) || first_distance <= 1.0e-9 ||
            !std::isfinite(second_distance) || second_distance <= 1.0e-9) {
            return std::nullopt;
        }
    }
    EdgeTreatmentSection result;
    result.wire.overlay = true;
    result.first_tangent = edge_preview_offset(
        point, *first_direction, first_distance);
    result.second_tangent = edge_preview_offset(
        point, *second_direction, second_distance);
    result.first_distance = first_distance;
    result.second_distance = second_distance;
    result.plane_normal = zima::kernel::dimension_unit(
        zima::kernel::dimension_cross(*first_direction, *second_direction));
    if (!fillet) {
        result.wire.points = {result.first_tangent, result.second_tangent};
        return result;
    }
    const auto bisector = edge_preview_normalized({
        first_direction->x + second_direction->x,
        first_direction->y + second_direction->y,
        first_direction->z + second_direction->z});
    if (!bisector || std::sin(half_angle) <= 1.0e-9) return std::nullopt;
    const auto center = edge_preview_offset(
        point, *bisector, radius / std::sin(half_angle));
    const auto first_radius = edge_preview_difference(
        result.first_tangent, center);
    const auto second_radius = edge_preview_difference(
        result.second_tangent, center);
    constexpr std::size_t samples = 12;
    result.wire.points.reserve(samples + 1);
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double ratio = static_cast<double>(sample) /
            static_cast<double>(samples);
        const auto direction = edge_preview_blend_direction(
            first_radius, second_radius, ratio);
        result.wire.points.push_back(edge_preview_offset(
            center, direction, radius));
    }
    result.plane_normal=zima::kernel::dimension_unit(zima::kernel::dimension_cross(first_radius,second_radius));
    result.radius_center = center;
    result.radius_rim = result.wire.points[samples / 2];
    return result;
}

bool edge_preview_same_face(const zima::kernel::FaceReference& first,
        const zima::kernel::FaceReference& second) {
    return first.owner_id == second.owner_id &&
        first.semantic_key == second.semantic_key &&
        first.instance_path == second.instance_path;
}

std::vector<std::vector<zima::kernel::Vec3>> edge_preview_face_sections(
        const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::FaceReference& face,
        const zima::kernel::Vec3& point,
        const zima::kernel::Vec3& plane_normal,
        const zima::kernel::Vec3& guide) {
    using Vec3 = zima::kernel::Vec3;
    struct Segment {Vec3 first;Vec3 second;};
    std::vector<Segment> segments;
    const double coordinate_scale = std::max({1.0, std::abs(point.x),
        std::abs(point.y), std::abs(point.z)});
    const double tolerance = 1.0e-7 * coordinate_scale;
    const auto signed_distance = [&](const Vec3& value) {
        return edge_preview_dot(edge_preview_difference(value, point),
            plane_normal);
    };
    const auto append_unique = [&](std::vector<Vec3>& values,
                                    const Vec3& value) {
        if (std::ranges::none_of(values, [&](const auto& existing) {
                return edge_preview_length(edge_preview_difference(
                    existing, value)) <= tolerance;
            })) values.push_back(value);
    };
    for (std::size_t triangle = 0;
            triangle < mesh.triangle_references.size(); ++triangle) {
        if (!edge_preview_same_face(mesh.triangle_references[triangle], face) ||
            3 * triangle + 2 >= mesh.triangles.size()) continue;
        std::array<Vec3, 3> vertices;
        bool valid = true;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto index = mesh.triangles[3 * triangle + corner];
            if (index >= mesh.vertices.size()) {valid = false;break;}
            vertices[corner] = mesh.vertices[index];
        }
        if (!valid) continue;
        std::vector<Vec3> intersections;
        for (std::size_t edge = 0; edge < 3; ++edge) {
            const auto& first = vertices[edge];
            const auto& second = vertices[(edge + 1) % 3];
            const double first_distance = signed_distance(first);
            const double second_distance = signed_distance(second);
            if (std::abs(first_distance) <= tolerance)
                append_unique(intersections, first);
            if (std::abs(second_distance) <= tolerance)
                append_unique(intersections, second);
            if ((first_distance < -tolerance &&
                    second_distance > tolerance) ||
                (first_distance > tolerance &&
                    second_distance < -tolerance)) {
                const double ratio = first_distance /
                    (first_distance - second_distance);
                append_unique(intersections, {
                    first.x + (second.x - first.x) * ratio,
                    first.y + (second.y - first.y) * ratio,
                    first.z + (second.z - first.z) * ratio});
            }
        }
        if (intersections.size() < 2) continue;
        std::pair<std::size_t, std::size_t> farthest{0, 1};
        double farthest_distance{};
        for (std::size_t first = 0; first < intersections.size(); ++first)
            for (std::size_t second = first + 1;
                    second < intersections.size(); ++second) {
                const double distance = edge_preview_length(
                    edge_preview_difference(intersections[first],
                        intersections[second]));
                if (distance > farthest_distance) {
                    farthest_distance = distance;
                    farthest = {first, second};
                }
            }
        if (farthest_distance > tolerance)
            segments.push_back({intersections[farthest.first],
                intersections[farthest.second]});
    }
    if (segments.empty()) return {};

    std::vector<Vec3> nodes;
    std::vector<std::array<std::size_t, 2>> links;
    const auto node_for = [&](const Vec3& value) {
        const auto found = std::ranges::find_if(nodes,
            [&](const auto& existing) {
                return edge_preview_length(edge_preview_difference(
                    existing, value)) <= tolerance;
            });
        if (found != nodes.end())
            return static_cast<std::size_t>(found - nodes.begin());
        nodes.push_back(value);
        return nodes.size() - 1;
    };
    std::set<std::pair<std::size_t, std::size_t>> unique_links;
    for (const auto& segment : segments) {
        auto first = node_for(segment.first);
        auto second = node_for(segment.second);
        if (first == second) continue;
        if (second < first) std::swap(first, second);
        if (unique_links.emplace(first, second).second)
            links.push_back({first, second});
    }
    if (links.empty()) return {};
    const auto start = std::ranges::min_element(nodes, {},
        [&](const auto& node) {
            return edge_preview_length(edge_preview_difference(node, point));
        });
    if (start == nodes.end() || edge_preview_length(
            edge_preview_difference(*start, point)) > 10.0 * tolerance)
        return {};
    const std::size_t start_index =
        static_cast<std::size_t>(start - nodes.begin());
    std::vector<std::size_t> neighbors;
    for (const auto& link : links) {
        if (link[0] == start_index) neighbors.push_back(link[1]);
        else if (link[1] == start_index) neighbors.push_back(link[0]);
    }
    const auto normalized_guide = edge_preview_normalized(guide);
    if (!normalized_guide || neighbors.empty()) return {};
    struct PathState {
        std::vector<std::size_t> path;
        std::set<std::size_t> visited;
    };
    std::vector<PathState> pending;
    for (const auto neighbor : neighbors) {
        const auto direction = edge_preview_normalized(
            edge_preview_difference(nodes[neighbor],nodes[start_index]));
        if (direction && edge_preview_dot(*direction,*normalized_guide)>=.5)
            pending.push_back({{start_index,neighbor},
                {start_index,neighbor}});
    }
    std::vector<std::vector<Vec3>> result;
    constexpr std::size_t maximum_paths=64;
    while (!pending.empty() && result.size()<maximum_paths) {
        auto state=std::move(pending.back());
        pending.pop_back();
        const auto previous=state.path[state.path.size()-2];
        const auto current=state.path.back();
        std::vector<std::size_t> candidates;
        for (const auto& link:links) {
            const auto candidate=link[0]==current?link[1]:
                link[1]==current?link[0]:nodes.size();
            if (candidate<nodes.size()&&candidate!=previous&&
                    !state.visited.contains(candidate))
                candidates.push_back(candidate);
        }
        if (candidates.empty()||state.path.size()>=nodes.size()) {
            std::vector<Vec3> path{point};
            for (std::size_t index=1;index<state.path.size();++index)
                path.push_back(nodes[state.path[index]]);
            result.push_back(std::move(path));
            continue;
        }
        // A single persisted face identity can have several current Body
        // descendants meeting at this section.  Preserve every local branch;
        // the rolling-radius solution below selects the nearest valid pair.
        // Following only the straightest branch can jump to a remote Body
        // boundary instead of returning over the preceding radius.
        for (const auto candidate:candidates) {
            if (pending.size()+result.size()>=maximum_paths) break;
            auto branch=state;
            branch.path.push_back(candidate);
            branch.visited.insert(candidate);
            pending.push_back(std::move(branch));
        }
    }
    return result;
}

std::optional<EdgeTreatmentSection> edge_treatment_curved_section(
        const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::ViewerEdge& route,
        std::size_t point_index,
        double radius) {
    using Vec3 = zima::kernel::Vec3;
    if (!std::isfinite(radius) || radius <= 1.0e-9 ||
        route.edge_treatment_side_references.size() != 2 ||
        !edge_preview_has_face_guides(route) ||
        point_index >= route.points.size()) return {};
    const auto& point = route.points[point_index];
    Vec3 route_delta;
    if (point_index == 0)
        route_delta = edge_preview_difference(route.points[1], point);
    else if (point_index + 1 == route.points.size())
        route_delta = edge_preview_difference(point,
            route.points[point_index - 1]);
    else
        route_delta = edge_preview_difference(route.points[point_index + 1],
            route.points[point_index - 1]);
    const auto plane_normal = edge_preview_normalized(route_delta);
    if (!plane_normal) return {};
    const auto first_axis = edge_preview_normalized(
        route.edge_treatment_side_directions[0][point_index]);
    if (!first_axis) return {};
    const auto second_axis = edge_preview_normalized(
        zima::kernel::dimension_cross(*plane_normal, *first_axis));
    if (!second_axis) return {};
    const auto first_curves = edge_preview_face_sections(mesh,
        route.edge_treatment_side_references[0], point, *plane_normal,
        route.edge_treatment_side_directions[0][point_index]);
    const auto second_curves = edge_preview_face_sections(mesh,
        route.edge_treatment_side_references[1], point, *plane_normal,
        route.edge_treatment_side_directions[1][point_index]);
    if (first_curves.empty() || second_curves.empty()) return {};

    struct Vec2 {double x{};double y{};};
    const auto to_2d = [&](const Vec3& value) {
        const auto delta = edge_preview_difference(value, point);
        return Vec2{edge_preview_dot(delta, *first_axis),
            edge_preview_dot(delta, *second_axis)};
    };
    const auto to_3d = [&](const Vec2& value) {
        return Vec3{point.x + first_axis->x * value.x + second_axis->x * value.y,
            point.y + first_axis->y * value.x + second_axis->y * value.y,
            point.z + first_axis->z * value.x + second_axis->z * value.y};
    };
    const auto cross = [](const Vec2& left, const Vec2& right) {
        return left.x * right.y - left.y * right.x;
    };
    const auto difference = [](const Vec2& left, const Vec2& right) {
        return Vec2{left.x - right.x, left.y - right.y};
    };
    const auto add = [](const Vec2& point, const Vec2& direction,
                         double scale) {
        return Vec2{point.x + direction.x * scale,
            point.y + direction.y * scale};
    };
    struct Candidate {Vec2 center;Vec2 first;Vec2 second;double score{};};
    std::optional<Candidate> best;
    for (const auto& first_curve:first_curves)
        for (const auto& second_curve:second_curves) {
    std::vector<Vec2> first, second;
    std::ranges::transform(first_curve, std::back_inserter(first), to_2d);
    std::ranges::transform(second_curve, std::back_inserter(second), to_2d);
    double first_progress{};
    for (std::size_t first_segment = 1;
            first_segment < first.size(); ++first_segment) {
        const auto first_delta = difference(first[first_segment],
            first[first_segment - 1]);
        const double first_length = std::hypot(first_delta.x, first_delta.y);
        if (first_length <= 1.0e-9) continue;
        const Vec2 first_unit{first_delta.x / first_length,
            first_delta.y / first_length};
        const Vec2 first_normal{-first_unit.y, first_unit.x};
        double second_progress{};
        for (std::size_t second_segment = 1;
                second_segment < second.size(); ++second_segment) {
            const auto second_delta = difference(second[second_segment],
                second[second_segment - 1]);
            const double second_length = std::hypot(second_delta.x,
                second_delta.y);
            if (second_length <= 1.0e-9) continue;
            const Vec2 second_unit{second_delta.x / second_length,
                second_delta.y / second_length};
            const Vec2 second_normal{-second_unit.y, second_unit.x};
            const double denominator = cross(first_delta, second_delta);
            if (std::abs(denominator) <= 1.0e-12 *
                    first_length * second_length) {
                second_progress += second_length;
                continue;
            }
            for (const double first_side : {-1.0, 1.0})
                for (const double second_side : {-1.0, 1.0}) {
                    const auto first_offset = add(first[first_segment - 1],
                        first_normal, first_side * radius);
                    const auto second_offset = add(second[second_segment - 1],
                        second_normal, second_side * radius);
                    const auto between = difference(second_offset,
                        first_offset);
                    const double first_ratio = cross(between, second_delta) /
                        denominator;
                    const double second_ratio = cross(between, first_delta) /
                        denominator;
                    constexpr double parameter_tolerance = 1.0e-6;
                    if (first_ratio < -parameter_tolerance ||
                        first_ratio > 1.0 + parameter_tolerance ||
                        second_ratio < -parameter_tolerance ||
                        second_ratio > 1.0 + parameter_tolerance) continue;
                    const auto first_contact = add(
                        first[first_segment - 1], first_delta, first_ratio);
                    const auto second_contact = add(
                        second[second_segment - 1], second_delta,
                        second_ratio);
                    if (std::hypot(first_contact.x - second_contact.x,
                            first_contact.y - second_contact.y) <= 1.0e-6)
                        continue;
                    const double first_distance = first_progress +
                        std::clamp(first_ratio, 0.0, 1.0) * first_length;
                    const double second_distance = second_progress +
                        std::clamp(second_ratio, 0.0, 1.0) * second_length;
                    if (first_distance <= 1.0e-6 ||
                        second_distance <= 1.0e-6) continue;
                    const auto center = add(first_offset, first_delta,
                        first_ratio);
                    const double score = first_distance + second_distance;
                    if (!best || score < best->score)
                        best = Candidate{center, first_contact,
                            second_contact, score};
                }
            second_progress += second_length;
        }
        first_progress += first_length;
    }
    }
    if (!best) return {};
    EdgeTreatmentSection result;
    result.wire.overlay = true;
    result.first_tangent = to_3d(best->first);
    result.second_tangent = to_3d(best->second);
    result.first_distance = edge_preview_length(edge_preview_difference(
        result.first_tangent, point));
    result.second_distance = edge_preview_length(edge_preview_difference(
        result.second_tangent, point));
    result.radius_center = to_3d(best->center);
    const auto first_radius = edge_preview_difference(result.first_tangent,
        *result.radius_center);
    const auto second_radius = edge_preview_difference(result.second_tangent,
        *result.radius_center);
    constexpr std::size_t samples = 12;
    for (std::size_t sample = 0; sample <= samples; ++sample) {
        const double ratio = static_cast<double>(sample) /
            static_cast<double>(samples);
        const auto direction = edge_preview_blend_direction(first_radius,
            second_radius, ratio);
        result.wire.points.push_back(edge_preview_offset(
            *result.radius_center, direction, radius));
    }
    result.radius_rim = result.wire.points[samples / 2];
    result.plane_normal = *plane_normal;
    result.curved_support = true;
    return result;
}

void edge_preview_append_corner(std::vector<zima::kernel::Vec3>& rail,
    const zima::kernel::Vec3& corner,
    const zima::kernel::Vec3& target) {
    if (rail.empty() || edge_preview_same_point(rail.back(), target)) return;
    const auto previous_vector = edge_preview_difference(rail.back(), corner);
    const auto target_vector = edge_preview_difference(target, corner);
    const double previous_length = edge_preview_length(previous_vector);
    const double target_length = edge_preview_length(target_vector);
    constexpr std::size_t samples = 6;
    for (std::size_t sample = 1; sample <= samples; ++sample) {
        const double ratio = static_cast<double>(sample) /
            static_cast<double>(samples);
        const auto direction = edge_preview_blend_direction(
            previous_vector, target_vector, ratio);
        const double distance = previous_length * (1.0 - ratio) +
            target_length * ratio;
        rail.push_back(edge_preview_offset(corner, direction, distance));
    }
}

struct EdgePreviewCircularPath {
    zima::kernel::Vec3 center;
    zima::kernel::Vec3 axis;
    double radius{};
};

std::optional<EdgePreviewCircularPath> edge_preview_circular_path(
        const zima::kernel::ViewerEdge& edge,bool first) {
    if(edge.points.size()<3)return {};
    const auto& p=edge.points.front();
    const auto u=edge_preview_difference(edge.points[edge.points.size()/2],p);
    const auto v=edge_preview_difference(edge.points.back(),p);
    const auto w=zima::kernel::dimension_cross(u,v);
    const double w2=edge_preview_dot(w,w);
    if(w2<1.e-18)return {};
    auto center=edge_preview_offset(p,zima::kernel::dimension_cross(v,w),edge_preview_dot(u,u)/(2.*w2));
    center=edge_preview_offset(center,zima::kernel::dimension_cross(w,u),edge_preview_dot(v,v)/(2.*w2));
    const auto axis=edge_preview_normalized(w);
    const double radius=edge_preview_length(edge_preview_difference(p,center));
    if(!axis||radius<=1.e-9)return {};
    for(const auto& point:edge.points) {
        const auto radial=edge_preview_difference(point,center);
        if(std::abs(edge_preview_dot(radial,*axis))>1.e-7||
            std::abs(edge_preview_length(radial)-radius)>1.e-7)return {};
    }
    return EdgePreviewCircularPath{center,edge_preview_offset({},*axis,first?1.:-1.),radius};
}

struct FilletPreviewTerminal {
    std::size_t group{};
    zima::kernel::Vec3 node;
    zima::kernel::Vec3 axis;
    double extent{};
    double backward_extent{};
    EdgeTreatmentSection section;
    std::size_t first_rail{};
    std::size_t second_rail{};
    std::size_t end_arc{};
    bool at_start{};
    std::vector<zima::kernel::FaceReference> curved_terminal_faces;
    std::optional<bool> adds_material;
    zima::kernel::VertexReference vertex;
    std::optional<EdgePreviewCircularPath> circular_path;
};

std::shared_ptr<const zima::kernel::SurfaceGeometry> edge_preview_surface(
        const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::FaceReference& face) {
    if(face.surface)return face.surface;
    const auto found=std::ranges::find_if(mesh.triangle_references,
        [&](const auto& candidate){return candidate==face&&candidate.surface;});
    if(found!=mesh.triangle_references.end())return found->surface;
    // Compact saved rollback packets retain exact surface descriptors in
    // the reference packet. Resolve the same identity there, without reading
    // the native B-Rep or changing the persisted document format.
    const auto original=std::ranges::find_if(mesh.original_references.triangle_references,
        [&](const auto& candidate){return candidate==face&&candidate.surface;});
    return original==mesh.original_references.triangle_references.end()?nullptr:original->surface;
}

std::optional<std::pair<zima::kernel::Vec3,zima::kernel::Vec3>>
edge_preview_terminal_plane(const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::FaceReference& face) {
    if(const auto surface=edge_preview_surface(mesh,face)) {
        if(surface->kind!=zima::kernel::SurfaceGeometry::Kind::Plane)return {};
        return std::pair{surface->origin,surface->axis};
    }
    // Some current viewer packets carry only the planar face's triangles.
    // Prove coplanarity of all its vertices before using their exact plane.
    std::optional<std::pair<zima::kernel::Vec3,zima::kernel::Vec3>> result;
    for(std::size_t t=0;t<mesh.triangle_references.size()&&3*t+2<mesh.triangles.size();++t) {
        if(mesh.triangle_references[t]!=face)continue;
        const auto a=mesh.triangles[3*t],b=mesh.triangles[3*t+1],c=mesh.triangles[3*t+2];
        if(std::max({a,b,c})>=mesh.vertices.size())continue;
        if(!result) {
            const auto normal=edge_preview_normalized(zima::kernel::dimension_cross(
                edge_preview_difference(mesh.vertices[b],mesh.vertices[a]),
                edge_preview_difference(mesh.vertices[c],mesh.vertices[a])));
            if(normal)result=std::pair{mesh.vertices[a],*normal};
        }
        if(result)for(const auto i:{a,b,c})
            if(std::abs(edge_preview_dot(edge_preview_difference(mesh.vertices[i],result->first),result->second))>1.e-8)
                return {};
    }
    // Wire-only rollback packets retain exact boundary samples and the
    // calculated in-face directions. They identify a plane even when its
    // display triangles have deliberately been omitted from the packet.
    for(const auto& edge:mesh.edges)for(std::size_t side=0;
            side<edge.edge_treatment_side_references.size();++side) {
        if(edge.edge_treatment_side_references[side]!=face||edge.points.size()<2||
                side>=edge.edge_treatment_side_directions.size()||
                edge.edge_treatment_side_directions[side].size()!=edge.points.size())continue;
        const auto& guides=edge.edge_treatment_side_directions[side];
        if(!result) {
            const auto normal=edge_preview_normalized(zima::kernel::dimension_cross(
                edge_preview_difference(edge.points[1],edge.points[0]),guides[0]));
            if(normal)result=std::pair{edge.points[0],*normal};
        }
        if(result)for(std::size_t i=0;i<edge.points.size();++i)
            if(std::abs(edge_preview_dot(edge_preview_difference(edge.points[i],result->first),result->second))>1.e-7||
                std::abs(edge_preview_dot(guides[i],result->second))>1.e-7)return {};
    }
    return result;
}

std::optional<bool> edge_preview_adds_material(
        const zima::kernel::ViewerMesh* mesh,
        const zima::kernel::ViewerEdge& edge,
        const EdgeTreatmentSection& section,const zima::kernel::Vec3& node) {
    if(!mesh)return {};
    const auto center=section.radius_center.value_or(edge_preview_offset(
        section.first_tangent,edge_preview_difference(section.second_tangent,
            section.first_tangent),.5));
    const auto offset=edge_preview_difference(center,node);
    for(const auto& face:edge.edge_treatment_side_references) {
        const auto surface=edge_preview_surface(*mesh,face);
        if(!surface||surface->kind!=zima::kernel::SurfaceGeometry::Kind::Plane)continue;
        auto outward=edge_preview_offset({},surface->axis,surface->reversed?-1.:1.);
        // The current material winding remains authoritative if a Boolean
        // reversed a retained original support.
        for(std::size_t t=0;t<mesh->triangle_references.size()&&3*t+2<mesh->triangles.size();++t)
            if(mesh->triangle_references[t]==face) {
                const auto a=mesh->triangles[3*t],b=mesh->triangles[3*t+1],c=mesh->triangles[3*t+2];
                if(std::max({a,b,c})>=mesh->vertices.size())continue;
                const auto normal=edge_preview_normalized(zima::kernel::dimension_cross(
                    edge_preview_difference(mesh->vertices[b],mesh->vertices[a]),
                    edge_preview_difference(mesh->vertices[c],mesh->vertices[a])));
                if(normal){outward=*normal;break;}
            }
        const double side=edge_preview_dot(offset,outward);
        if(std::abs(side)>1.0e-9)return side>0.;
    }
    return {};
}

bool edge_preview_on_minor_arc(const zima::kernel::Vec3& radial,
    const zima::kernel::Vec3& first,
    const zima::kernel::Vec3& second);

struct FilletPreviewCylinder {
    zima::kernel::Vec3 center;
    zima::kernel::Vec3 axis;
    zima::kernel::Vec3 first_radial;
    zima::kernel::Vec3 last_radial;
    double radius{};
};

std::vector<FilletPreviewCylinder> edge_preview_terminal_cylinders(
        const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::FaceReference& face,
        const zima::kernel::Vec3& node) {
    using Vec3=zima::kernel::Vec3;
    std::vector<FilletPreviewCylinder> result;
    const auto has_face=[&](const auto& edge) {
        return std::ranges::any_of(edge.edge_treatment_side_references,
            [&](const auto& candidate) {
                return edge_preview_same_face(candidate,face);
            });
    };
    const auto cross=[](const Vec3& first,const Vec3& second) {
        return Vec3{first.y*second.z-first.z*second.y,
            first.z*second.x-first.x*second.z,
            first.x*second.y-first.y*second.x};
    };
    for(const auto& edge:mesh.edges) {
        if(!has_face(edge)||edge.points.size()<3||
           std::ranges::none_of(edge.points,[&](const auto& point) {
               return edge_preview_same_point(point,node);
           }))continue;
        const auto& first=edge.points.front();
        const auto& middle=edge.points[edge.points.size()/2];
        const auto& last=edge.points.back();
        const auto u=edge_preview_difference(middle,first);
        const auto v=edge_preview_difference(last,first);
        const auto w=cross(u,v);
        const double w_squared=edge_preview_dot(w,w);
        const double scale=std::max({1.,edge_preview_length(u),
            edge_preview_length(v)});
        if(w_squared<=1.0e-16*scale*scale*scale*scale)continue;
        const auto v_cross_w=cross(v,w);
        const auto w_cross_u=cross(w,u);
        const double denominator=2.*w_squared;
        const auto center=Vec3{
            first.x+(edge_preview_dot(u,u)*v_cross_w.x+
                edge_preview_dot(v,v)*w_cross_u.x)/denominator,
            first.y+(edge_preview_dot(u,u)*v_cross_w.y+
                edge_preview_dot(v,v)*w_cross_u.y)/denominator,
            first.z+(edge_preview_dot(u,u)*v_cross_w.z+
                edge_preview_dot(v,v)*w_cross_u.z)/denominator};
        const auto axis=edge_preview_normalized(w);
        const double radius=edge_preview_length(
            edge_preview_difference(first,center));
        if(!axis||radius<=1.0e-9)continue;
        const double tolerance=1.0e-5*std::max(scale,radius);
        if(std::ranges::any_of(edge.points,[&](const auto& point) {
                const auto relative=edge_preview_difference(point,center);
                return std::abs(edge_preview_dot(relative,*axis))>
                        tolerance||
                    std::abs(edge_preview_length(relative)-radius)>
                        tolerance;
            }))continue;
        FilletPreviewCylinder cylinder{center,*axis,
            edge_preview_difference(first,center),
            edge_preview_difference(last,center),radius};
        result.push_back(std::move(cylinder));
    }
    return result;
}

std::vector<zima::kernel::FaceReference>
edge_preview_curved_terminal_faces(
        const zima::kernel::ViewerMesh& mesh,
        const zima::kernel::ViewerEdge& route,
        const zima::kernel::Vec3& node,
        const zima::kernel::Vec3& inward_axis,double profile_reach) {
    std::vector<zima::kernel::FaceReference> result;
    const auto support=[&](const auto& face) {
        return std::ranges::any_of(route.edge_treatment_side_references,
            [&](const auto& candidate) {
                return edge_preview_same_face(candidate,face);
            });
    };
    const auto curved=[&](const auto& face) {
        const auto surface=edge_preview_surface(mesh,face);
        if(surface)return surface->kind!=zima::kernel::SurfaceGeometry::Kind::Plane;
        return !edge_preview_terminal_cylinders(mesh,face,node).empty();
    };
    const auto append=[&](const auto& face) {
        if(!face.valid()||support(face)||!curved(face)||
           std::ranges::any_of(result,[&](const auto& current) {
               return edge_preview_same_face(current,face);
           }))return;
        result.push_back(face);
    };
    for(const auto& edge:mesh.edges) {
        if(std::ranges::none_of(edge.points,[&](const auto& point) {
                return edge_preview_same_point(point,node);
            }))continue;
        for(const auto& face:edge.edge_treatment_side_references)
            append(face);
    }
    // The physical terminal can be a compound envelope: an earlier radius
    // followed by one or more planes or spline faces. Walk that exact local
    // face adjacency only as far as the new profile can reach. The preview
    // then intersects all encountered boundaries, matching the successive
    // exact half-space/surface cuts used by the final calculation.
    const double tolerance=1.0e-6*std::max(1.,profile_reach);
    const auto within_reach=[&](const auto& edge) {
        return std::ranges::any_of(edge.points,[&](const auto& point) {
            const auto relative=edge_preview_difference(point,node);
            const double axial=edge_preview_dot(relative,inward_axis);
            return axial>tolerance&&axial<profile_reach-tolerance&&
                edge_preview_length(relative)<=2.*profile_reach+tolerance;
        });
    };
    for(std::size_t current=0;current<result.size();++current) {
        for(const auto& edge:mesh.edges) {
            if(!within_reach(edge)||
               std::ranges::none_of(edge.edge_treatment_side_references,
                    [&](const auto& face) {
                        return edge_preview_same_face(face,result[current]);
                    }))continue;
            for(const auto& face:edge.edge_treatment_side_references) {
                if(!face.valid()||support(face)||
                   std::ranges::any_of(result,[&](const auto& candidate) {
                       return edge_preview_same_face(candidate,face);
                   }))continue;
                result.push_back(face);
            }
        }
    }
    return result;
}

std::vector<zima::kernel::FaceReference> edge_preview_terminal_faces(
        const zima::kernel::ViewerMesh& mesh,const zima::kernel::ViewerEdge& route,
        bool first,const zima::kernel::Vec3& axis,double reach,
        const zima::kernel::Vec3* preceding=nullptr) {
    const auto& node=first?route.points.front():route.points.back();
    auto faces=edge_preview_curved_terminal_faces(mesh,route,node,axis,reach);
    const auto vertex=route.edge_treatment_endpoint_references.size()==2
        ?route.edge_treatment_endpoint_references[first?0:1]:zima::kernel::VertexReference{};
    for(const auto& edge:mesh.edges) {
        const bool incident=vertex.valid()&&!edge.edge_treatment_endpoint_references.empty()
            ?std::ranges::find(edge.edge_treatment_endpoint_references,vertex)!=
                edge.edge_treatment_endpoint_references.end()
            :std::ranges::any_of(edge.points,[&](const auto& p){return edge_preview_same_point(p,node);});
        if(!incident)continue;
        for(const auto& face:edge.edge_treatment_side_references) {
            if(!face.valid()||std::ranges::find(route.edge_treatment_side_references,face)!=
                    route.edge_treatment_side_references.end()||
               std::ranges::find(faces,face)!=faces.end())continue;
            const auto plane=edge_preview_terminal_plane(mesh,face);
            if(plane&&std::abs(edge_preview_dot(plane->second,axis))<=1.0e-10&&
                    (!preceding||std::abs(edge_preview_dot(plane->second,
                        edge_preview_difference(*preceding,plane->first)))<=1.e-8))continue;
            faces.push_back(face);
        }
    }
    return faces;
}

zima::kernel::Vec3 edge_preview_rotate(const zima::kernel::Vec3& point,
        const EdgePreviewCircularPath& circle,double angle) {
    const auto p=edge_preview_difference(point,circle.center);
    auto result=edge_preview_offset(circle.center,p,std::cos(angle));
    result=edge_preview_offset(result,zima::kernel::dimension_cross(circle.axis,p),std::sin(angle));
    return edge_preview_offset(result,circle.axis,edge_preview_dot(circle.axis,p)*(1.-std::cos(angle)));
}

std::vector<zima::kernel::Vec3> edge_preview_circular_terminal_intersection(
        const zima::kernel::ViewerMesh& mesh,const FilletPreviewTerminal& terminal) {
    const auto& circle=*terminal.circular_path;
    std::vector<std::pair<zima::kernel::Vec3,zima::kernel::Vec3>> planes;
    const auto preceding=edge_preview_rotate(terminal.node,circle,
        std::min(.1,terminal.extent/circle.radius*.1));
    for(const auto& face:terminal.curved_terminal_faces)
        if(auto plane=edge_preview_terminal_plane(mesh,face)) {
            if(edge_preview_dot(edge_preview_difference(preceding,plane->first),plane->second)<0.)
                plane->second=edge_preview_offset({},plane->second,-1.);
            planes.push_back(*plane);
        }
    if(planes.empty())return {};
    std::vector<zima::kernel::Vec3> result;
    for(const auto& point:terminal.section.wire.points) {
        const auto relative=edge_preview_difference(point,circle.center);
        const auto center=edge_preview_offset(circle.center,circle.axis,edge_preview_dot(relative,circle.axis));
        const auto radial=edge_preview_difference(point,center);
        const auto tangent=zima::kernel::dimension_cross(circle.axis,radial);
        double parameter{};
        for(const auto& [origin,normal]:planes) {
            // Only trim a section point lying beyond this terminal plane.
            // A tangent plane cannot demand a fictitious outgoing arc tail.
            if(edge_preview_dot(edge_preview_difference(edge_preview_rotate(point,circle,parameter),origin),normal)>=-1.e-8)continue;
            const double a=edge_preview_dot(normal,radial),b=edge_preview_dot(normal,tangent);
            const double c=edge_preview_dot(normal,edge_preview_difference(center,origin));
            const double magnitude=std::hypot(a,b);
            if(magnitude<=1.e-12||std::abs(c)>magnitude+1.e-8)return {};
            const double phase=std::atan2(b,a),delta=std::acos(std::clamp(-c/magnitude,-1.,1.));
            std::optional<double> intersection;
            for(const double root:{phase-delta,phase+delta})for(int turn=-1;turn<=1;++turn) {
                const double angle=root+turn*2.*std::numbers::pi;
                if(angle<parameter-1.e-9||angle>terminal.extent/circle.radius+1.e-9)continue;
                if(!intersection||angle<*intersection)intersection=angle;
            }
            if(!intersection)return {};
            parameter=*intersection;
        }
        result.push_back(edge_preview_rotate(point,circle,parameter));
    }
    return result;
}

std::vector<zima::kernel::FaceReference> edge_treatment_preview_end_choices(
        const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
        const zima::document::EdgeTreatmentParameters& parameters,
        const zima::kernel::ViewerMesh* mesh,std::size_t route,bool first) {
    std::vector<zima::kernel::FaceReference> result{{}};
    if(!mesh||route>=groups.size())return result;
    const auto paths=ordered_edge_treatment_preview_paths(groups[route]);
    if(paths.size()!=1||paths[0].edges.empty())return result;
    const auto& edge=first?paths[0].edges.front():paths[0].edges.back();
    const auto& node=first?edge.points.front():edge.points.back();
    const auto& neighbor=first?edge.points[1]:edge.points[edge.points.size()-2];
    const auto axis=edge_preview_normalized(edge_preview_difference(neighbor,node));
    if(!axis)return result;
    auto faces=edge_preview_terminal_faces(*mesh,edge,first,*axis,
        std::max(parameters.primary_size,parameters.secondary_size));
    // Local faces form one automatic envelope. Remote choices extend the
    // route outward only through the local curved terminal to its adjacent
    // cap. A ray hit on an unrelated, farther face is not a terminal choice.
    // Use the published topology and triangles;
    // asking for choices must never calculate a candidate solid.
    std::vector<zima::kernel::FaceReference> adjacent;
    for(const auto& boundary:mesh->edges)
        if(std::ranges::any_of(boundary.edge_treatment_side_references,
                [&](const auto& face) {
                    const auto surface=edge_preview_surface(*mesh,face);
                    return std::ranges::find(faces,face)!=faces.end()&&surface&&
                        surface->kind!=zima::kernel::SurfaceGeometry::Kind::Plane;
                }))
            for(const auto& face:boundary.edge_treatment_side_references)
                if(std::ranges::find(adjacent,face)==adjacent.end())adjacent.push_back(face);
    struct Candidate {zima::kernel::FaceReference face;double distance;};
    std::vector<Candidate> candidates;
    const auto append=[&](const auto& face,double distance) {
        if(distance<=1.0e-7||std::ranges::find(faces,face)!=faces.end()||
           std::ranges::find(edge.edge_treatment_side_references,face)!=
                edge.edge_treatment_side_references.end())return;
        const auto found=std::ranges::find_if(candidates,
            [&](const auto& candidate){return candidate.face==face;});
        if(found==candidates.end())candidates.push_back({face,distance});
        else found->distance=std::min(found->distance,distance);
    };
    for(const auto& face:adjacent) {
        const auto surface=edge_preview_surface(*mesh,face);
        if(!surface||surface->kind!=zima::kernel::SurfaceGeometry::Kind::Plane)continue;
        const double incidence=-edge_preview_dot(surface->axis,*axis);
        if(std::abs(incidence)>1.0e-10)
            append(face,edge_preview_dot(surface->axis,
                edge_preview_difference(surface->origin,node))/incidence);
    }
    for(std::size_t t=0;t<mesh->triangle_references.size()&&3*t+2<mesh->triangles.size();++t) {
        if(std::ranges::find(adjacent,mesh->triangle_references[t])==adjacent.end())continue;
        const auto ia=mesh->triangles[3*t],ib=mesh->triangles[3*t+1],ic=mesh->triangles[3*t+2];
        if(std::max({ia,ib,ic})>=mesh->vertices.size())continue;
        const auto& a=mesh->vertices[ia];
        const auto u=edge_preview_difference(mesh->vertices[ib],a);
        const auto v=edge_preview_difference(mesh->vertices[ic],a);
        const zima::kernel::Vec3 normal{u.y*v.z-u.z*v.y,
            u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const double denominator=-edge_preview_dot(normal,*axis);
        if(std::abs(denominator)<=1.0e-14)continue;
        const double distance=edge_preview_dot(normal,edge_preview_difference(a,node))/denominator;
        if(distance<=1.0e-7)continue;
        const auto relative=edge_preview_difference(edge_preview_offset(node,*axis,-distance),a);
        const double uu=edge_preview_dot(u,u),uv=edge_preview_dot(u,v),vv=edge_preview_dot(v,v);
        const double determinant=uu*vv-uv*uv;
        if(determinant<=1.0e-20)continue;
        const double ru=edge_preview_dot(relative,u),rv=edge_preview_dot(relative,v);
        const double b=(vv*ru-uv*rv)/determinant,c=(uu*rv-uv*ru)/determinant;
        if(b>=-1.0e-7&&c>=-1.0e-7&&b+c<=1.+1.0e-7)
            append(mesh->triangle_references[t],distance);
    }
    std::ranges::stable_sort(candidates,{},&Candidate::distance);
    for(const auto& candidate:candidates) {
        const auto surface=edge_preview_surface(*mesh,candidate.face);
        const bool equivalent=surface&&surface->kind==zima::kernel::SurfaceGeometry::Kind::Plane&&
            std::ranges::any_of(result,[&](const auto& face) {
                const auto other=edge_preview_surface(*mesh,face);
                return other&&other->kind==surface->kind&&
                    std::abs(edge_preview_dot(other->axis,surface->axis))>1.-1.0e-10&&
                    std::abs(edge_preview_dot(surface->axis,
                        edge_preview_difference(other->origin,surface->origin)))<1.0e-7;
            });
        if(!equivalent)result.push_back(candidate.face);
    }
    return result;
}

std::vector<zima::kernel::Vec3>
edge_preview_curved_terminal_intersection(
        const zima::kernel::ViewerMesh& mesh,
        const FilletPreviewTerminal& terminal) {
    using Vec3=zima::kernel::Vec3;
    if(terminal.curved_terminal_faces.empty()||
       terminal.section.wire.points.size()<2)return {};
    if(terminal.circular_path)return edge_preview_circular_terminal_intersection(mesh,terminal);
    std::vector<std::pair<std::size_t,FilletPreviewCylinder>> cylinders;
    for(std::size_t face_index=0;
            face_index<terminal.curved_terminal_faces.size();++face_index)
        for(auto& cylinder:edge_preview_terminal_cylinders(
                mesh,terminal.curved_terminal_faces[face_index],
                terminal.node))
            cylinders.emplace_back(face_index,std::move(cylinder));
    const auto candidate_face=[&](const auto& face)
            ->std::optional<std::size_t> {
        const auto found=std::ranges::find_if(
            terminal.curved_terminal_faces,[&](const auto& candidate) {
                return edge_preview_same_face(candidate,face);
            });
        if(found==terminal.curved_terminal_faces.end())return {};
        return static_cast<std::size_t>(std::distance(
            terminal.curved_terminal_faces.begin(),found));
    };
    const auto cross=[](const Vec3& first,const Vec3& second) {
        return Vec3{first.y*second.z-first.z*second.y,
            first.z*second.x-first.x*second.z,
            first.x*second.y-first.y*second.x};
    };
    std::vector<Vec3> result;
    result.reserve(terminal.section.wire.points.size());
    std::vector<std::optional<std::pair<Vec3,Vec3>>> planes;
    for(const auto& face:terminal.curved_terminal_faces)
        planes.push_back(edge_preview_terminal_plane(mesh,face));
    std::vector<bool> analytic_faces(planes.size());
    for(std::size_t i=0;i<planes.size();++i)
        analytic_faces[i]=planes[i].has_value()||std::ranges::any_of(
            cylinders,[&](const auto& cylinder){return cylinder.first==i;});
    const bool needs_mesh=std::ranges::find(analytic_faces,false)!=analytic_faces.end();
    std::vector<std::optional<double>> previous_parameters(
        terminal.curved_terminal_faces.size());
    for(const auto& origin:terminal.section.wire.points) {
        struct Candidate {Vec3 point;double parameter{};};
        std::vector<std::vector<Candidate>> candidates(
            terminal.curved_terminal_faces.size());
        std::vector<std::vector<Candidate>> analytical_extensions(
            terminal.curved_terminal_faces.size());
        std::vector<Candidate> opposite_caps;
        const double axial_tolerance=1.0e-6*std::max(
            {1.,terminal.extent,terminal.backward_extent});
        const auto within_terminal=[&](double parameter) {
            return parameter>=-terminal.backward_extent-axial_tolerance&&
                parameter<=terminal.extent+axial_tolerance;
        };
        // Plane caps are intersected analytically, including their extension
        // across the new profile. No trimmed face or solid is constructed.
        for(std::size_t i=0;i<terminal.curved_terminal_faces.size();++i) {
            const auto& plane=planes[i];
            if(!plane)continue;
            const double denominator=edge_preview_dot(plane->second,terminal.axis);
            if(std::abs(denominator)<=1.0e-10)continue;
            // A neighbouring cap at the other end of a short route bounds
            // the opposite side. It must not replace this end's curved cut
            // merely because a larger profile can now reach that cap.
            const auto route_interior=edge_preview_offset(terminal.node,
                terminal.axis,.5*terminal.extent);
            const double parameter=edge_preview_dot(plane->second,
                edge_preview_difference(plane->first,origin))/denominator;
            if(edge_preview_dot(plane->second,edge_preview_difference(
                    route_interior,plane->first))/denominator< -axial_tolerance) {
                if(within_terminal(parameter))opposite_caps.push_back({
                    edge_preview_offset(origin,terminal.axis,parameter),parameter});
                continue;
            }
            if(within_terminal(parameter))
                analytical_extensions[i].push_back({edge_preview_offset(origin,
                    terminal.axis,parameter),parameter});
        }
        for(std::size_t triangle=0;
                needs_mesh&&triangle<mesh.triangle_references.size();++triangle) {
            const auto face_index=candidate_face(
                mesh.triangle_references[triangle]);
            if(!face_index||3*triangle+2>=mesh.triangles.size())continue;
            // Exact plane/cylinder intersections must not compete with their
            // tessellated chords. Otherwise a fully materialized rollback
            // mesh can move a terminal away from its analytic boundary.
            if(analytic_faces[*face_index])continue;
            std::array<Vec3,3> vertices;
            bool valid=true;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto index=mesh.triangles[3*triangle+corner];
                if(index>=mesh.vertices.size()) {valid=false;break;}
                vertices[corner]=mesh.vertices[index];
            }
            if(!valid)continue;
            const auto first=edge_preview_difference(vertices[1],
                vertices[0]);
            const auto second=edge_preview_difference(vertices[2],
                vertices[0]);
            const auto normal=cross(first,second);
            const double denominator=edge_preview_dot(normal,terminal.axis);
            const double scale=std::max({1.,edge_preview_length(first),
                edge_preview_length(second)});
            if(std::abs(denominator)<=1.0e-12*scale*scale)continue;
            const double parameter=edge_preview_dot(normal,
                edge_preview_difference(vertices[0],origin))/denominator;
            const auto point=edge_preview_offset(origin,terminal.axis,
                parameter);
            const auto relative=edge_preview_difference(point,vertices[0]);
            const double d00=edge_preview_dot(first,first);
            const double d01=edge_preview_dot(first,second);
            const double d11=edge_preview_dot(second,second);
            const double d20=edge_preview_dot(relative,first);
            const double d21=edge_preview_dot(relative,second);
            const double barycentric_denominator=d00*d11-d01*d01;
            if(std::abs(barycentric_denominator)<=1.0e-20)continue;
            const double v=(d11*d20-d01*d21)/
                barycentric_denominator;
            const double w=(d00*d21-d01*d20)/
                barycentric_denominator;
            constexpr double tolerance=1.0e-6;
            if(v<-tolerance||w<-tolerance||v+w>1.+tolerance)continue;
            if(within_terminal(parameter))
                candidates[*face_index].push_back({point,parameter});
        }
        for(const auto& [face_index,cylinder]:cylinders) {
            const auto subtract_axis=[](const Vec3& value,
                    const Vec3& axis) {
                return edge_preview_difference(value,edge_preview_offset(
                    Vec3{},axis,edge_preview_dot(value,axis)));
            };
            const auto from_center=edge_preview_difference(origin,
                cylinder.center);
            const auto line_direction=subtract_axis(terminal.axis,
                cylinder.axis);
            const auto line_origin=subtract_axis(from_center,
                cylinder.axis);
            const double quadratic=edge_preview_dot(line_direction,
                line_direction);
            if(quadratic<=1.0e-12)continue;
            const double linear=2.*edge_preview_dot(line_origin,
                line_direction);
            const double constant=edge_preview_dot(line_origin,
                line_origin)-cylinder.radius*cylinder.radius;
            double discriminant=linear*linear-4.*quadratic*constant;
            const double tolerance=1.0e-7*std::max(1.,cylinder.radius);
            // Convert the existing radial tolerance to discriminant units.
            // Squaring it alone rejects tangent contacts after tiny axis-fit
            // roundoff and discards the whole terminal's otherwise valid cut.
            const double discriminant_tolerance=4.*quadratic*tolerance*
                (2.*cylinder.radius+tolerance);
            if(discriminant<-discriminant_tolerance)continue;
            discriminant=std::max(0.,discriminant);
            for(const double parameter:{
                    (-linear-std::sqrt(discriminant))/(2.*quadratic),
                    (-linear+std::sqrt(discriminant))/(2.*quadratic)}) {
                const auto point=edge_preview_offset(origin,terminal.axis,
                    parameter);
                const auto relative=edge_preview_difference(point,
                    cylinder.center);
                const double axial=edge_preview_dot(relative,cylinder.axis);
                const auto axis_point=edge_preview_offset(cylinder.center,
                    cylinder.axis,axial);
                const auto radial=edge_preview_difference(point,axis_point);
                if(edge_preview_on_minor_arc(radial,
                        cylinder.first_radial,
                        cylinder.last_radial)) {
                    if(within_terminal(parameter))
                        candidates[face_index].push_back({point,parameter});
                } else if(within_terminal(parameter))
                    analytical_extensions[face_index].push_back(
                        {point,parameter});
            }
        }
        // Beyond the finite cylindrical arc, a reachable adjacent opposite
        // cap closes the envelope. Do not continue the cylinder behind that
        // arc and draw a disconnected sliver on a large-radius profile.
        if(!opposite_caps.empty()&&std::ranges::all_of(candidates,
                [](const auto& values){return values.empty();})&&
                std::ranges::any_of(analytical_extensions,
                    [](const auto& values){return !values.empty();})) {
            result.push_back(std::ranges::min_element(opposite_caps,{},
                &Candidate::parameter)->point);
            continue;
        }
        // A Boolean split can end the locally published cylinder boundary at
        // the source node even though the same analytical terminal surface
        // continues under the prospective Fillet.  Prefer intersections on
        // the trimmed face, but allow that local cylinder continuation when
        // it is the only bounded contact along the selected route.
        std::vector<Candidate> boundaries;
        for(std::size_t face_index=0;face_index<candidates.size();
                ++face_index) {
            if(candidates[face_index].empty())
                candidates[face_index]=
                    std::move(analytical_extensions[face_index]);
            if(candidates[face_index].empty())continue;
            const auto chosen=std::ranges::min_element(
                candidates[face_index],{},[&](const auto& candidate) {
                    return previous_parameters[face_index]
                        ?std::abs(candidate.parameter-
                            *previous_parameters[face_index])
                        :std::abs(candidate.parameter);
                });
            boundaries.push_back(*chosen);
            previous_parameters[face_index]=chosen->parameter;
        }
        if(boundaries.empty())return {};
        // The retained cutter lies inward of every encountered boundary.
        // Along this route-axis ray their intersection therefore starts at
        // the furthest local hit, not whichever surface happened to be
        // visited first.
        result.push_back(std::ranges::max_element(boundaries,{},
            &Candidate::parameter)->point);
    }
    return result;
}

bool edge_preview_clip_curved_terminal(
        std::vector<zima::kernel::ViewerEdge>& edges,
        const FilletPreviewTerminal& terminal,
        const zima::kernel::ViewerMesh& mesh) {
    if(terminal.first_rail>=edges.size()||
       terminal.second_rail>=edges.size()||
       terminal.end_arc>=edges.size())return false;
    auto intersection=edge_preview_curved_terminal_intersection(
        mesh,terminal);
    if(intersection.size()<2)return false;
    auto& first=edges[terminal.first_rail].points;
    auto& second=edges[terminal.second_rail].points;
    if(first.empty()||second.empty())return false;
    const auto trim_rail=[&](auto& rail,const auto& endpoint) {
        if(!terminal.circular_path) {
            (terminal.at_start?rail.front():rail.back())=endpoint;return;
        }
        if(!terminal.at_start)std::ranges::reverse(rail);
        const auto& circle=*terminal.circular_path;
        const auto center=edge_preview_offset(circle.center,circle.axis,
            edge_preview_dot(edge_preview_difference(rail.front(),circle.center),circle.axis));
        const auto original=edge_preview_difference(rail.front(),center);
        const auto angle=[&](const auto& point) {
            const auto radial=edge_preview_difference(point,center);
            return std::atan2(edge_preview_dot(circle.axis,zima::kernel::dimension_cross(original,radial)),
                edge_preview_dot(original,radial));
        };
        const double cut=angle(endpoint),radius=edge_preview_length(original);
        std::size_t remove{};
        while(remove<rail.size()-1&&angle(rail[remove])<cut-1.e-9&&
                std::abs(edge_preview_length(edge_preview_difference(rail[remove],center))-radius)<1.e-7)++remove;
        rail.erase(rail.begin(),rail.begin()+remove);
        if(edge_preview_same_point(rail.front(),endpoint))rail.front()=endpoint;
        else rail.insert(rail.begin(),endpoint);
        if(!terminal.at_start)std::ranges::reverse(rail);
    };
    trim_rail(first,intersection.front());
    trim_rail(second,intersection.back());
    edges[terminal.end_arc].points=std::move(intersection);
    return true;
}

void edge_preview_extend_fillet_node(FilletPreviewTerminal& first,
        FilletPreviewTerminal& second) {
    if(!first.section.radius_center||!second.section.radius_center)return;
    const auto separation=edge_preview_difference(
        *first.section.radius_center,*second.section.radius_center);
    const double cosine=edge_preview_dot(first.axis,second.axis);
    const double denominator=1.-cosine*cosine;
    if(denominator<=1.0e-10)return;
    const double first_projection=edge_preview_dot(first.axis,separation);
    const double second_projection=edge_preview_dot(second.axis,separation);
    const double first_parameter=(cosine*second_projection-
        first_projection)/denominator;
    const double second_parameter=(second_projection-
        cosine*first_projection)/denominator;
    const auto first_closest=edge_preview_offset(*first.section.radius_center,
        first.axis,first_parameter);
    const auto second_closest=edge_preview_offset(
        *second.section.radius_center,second.axis,second_parameter);
    const double first_radius=edge_preview_length(edge_preview_difference(
        first.section.first_tangent,*first.section.radius_center));
    const double second_radius=edge_preview_length(edge_preview_difference(
        second.section.first_tangent,*second.section.radius_center));
    const double tolerance=1.0e-7*std::max({1.,first_radius,second_radius,
        first.extent,second.extent});
    if(edge_preview_length(edge_preview_difference(first_closest,
            second_closest))>tolerance||
       first_parameter>first.extent+tolerance||
       second_parameter>second.extent+tolerance)return;
    const double first_extension=std::max(0.,-first_parameter);
    const double second_extension=std::max(0.,-second_parameter);
    const double extension_limit=2.*(std::max(first_radius,second_radius)+
        std::max(edge_preview_length(edge_preview_difference(first.node,
                     *first.section.radius_center)),
                 edge_preview_length(edge_preview_difference(second.node,
                     *second.section.radius_center))));
    if(first_extension>extension_limit||
       second_extension>extension_limit)return;
    first.backward_extent=first_extension;
    second.backward_extent=second_extension;
}

// A plane/plane Fillet on a straight route is an exact circular cylinder.
// Intersect two such lightweight terminal cylinders directly so the preview
// uses the same trim curve as their simultaneous Body Boolean, without
// calculating an OCCT solid during pointer interaction.
std::optional<std::pair<zima::kernel::Vec3,double>>
edge_preview_terminal_sweep(const zima::kernel::ViewerEdge& edge,
        bool at_start) {
    if(edge.points.size()<2)return {};
    const auto& origin=at_start?edge.points.front():edge.points.back();
    if(const auto circle=edge_preview_circular_path(edge,at_start)) {
        const auto radial=edge_preview_difference(origin,circle->center);
        const auto axis=edge_preview_normalized(zima::kernel::dimension_cross(circle->axis,radial));
        double extent{};
        for(std::size_t i=1;i<edge.points.size();++i)
            extent+=2.*circle->radius*std::asin(std::clamp(edge_preview_length(
                edge_preview_difference(edge.points[i],edge.points[i-1]))/(2.*circle->radius),0.,1.));
        if(axis&&extent>1.e-9)return std::pair{*axis,extent};
    }
    const auto& neighbor=at_start?edge.points[1]:
        edge.points[edge.points.size()-2];
    const auto axis=edge_preview_normalized(
        edge_preview_difference(neighbor,origin));
    if(!axis)return {};
    double extent{};
    const double scale=std::max({1.,std::abs(origin.x),std::abs(origin.y),
        std::abs(origin.z)});
    const double tolerance=1.0e-7*scale;
    for(const auto& point:edge.points) {
        const auto delta=edge_preview_difference(point,origin);
        const double progress=edge_preview_dot(delta,*axis);
        const auto projected=edge_preview_offset(origin,*axis,progress);
        if(progress<-tolerance||edge_preview_length(
                edge_preview_difference(point,projected))>tolerance)
            return {};
        extent=std::max(extent,progress);
    }
    if(extent<=tolerance)return {};
    return std::pair{*axis,extent};
}

bool edge_preview_on_minor_arc(const zima::kernel::Vec3& radial,
        const zima::kernel::Vec3& first,
        const zima::kernel::Vec3& second) {
    const auto value=edge_preview_normalized(radial);
    const auto begin=edge_preview_normalized(first);
    const auto end=edge_preview_normalized(second);
    if(!value||!begin||!end)return false;
    const auto angle=[](const auto& left,const auto& right) {
        return std::acos(std::clamp(edge_preview_dot(left,right),-1.,1.));
    };
    const double complete=angle(*begin,*end);
    return std::abs(angle(*begin,*value)+angle(*value,*end)-complete)<=
        1.0e-5;
}

std::vector<zima::kernel::Vec3> edge_preview_fillet_intersection(
        const FilletPreviewTerminal& first,
        const FilletPreviewTerminal& second) {
    using Vec3=zima::kernel::Vec3;
    if(!first.section.radius_center||!second.section.radius_center)
        return {};
    const auto first_begin=edge_preview_difference(
        first.section.first_tangent,*first.section.radius_center);
    const auto first_end=edge_preview_difference(
        first.section.second_tangent,*first.section.radius_center);
    const auto second_begin=edge_preview_difference(
        second.section.first_tangent,*second.section.radius_center);
    const auto second_end=edge_preview_difference(
        second.section.second_tangent,*second.section.radius_center);
    const double first_radius=edge_preview_length(first_begin);
    const double second_radius=edge_preview_length(second_begin);
    if(first_radius<=1.0e-9||second_radius<=1.0e-9)return {};
    const auto subtract_axis=[](const Vec3& value,const Vec3& axis) {
        return edge_preview_difference(value,edge_preview_offset(
            Vec3{},axis,edge_preview_dot(value,axis)));
    };
    constexpr std::size_t samples=64;
    std::vector<std::vector<Vec3>> spans(1);
    std::optional<Vec3> previous;
    const double scale=std::max({1.,first_radius,second_radius,
        first.extent,second.extent});
    const double tolerance=1.0e-7*scale;
    for(std::size_t sample=0;sample<=samples;++sample) {
        const double ratio=static_cast<double>(sample)/samples;
        const auto radial=edge_preview_blend_direction(first_begin,
            first_end,ratio);
        const auto point=edge_preview_offset(*first.section.radius_center,
            radial,first_radius);
        const auto from_second=edge_preview_difference(point,
            *second.section.radius_center);
        const auto line_direction=subtract_axis(first.axis,second.axis);
        const auto line_origin=subtract_axis(from_second,second.axis);
        const double quadratic=edge_preview_dot(line_direction,
            line_direction);
        const double linear=2.*edge_preview_dot(line_origin,
            line_direction);
        const double constant=edge_preview_dot(line_origin,line_origin)-
            second_radius*second_radius;
        std::vector<std::pair<Vec3,double>> candidates;
        if(quadratic>1.0e-12) {
            double discriminant=linear*linear-4.*quadratic*constant;
            if(discriminant>=-tolerance*tolerance) {
                discriminant=std::max(0.,discriminant);
                for(const double root:{(-linear-std::sqrt(discriminant))/
                            (2.*quadratic),
                        (-linear+std::sqrt(discriminant))/(2.*quadratic)}) {
                    const auto candidate=edge_preview_offset(point,
                        first.axis,root);
                    const auto relative=edge_preview_difference(candidate,
                        *second.section.radius_center);
                    const double second_progress=edge_preview_dot(relative,
                        second.axis);
                    if(root<-first.backward_extent-tolerance||
                            root>first.extent+tolerance||
                            second_progress<-
                                second.backward_extent-tolerance||
                            second_progress>second.extent+tolerance)
                        continue;
                    const auto second_center=edge_preview_offset(
                        *second.section.radius_center,second.axis,
                        second_progress);
                    const auto second_radial=edge_preview_difference(
                        candidate,second_center);
                    if(!edge_preview_on_minor_arc(second_radial,
                            second_begin,second_end))continue;
                    const double score=previous?edge_preview_length(
                        edge_preview_difference(candidate,*previous)):
                        root+second_progress;
                    candidates.push_back({candidate,score});
                }
            }
        }
        if(candidates.empty()) {
            if(!spans.back().empty())spans.emplace_back();
            previous.reset();
            continue;
        }
        const auto chosen=std::ranges::min_element(candidates,{},
            [](const auto& candidate) {return candidate.second;});
        if(spans.back().empty()||!edge_preview_same_point(
                spans.back().back(),chosen->first))
            spans.back().push_back(chosen->first);
        previous=chosen->first;
    }
    const auto longest=std::ranges::max_element(spans,{},
        [](const auto& span) {return span.size();});
    if(longest==spans.end()||longest->size()<3||
            edge_preview_length(edge_preview_difference(
                longest->front(),longest->back()))<=tolerance)
        return {};
    return *longest;
}

// Revolve the circular Fillet or straight Chamfer section around the corner.
// Only the boundary curves are sampled here; there is no BRep or Boolean.
bool edge_preview_arc_node(const FilletPreviewTerminal& first,
        const FilletPreviewTerminal& second,
        std::vector<zima::kernel::ViewerEdge>& connectors) {
    // Compact rollback packets can omit oriented surface descriptors. Their
    // exact section directions still prove the matching swept profiles below;
    // reject a known mixed sense without disabling those saved-model nodes.
    if(first.adds_material&&second.adds_material&&first.adds_material!=second.adds_material)
        return false;
    const double cosine=edge_preview_dot(first.axis,second.axis);
    const auto n1=edge_preview_normalized(edge_preview_difference(second.axis,
        edge_preview_offset({},first.axis,cosine)));
    const auto n2=edge_preview_normalized(edge_preview_difference(first.axis,
        edge_preview_offset({},second.axis,cosine)));
    if(!n1||!n2)return false;
    const auto axis=edge_preview_normalized(zima::kernel::dimension_cross(*n1,*n2));
    if(!axis)return false;
    const double sweep=std::acos(std::clamp(edge_preview_dot(*n1,*n2),-1.,1.));
    const auto rotate=[&](const auto& point,double angle) {
        const auto p=edge_preview_difference(point,first.node);
        auto rotated=edge_preview_offset(first.node,p,std::cos(angle));
        rotated=edge_preview_offset(rotated,zima::kernel::dimension_cross(*axis,p),std::sin(angle));
        return edge_preview_offset(rotated,*axis,edge_preview_dot(*axis,p)*(1.-std::cos(angle)));
    };
    const auto& a=first.section.wire.points;
    const auto& b=second.section.wire.points;
    if(a.size()<2||b.size()<2)return false;
    // A rounded connector fills the gap behind both branch ends. Sections
    // pointing into the other branch already intersect; revolving them would
    // merely draw a second, buried solution with no distinct body boundary.
    const double scale=std::max({1.,first.section.first_distance,
        first.section.second_distance});
    if(std::ranges::any_of(a,[&](const auto& p) {
            return edge_preview_dot(edge_preview_difference(p,first.node),*n1)>1.e-7*scale;
        })||std::ranges::any_of(b,[&](const auto& p) {
            return edge_preview_dot(edge_preview_difference(p,second.node),*n2)>1.e-7*scale;
        }))return false;
    const auto matches=[&](const auto& p,const auto& q) {
        return edge_preview_length(edge_preview_difference(p,q))<=
            1.0e-6*std::max(1.,first.section.first_distance);
    };
    if(!((matches(rotate(a.front(),sweep),b.front())&&matches(rotate(a.back(),sweep),b.back()))||
         (matches(rotate(a.front(),sweep),b.back())&&matches(rotate(a.back(),sweep),b.front()))))return false;
    for(const auto& endpoint:{a.front(),a.back()}) {
        if(matches(endpoint,rotate(endpoint,sweep)))continue;
        zima::kernel::ViewerEdge rail;rail.overlay=true;
        constexpr std::size_t samples=64;
        for(std::size_t i=0;i<=samples;++i)
            rail.points.push_back(rotate(endpoint,sweep*static_cast<double>(i)/samples));
        connectors.push_back(std::move(rail));
    }
    return true;
}

std::vector<zima::kernel::Vec3> edge_preview_chamfer_intersection(
        const FilletPreviewTerminal& first,const FilletPreviewTerminal& second) {
    if(first.adds_material&&second.adds_material&&first.adds_material!=second.adds_material)
        return {};
    const auto normal=edge_preview_difference(first.axis,second.axis);
    const double denominator=edge_preview_dot(normal,first.axis);
    if(std::abs(denominator)<=1.e-10)return {};
    std::vector<zima::kernel::Vec3> result;
    for(const auto& p:first.section.wire.points) {
        const double t=edge_preview_dot(normal,edge_preview_difference(first.node,p))/denominator;
        if(t>first.extent)return {};
        result.push_back(edge_preview_offset(p,first.axis,t));
    }
    return result;
}

void edge_preview_clip_terminal_rails(
        std::vector<zima::kernel::ViewerEdge>& edges,
        const FilletPreviewTerminal& terminal,
        const std::vector<zima::kernel::Vec3>& intersection) {
    if(intersection.empty())return;
    const double scale=std::max(1.,terminal.extent);
    const double tolerance=1.0e-6*scale;
    for(const auto rail_index:{terminal.first_rail,terminal.second_rail}) {
        if(rail_index>=edges.size()||edges[rail_index].points.empty())
            continue;
        auto& rail=edges[rail_index].points;
        auto& endpoint=terminal.at_start?rail.front():rail.back();
        for(const auto& candidate:{intersection.front(),
                intersection.back()}) {
            const auto delta=edge_preview_difference(candidate,endpoint);
            const double progress=edge_preview_dot(delta,terminal.axis);
            const auto projected=edge_preview_offset(endpoint,terminal.axis,
                progress);
            if(progress>=-terminal.backward_extent-tolerance&&
                    progress<=terminal.extent+tolerance&&
                    edge_preview_length(edge_preview_difference(
                        candidate,projected))<=tolerance) {
                endpoint=candidate;
                break;
            }
        }
    }
}

bool edge_preview_three_branch_node(std::vector<zima::kernel::ViewerEdge>& edges,
        std::array<FilletPreviewTerminal*,3> terminals,
        std::vector<zima::kernel::ViewerEdge>& connectors) {
    for(std::size_t a=0;a<3;++a) {
        if(!terminals[a]->section.radius_center||!terminals[a]->adds_material)return false;
        for(std::size_t b=a+1;b<3;++b)
            if(std::abs(edge_preview_dot(terminals[a]->axis,terminals[b]->axis))>1.0e-7)return false;
    }
    const double radius=edge_preview_length(edge_preview_difference(
        terminals[0]->section.first_tangent,*terminals[0]->section.radius_center));
    for(const auto* terminal:terminals)
        if(radius>=terminal->extent||std::abs(radius-edge_preview_length(
                edge_preview_difference(terminal->section.first_tangent,
                    *terminal->section.radius_center)))>1.0e-7)return false;
    const auto subtractive=std::ranges::count_if(terminals,
        [](const auto* t){return !*t->adds_material;});
    if(subtractive!=0&&subtractive!=1&&subtractive!=3)return false;
    // The spherical corner meets each cylinder one radius along its branch.
    // Mixed corners have the same three contact planes and a toroidal elbow.
    for(auto* terminal:terminals) {
        const auto moved=[&](const auto& p){return edge_preview_offset(p,terminal->axis,radius);};
        for(const auto rail:{terminal->first_rail,terminal->second_rail}) {
            auto& points=edges[rail].points;
            auto& endpoint=terminal->at_start?points.front():points.back();
            endpoint=moved(endpoint);
        }
        for(auto& p:terminal->section.wire.points)p=moved(p);
        terminal->section.first_tangent=moved(terminal->section.first_tangent);
        terminal->section.second_tangent=moved(terminal->section.second_tangent);
        terminal->section.radius_center=moved(*terminal->section.radius_center);
        edges[terminal->end_arc]=terminal->section.wire;
    }
    if(subtractive==1) {
        std::array<FilletPreviewTerminal,2> inner;
        std::size_t index{};
        auto origin=terminals[0]->node;
        for(const auto* terminal:terminals)if(*terminal->adds_material) {
            inner[index++]=*terminal;
            origin=edge_preview_offset(origin,terminal->axis,radius);
        }
        inner[0].node=inner[1].node=origin;
        static_cast<void>(edge_preview_arc_node(inner[0],inner[1],connectors));
    }
    return true;
}

std::vector<EdgeTreatmentTerminalNode> edge_treatment_terminal_nodes(
        const std::vector<EdgeTreatmentPreviewGeometry::TerminalLabel>& labels) {
    struct NodeWithVertex {
        EdgeTreatmentTerminalNode node;
        zima::kernel::VertexReference vertex;
    };
    std::vector<NodeWithVertex> grouped;
    grouped.reserve(labels.size());
    for(const auto& label:labels) {
        auto existing=label.vertex.valid()
            ?std::ranges::find_if(grouped,[&](const auto& candidate) {
                return candidate.vertex==label.vertex;
            }):grouped.end();
        if(existing==grouped.end()) {
            grouped.push_back({{label.point,label.text,
                {{label.route,label.first}}},label.vertex});
            continue;
        }
        existing->node.text+='-';
        existing->node.text+=label.text;
        existing->node.route_ends.emplace_back(label.route,label.first);
    }
    std::vector<EdgeTreatmentTerminalNode> result;
    result.reserve(grouped.size());
    for(auto& entry:grouped)result.push_back(std::move(entry.node));
    return result;
}

EdgeTreatmentPreviewGeometry edge_treatment_preview_wire(
    const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
    const zima::document::EdgeTreatmentParameters& parameters,
    zima::document::FeatureKind kind, const std::string& owner_id,
    const zima::kernel::ViewerMesh* input_mesh) {
    EdgeTreatmentPreviewGeometry result;
    const bool fillet = kind == zima::document::FeatureKind::Fillet;
    if (!std::isfinite(parameters.primary_size) ||
        parameters.primary_size <= 1.0e-9) return result;
    std::vector<FilletPreviewTerminal> fillet_terminals;
    std::vector<std::pair<std::size_t,std::size_t>> path_rails;
    std::size_t source_path_index=1;
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto group = zima::document::actual_edge_treatment_intervals(
            groups[group_index]);
        auto ordered_paths=ordered_edge_treatment_preview_paths(group);
        const bool one_unambiguous_path=ordered_paths.size()==1;
        for (auto path : std::move(ordered_paths)) {
            if (path.edges.empty()) continue;
            const auto nodes=edge_treatment_preview_path_nodes(path);
            const bool open=nodes.size()>=2&&
                !edge_preview_same_point(path.edges.front().points.front(),
                    path.edges.back().points.back());
            const auto node_text=[&](std::size_t node) {
                return std::to_string(source_path_index)+"."+
                    std::to_string(node+1);
            };
            for(std::size_t node=0;node<nodes.size();++node) {
                const bool terminal=open&&one_unambiguous_path&&
                    (node==0||node+1==nodes.size());
                if(!terminal)result.labels.emplace_back(nodes[node],
                    node_text(node));
            }
            if(open&&one_unambiguous_path&&
               path.edges.front().edge_treatment_endpoint_references.size()==2&&
               path.edges.back().edge_treatment_endpoint_references.size()==2) {
                result.terminal_labels.push_back({nodes.front(),node_text(0),
                    group_index,true,path.edges.front().
                        edge_treatment_endpoint_references.front()});
                result.terminal_labels.push_back({nodes.back(),
                    node_text(nodes.size()-1),group_index,false,
                    path.edges.back().edge_treatment_endpoint_references.back()});
            }
            ++source_path_index;
            double path_length{};
            for (const auto& edge : path.edges) {
                for (std::size_t point = 1; point < edge.points.size(); ++point) {
                    path_length += edge_preview_length(edge_preview_difference(
                        edge.points[point], edge.points[point - 1]));
                }
            }
            const bool linear_fillet = fillet &&
                parameters.fillet_mode ==
                    zima::document::EdgeTreatmentParameters::FilletMode::Linear;
            bool semantic_start_is_displayed_start = true;
            if (linear_fillet &&
                path.edges.front().edge_treatment_endpoint_references.size() == 2 &&
                group_index < parameters.route_start_vertices.size()) {
                const auto& start = path.edges.front()
                    .edge_treatment_endpoint_references.front();
                semantic_start_is_displayed_start =
                    start == parameters.route_start_vertices[group_index];
            }
            const bool primary_at_displayed_start =
                semantic_start_is_displayed_start != parameters.reverse;
            const auto radius_at = [&](double distance) {
                if (!linear_fillet || path_length <= 1.0e-9) {
                    return parameters.primary_size;
                }
                double ratio = std::clamp(distance / path_length, 0.0, 1.0);
                if (!primary_at_displayed_start) ratio = 1.0 - ratio;
                return parameters.primary_size * (1.0 - ratio) +
                    parameters.secondary_size * ratio;
            };
            zima::kernel::ViewerEdge first_rail;
            zima::kernel::ViewerEdge second_rail;
            first_rail.overlay = true;
            second_rail.overlay = true;
            std::optional<EdgeTreatmentSection> first_section;
            std::optional<EdgeTreatmentSection> last_section;
            std::optional<EdgeTreatmentSection> first_measurable_section;
            std::optional<EdgeTreatmentSection> last_measurable_section;
            bool rails_valid = true;
            double traversed{};
            const auto section_at=[&](const auto& edge,
                    std::size_t point_index,double section_radius,
                    bool allow_degenerate_terminal=false) {
                // The calculated support-face sections are the authoritative
                // lightweight source for a rolling Fillet. Try them first so
                // plane/circle, circle/circle, ellipse/circle and spline/circle
                // contacts do not fall back to an old tangent-line cage merely
                // because those tangents happen to form a finite angle.
                const bool planar_pair=input_mesh&&edge.edge_treatment_side_references.size()==2&&
                    std::ranges::all_of(edge.edge_treatment_side_references,[&](const auto& face) {
                        const auto surface=edge_preview_surface(*input_mesh,face);
                        return surface&&surface->kind==zima::kernel::SurfaceGeometry::Kind::Plane;
                    });
                if(fillet&&input_mesh&&!planar_pair)
                    if(auto curved=edge_treatment_curved_section(*input_mesh,
                            edge,point_index,section_radius))
                        return curved;
                auto section=edge_treatment_section(edge.points[point_index],
                    edge.edge_treatment_side_directions[0][point_index],
                    edge.edge_treatment_side_directions[1][point_index],
                    parameters,fillet,section_radius);
                if(!section&&fillet&&allow_degenerate_terminal) {
                    const auto first=edge_preview_normalized(
                        edge.edge_treatment_side_directions[0][point_index]);
                    const auto second=edge_preview_normalized(
                        edge.edge_treatment_side_directions[1][point_index]);
                    if(first&&second&&edge_preview_dot(*first,*second)<
                            -1.0+1.0e-8) {
                        // Two support faces can become tangent with opposite
                        // material directions exactly where a valid rolling
                        // Fillet dies into the preceding surface.  The exact
                        // terminal section is one point: both tangent rails
                        // converge there.  Do not discard the complete route
                        // merely because this limiting section has no finite
                        // circle of its own.
                        EdgeTreatmentSection limit;
                        limit.wire.overlay=true;
                        limit.wire.points={edge.points[point_index]};
                        limit.first_tangent=edge.points[point_index];
                        limit.second_tangent=edge.points[point_index];
                        limit.curved_support=true;
                        section=std::move(limit);
                    }
                }
                return section;
            };
            for (std::size_t edge_index = 0;
                 edge_index < path.edges.size(); ++edge_index) {
                const auto& edge = path.edges[edge_index];
                bool swap_section_sides{};
                auto start=section_at(edge,0,radius_at(traversed),
                    edge_index==0);
                if (!start) {
                    rails_valid = false;
                    break;
                }
                if (!first_rail.points.empty()) {
                    const double straight = edge_preview_length(
                            edge_preview_difference(first_rail.points.back(),
                                start->first_tangent)) +
                        edge_preview_length(edge_preview_difference(
                            second_rail.points.back(), start->second_tangent));
                    const double swapped = edge_preview_length(
                            edge_preview_difference(first_rail.points.back(),
                                start->second_tangent)) +
                        edge_preview_length(edge_preview_difference(
                            second_rail.points.back(), start->first_tangent));
                    if (swapped + 1.0e-9 < straight) {
                        swap_section_sides = true;
                        edge_preview_swap_section_sides(*start);
                    }
                }
                if (!start) {
                    rails_valid = false;
                    break;
                }
                if (!first_section) first_section = *start;
                if (edge_index != 0) {
                    edge_preview_append_corner(first_rail.points,
                        edge.points.front(), start->first_tangent);
                    edge_preview_append_corner(second_rail.points,
                        edge.points.front(), start->second_tangent);
                }
                for (std::size_t point_index = edge_index == 0 ? 0 : 1;
                     point_index < edge.points.size(); ++point_index) {
                    if (point_index > 0) {
                        traversed += edge_preview_length(edge_preview_difference(
                            edge.points[point_index],
                            edge.points[point_index - 1]));
                    }
                    auto section=section_at(edge,point_index,
                        radius_at(traversed),
                        (edge_index==0&&point_index==0)||
                            (edge_index+1==path.edges.size()&&
                             point_index+1==edge.points.size()));
                    if (!section) {
                        rails_valid = false;
                        break;
                    }
                    if (swap_section_sides) {
                        edge_preview_swap_section_sides(*section);
                    }
                    first_rail.points.push_back(section->first_tangent);
                    second_rail.points.push_back(section->second_tangent);
                    if(section->radius_center&&section->radius_rim) {
                        if(!first_measurable_section)
                            first_measurable_section=*section;
                        last_measurable_section=*section;
                    }
                    last_section = std::move(section);
                }
                if (!rails_valid) break;
            }
            if (!rails_valid || !first_section || !last_section ||
                first_rail.points.size() < 2 || second_rail.points.size() < 2) {
                continue;
            }
            const auto edge_offset=result.edges.size();
            result.edges.push_back(std::move(first_rail));
            result.edges.push_back(std::move(second_rail));
            result.edges.push_back(first_section->wire);
            result.edges.push_back(last_section->wire);
            path_rails.emplace_back(edge_offset,edge_offset+1);
            for(const bool at_start:{true,false}) {
                const auto& edge=at_start?path.edges.front():path.edges.back();
                const auto& section=at_start?*first_section:*last_section;
                const auto circle=edge_preview_circular_path(edge,at_start);
                if(const auto sweep=edge_preview_terminal_sweep(edge,at_start)) {
                    const auto& node=at_start?edge.points.front():edge.points.back();
                    const auto preceding=circle?edge_preview_rotate(node,*circle,
                        std::min(.1,.1*sweep->second/circle->radius)):
                        edge_preview_offset(node,sweep->first,.1*sweep->second);
                    fillet_terminals.push_back({group_index,node,sweep->first,
                        sweep->second,0.,section,edge_offset,edge_offset+1,
                        edge_offset+(at_start?2:3),at_start,
                        input_mesh?edge_preview_terminal_faces(*input_mesh,edge,at_start,
                            sweep->first,radius_at(at_start?0.:path_length),circle?&preceding:nullptr):
                            std::vector<zima::kernel::FaceReference>{},
                        edge_preview_adds_material(input_mesh,edge,section,node),
                        edge.edge_treatment_endpoint_references.size()==2?
                            edge.edge_treatment_endpoint_references[at_start?0:1]:
                            zima::kernel::VertexReference{},circle});
                }
            }
            if (result.dimensions.empty()) {
                const auto radius_dimension = [&](const EdgeTreatmentSection& section,
                                                   double value,
                                                   const char* key) {
                    if (!section.radius_center || !section.radius_rim) return;
                    zima::kernel::ViewerDimension dimension;
                    dimension.value = value;
                    dimension.reference = {owner_id,
                        std::string{"parameter:"} + key, {}};
                    dimension.kind = zima::kernel::ViewerDimensionKind::Radius;
                    dimension.plane_normal=section.plane_normal;
                    dimension.witness_first = *section.radius_center;
                    dimension.witness_second = *section.radius_rim;
                    dimension.line_first = dimension.witness_first;
                    dimension.line_second = dimension.witness_second;
                    dimension.label_prefix = "R";
                    result.dimensions.push_back(std::move(dimension));
                };
                const auto linear_dimension = [&](const zima::kernel::Vec3& tangent,
                                                   double value,
                                                   const char* key) {
                    zima::kernel::ViewerDimension dimension;
                    dimension.value = value;
                    dimension.reference = {owner_id,
                        std::string{"parameter:"} + key, {}};
                    dimension.kind = zima::kernel::ViewerDimensionKind::Linear;
                    dimension.plane_normal = first_section->plane_normal;
                    dimension.witness_first = path.edges.front().points.front();
                    dimension.witness_second = tangent;
                    // Put each distance in its adjacent-face plane. The shared
                    // dimension envelope then offsets both beyond the route's
                    // start, instead of across the solid in a common section plane.
                    const auto& start_edge = path.edges.front();
                    const auto outside = edge_preview_difference(
                        start_edge.points.front(), start_edge.points[1]);
                    const auto measured = edge_preview_difference(
                        tangent, dimension.witness_first);
                    if (const auto normal = edge_preview_normalized(
                            zima::kernel::dimension_cross(measured, outside)))
                        dimension.plane_normal = *normal;
                    const auto shift_direction = zima::kernel::dimension_unit(
                        zima::kernel::dimension_cross(dimension.plane_normal, measured));
                    const double shift_distance = std::max(0.5, value * 0.35);
                    dimension.line_first = edge_preview_offset(
                        dimension.witness_first, shift_direction, shift_distance);
                    dimension.line_second = edge_preview_offset(
                        dimension.witness_second, shift_direction, shift_distance);
                    dimension.label_prefix.clear();
                    result.dimensions.push_back(std::move(dimension));
                };
                if (fillet) {
                    if (linear_fillet) {
                        radius_dimension(first_measurable_section.value_or(
                                *first_section),radius_at(0.0),
                            primary_at_displayed_start
                                ? "primary" : "secondary");
                        radius_dimension(last_measurable_section.value_or(
                                *last_section),radius_at(path_length),
                            primary_at_displayed_start
                                ? "secondary" : "primary");
                    } else {
                        radius_dimension(first_measurable_section.value_or(
                                *first_section),
                            parameters.primary_size, "primary");
                    }
                } else {
                    linear_dimension(first_section->first_tangent,
                        parameters.primary_size, "primary");
                    using Mode =
                        zima::document::EdgeTreatmentParameters::ChamferMode;
                    if (parameters.chamfer_mode == Mode::TwoDistances) {
                        linear_dimension(first_section->second_tangent,
                            parameters.secondary_size, "secondary");
                    } else if (parameters.chamfer_mode == Mode::DistanceAngle) {
                        const auto vertex = first_section->first_tangent;
                        const auto first_ray = edge_preview_difference(
                            path.edges.front().points.front(), vertex);
                        const auto second_ray = edge_preview_difference(
                            first_section->second_tangent, vertex);
                        const zima::kernel::Vec3 cross{
                            first_ray.y * second_ray.z - first_ray.z * second_ray.y,
                            first_ray.z * second_ray.x - first_ray.x * second_ray.z,
                            first_ray.x * second_ray.y - first_ray.y * second_ray.x};
                        if (const auto normal = edge_preview_normalized(cross)) {
                            zima::kernel::ViewerDimension dimension;
                            dimension.kind =
                                zima::kernel::ViewerDimensionKind::Angular;
                            dimension.value = parameters.angle_degrees;
                            dimension.sweep_degrees = parameters.angle_degrees;
                            dimension.unit_suffix = "°";
                            dimension.reference = {owner_id,
                                "parameter:treatment_angle", {}};
                            dimension.witness_first = vertex;
                            dimension.witness_second =
                                first_section->second_tangent;
                            dimension.line_first =
                                path.edges.front().points.front();
                            dimension.line_second =
                                first_section->second_tangent;
                            dimension.plane_normal = *normal;
                            result.dimensions.push_back(std::move(dimension));
                        }
                    }
                }
            }
        }
    }
    const auto capture_terminal_contacts=[&] {
        for(const auto [first_index,second_index]:path_rails) {
            const auto& first=result.edges[first_index].points;
            const auto& second=result.edges[second_index].points;
            if(first.empty()||second.empty())continue;
            result.terminal_contacts.insert(result.terminal_contacts.end(),
                {first.front(),second.front(),first.back(),second.back()});
        }
    };
    if(!fillet_terminals.empty()) {
        using EndMode=
            zima::document::EdgeTreatmentParameters::RouteEndMode;
        const auto authored_first=[&](const FilletPreviewTerminal& terminal) {
            if(terminal.vertex.valid()&&terminal.group<parameters.route_end_faces.size()) {
                const auto& ends=parameters.route_end_faces[terminal.group];
                if(terminal.vertex==ends.first_vertex)return true;
                if(terminal.vertex==ends.last_vertex)return false;
            }
            return terminal.at_start;
        };
        const auto terminal_mode=[&](const FilletPreviewTerminal& terminal) {
            if(terminal.group>=parameters.route_end_faces.size())
                return EndMode::NoCut;
            const auto& ends=parameters.route_end_faces[terminal.group];
            return authored_first(terminal)?ends.first_mode:ends.last_mode;
        };
        std::vector<bool> remove_edge(result.edges.size());
        std::vector<bool> consumed(fillet_terminals.size());
        std::vector<zima::kernel::ViewerEdge> intersections;
        for(std::size_t first=0;first<fillet_terminals.size();++first) {
            if(consumed[first])continue;
            std::vector<std::size_t> node;
            for(std::size_t second=0;
                    second<fillet_terminals.size();++second)
                if(!consumed[second]&&edge_preview_same_point(
                        fillet_terminals[first].node,
                        fillet_terminals[second].node))
                    node.push_back(second);
            for(const auto terminal:node)consumed[terminal]=true;
            bool joined{};
            if(fillet&&node.size()==3)
                joined=edge_preview_three_branch_node(result.edges,
                    {&fillet_terminals[node[0]],&fillet_terminals[node[1]],
                     &fillet_terminals[node[2]]},intersections);
            if(node.size()==2&&
               terminal_mode(fillet_terminals[node[0]])==EndMode::RoundedArc&&
               terminal_mode(fillet_terminals[node[1]])==EndMode::RoundedArc)
                joined=edge_preview_arc_node(fillet_terminals[node[0]],
                    fillet_terminals[node[1]],intersections);
            if(node.size()==2&&fillet_terminals[node[0]].group!=
                    fillet_terminals[node[1]].group&&
               terminal_mode(fillet_terminals[node[0]])==
                    EndMode::RoundedJoin&&
               terminal_mode(fillet_terminals[node[1]])==
                    EndMode::RoundedJoin) {
                if(fillet)edge_preview_extend_fillet_node(fillet_terminals[node[0]],
                    fillet_terminals[node[1]]);
                auto intersection=fillet?edge_preview_fillet_intersection(
                    fillet_terminals[node[0]],fillet_terminals[node[1]]):
                    edge_preview_chamfer_intersection(fillet_terminals[node[0]],fillet_terminals[node[1]]);
                if(!intersection.empty()) {
                    edge_preview_clip_terminal_rails(result.edges,
                        fillet_terminals[node[0]],intersection);
                    edge_preview_clip_terminal_rails(result.edges,
                        fillet_terminals[node[1]],intersection);
                    remove_edge[fillet_terminals[node[0]].end_arc]=true;
                    remove_edge[fillet_terminals[node[1]].end_arc]=true;
                    zima::kernel::ViewerEdge wire;
                    wire.overlay=true;
                    wire.points=std::move(intersection);
                    intersections.push_back(std::move(wire));
                    joined=true;
                }
            }
            if(joined||!input_mesh)continue;
            for(const auto terminal:node)
                if(terminal_mode(fillet_terminals[terminal])==EndMode::Cut) {
                    auto& end=fillet_terminals[terminal];
                    const auto& definition=parameters.route_end_faces[end.group];
                    const auto& face=authored_first(end)?definition.first:definition.last;
                    end.backward_extent=2.*std::max(end.section.first_distance,
                        end.section.second_distance);
                    if(face.valid()) {
                        // A selected local face still denotes the complete
                        // adjacent envelope. A remote face denotes extension
                        // to that surface and may lie farther away than R.
                        if(std::ranges::find(end.curved_terminal_faces,face)==
                                end.curved_terminal_faces.end()) {
                            end.curved_terminal_faces={face};
                            for(const auto& point:input_mesh->vertices)
                                end.backward_extent=std::max(end.backward_extent,
                                    -edge_preview_dot(edge_preview_difference(point,end.node),end.axis)+
                                    2.*std::max(end.section.first_distance,end.section.second_distance));
                            if(const auto surface=edge_preview_surface(*input_mesh,face))
                                end.backward_extent=std::max(end.backward_extent,
                                    edge_preview_length(edge_preview_difference(surface->origin,end.node))+
                                    2.*std::max(end.section.first_distance,end.section.second_distance));
                        }
                    }
                    static_cast<void>(edge_preview_clip_curved_terminal(
                        result.edges,end,*input_mesh));
                }
        }
        // Terminal clipping above changes the rail endpoints. Capture those
        // final analytical contacts before compacting replaced section wires;
        // route-end labels intentionally remain independent of this step.
        capture_terminal_contacts();
        std::vector<zima::kernel::ViewerEdge> retained;
        retained.reserve(result.edges.size()+intersections.size());
        for(std::size_t index=0;index<result.edges.size();++index)
            if(!remove_edge[index])
                retained.push_back(std::move(result.edges[index]));
        retained.insert(retained.end(),
            std::make_move_iterator(intersections.begin()),
            std::make_move_iterator(intersections.end()));
        result.edges=std::move(retained);
    } else capture_terminal_contacts();
    return result;
}

std::vector<std::pair<std::size_t,bool>> edge_treatment_authored_node_ends(
        const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
        const zima::document::EdgeTreatmentParameters& parameters,
        const std::vector<std::pair<std::size_t,bool>>& route_ends) {
    auto authored_ends=route_ends;
    for(auto& [route,first]:authored_ends) {
        if(route>=parameters.route_end_faces.size())continue;
        const auto paths=ordered_edge_treatment_preview_paths(groups.at(route));
        if(paths.size()!=1||paths.front().edges.empty())continue;
        const auto& edge=first?paths.front().edges.front():paths.front().edges.back();
        if(edge.edge_treatment_endpoint_references.size()!=2)continue;
        const auto& vertex=edge.edge_treatment_endpoint_references[first?0:1];
        const auto& ends=parameters.route_end_faces[route];
        if(vertex.valid()&&vertex==ends.first_vertex)first=true;
        else if(vertex.valid()&&vertex==ends.last_vertex)first=false;
    }
    return authored_ends;
}

EdgeTreatmentNodeSolutions edge_treatment_preview_node_solutions(
        const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
        const zima::document::HistoryContainer& feature,
        const zima::kernel::ViewerMesh& input_mesh,
        const std::vector<std::pair<std::size_t,bool>>& route_ends) {
    using Mode=zima::document::EdgeTreatmentParameters::RouteEndMode;
    if(route_ends.size()!=2)return {};
    if(feature.feature_kind==zima::document::FeatureKind::Chamfer&&
        feature.edge_treatment.chamfer_mode!=zima::document::EdgeTreatmentParameters::ChamferMode::EqualDistance)return {};
    auto definition=feature.edge_treatment;
    // Missing optional end overrides mean the automatic defaults. Ordinary
    // inspection reads the stored definition before Properties learns ends.
    definition.route_end_faces.resize(groups.size());
    const auto authored_ends=edge_treatment_authored_node_ends(groups,definition,route_ends);
    const auto wire=[&](Mode mode) {
        for(const auto [route,first]:authored_ends) {
            auto& end=definition.route_end_faces.at(route);
            (first?end.first_mode:end.last_mode)=mode;
            (first?end.first:end.last)={};
        }
        return edge_treatment_preview_wire(groups,definition,feature.feature_kind,feature.id,&input_mesh);
    };
    const auto uncut=wire(Mode::NoCut);
    const auto differs=[&](const auto& value) {
        if(value.edges.size()!=uncut.edges.size())return true;
        for(std::size_t i=0;i<value.edges.size();++i)
            if(value.edges[i].points!=uncut.edges[i].points)return true;
        return false;
    };
    return {differs(wire(Mode::RoundedJoin)),differs(wire(Mode::RoundedArc))};
}

} // namespace zima::app::workspace_detail
