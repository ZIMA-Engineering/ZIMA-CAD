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
        std::size_t seed{};
        bool reverse_seed{};
        for (std::size_t index = 0; index < edges.size(); ++index) {
            const bool front_free =
                connection_count(edges[index].points.front(), index) == 0;
            const bool back_free =
                connection_count(edges[index].points.back(), index) == 0;
            if (!front_free && !back_free) continue;
            seed = index;
            reverse_seed = !front_free && back_free;
            break;
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

struct FilletPreviewTerminal {
    std::size_t group{};
    zima::kernel::Vec3 node;
    zima::kernel::Vec3 axis;
    double extent{};
    EdgeTreatmentSection section;
    std::size_t first_rail{};
    std::size_t second_rail{};
    std::size_t end_arc{};
    bool at_start{};
};

// A plane/plane Fillet on a straight route is an exact circular cylinder.
// Intersect two such lightweight terminal cylinders directly so the preview
// uses the same trim curve as their simultaneous Body Boolean, without
// calculating an OCCT solid during pointer interaction.
std::optional<std::pair<zima::kernel::Vec3,double>>
edge_preview_terminal_sweep(const zima::kernel::ViewerEdge& edge,
        bool at_start) {
    if(edge.points.size()<2)return {};
    const auto& origin=at_start?edge.points.front():edge.points.back();
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
                    if(root<-tolerance||root>first.extent+tolerance||
                            second_progress<-tolerance||
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
            if(progress>=-tolerance&&progress<=terminal.extent+tolerance&&
                    edge_preview_length(edge_preview_difference(
                        candidate,projected))<=tolerance) {
                endpoint=candidate;
                break;
            }
        }
    }
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
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto group = zima::document::actual_edge_treatment_intervals(
            groups[group_index]);
        for (auto path : ordered_edge_treatment_preview_paths(group)) {
            if (path.edges.empty()) continue;
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
            bool rails_valid = true;
            double traversed{};
            const auto section_at=[&](const auto& edge,
                    std::size_t point_index,double section_radius) {
                // The calculated support-face sections are the authoritative
                // lightweight source for a rolling Fillet. Try them first so
                // plane/circle, circle/circle, ellipse/circle and spline/circle
                // contacts do not fall back to an old tangent-line cage merely
                // because those tangents happen to form a finite angle.
                if(fillet&&input_mesh)
                    if(auto curved=edge_treatment_curved_section(*input_mesh,
                            edge,point_index,section_radius))
                        return curved;
                return edge_treatment_section(edge.points[point_index],
                    edge.edge_treatment_side_directions[0][point_index],
                    edge.edge_treatment_side_directions[1][point_index],
                    parameters,fillet,section_radius);
            };
            for (std::size_t edge_index = 0;
                 edge_index < path.edges.size(); ++edge_index) {
                auto& edge = path.edges[edge_index];
                auto start=section_at(edge,0,radius_at(traversed));
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
                    if (swapped < straight) {
                        std::swap(edge.edge_treatment_side_directions[0],
                                  edge.edge_treatment_side_directions[1]);
                        start=section_at(edge,0,radius_at(traversed));
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
                        radius_at(traversed));
                    if (!section) {
                        rails_valid = false;
                        break;
                    }
                    first_rail.points.push_back(section->first_tangent);
                    second_rail.points.push_back(section->second_tangent);
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
            if(fillet) {
                if(!first_section->curved_support) {
                    if(const auto sweep=edge_preview_terminal_sweep(
                            path.edges.front(),true))
                        fillet_terminals.push_back({group_index,
                            path.edges.front().points.front(),sweep->first,
                            sweep->second,*first_section,edge_offset,
                            edge_offset+1,edge_offset+2,true});
                }
                if(!last_section->curved_support) {
                    if(const auto sweep=edge_preview_terminal_sweep(
                            path.edges.back(),false))
                        fillet_terminals.push_back({group_index,
                            path.edges.back().points.back(),sweep->first,
                            sweep->second,*last_section,edge_offset,
                            edge_offset+1,edge_offset+3,false});
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
                        radius_dimension(*first_section, radius_at(0.0),
                            primary_at_displayed_start
                                ? "primary" : "secondary");
                        radius_dimension(*last_section, radius_at(path_length),
                            primary_at_displayed_start
                                ? "secondary" : "primary");
                    } else {
                        radius_dimension(*first_section,
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
    if(fillet_terminals.size()>=2) {
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
            if(node.size()!=2||fillet_terminals[node[0]].group==
                    fillet_terminals[node[1]].group)
                continue;
            auto intersection=edge_preview_fillet_intersection(
                fillet_terminals[node[0]],fillet_terminals[node[1]]);
            if(intersection.empty())continue;
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
        }
        std::vector<zima::kernel::ViewerEdge> retained;
        retained.reserve(result.edges.size()+intersections.size());
        for(std::size_t index=0;index<result.edges.size();++index)
            if(!remove_edge[index])
                retained.push_back(std::move(result.edges[index]));
        retained.insert(retained.end(),
            std::make_move_iterator(intersections.begin()),
            std::make_move_iterator(intersections.end()));
        result.edges=std::move(retained);
    }
    return result;
}

} // namespace zima::app::workspace_detail
