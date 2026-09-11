#include "workspace_internal.hpp"

namespace zima::app::workspace_detail {


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

bool viewer_edges_same_geometry(
    const zima::kernel::ViewerEdge& first,
    const zima::kernel::ViewerEdge& second) {
    if (first.points.empty() || first.points.size() != second.points.size()) {
        return false;
    }
    const auto same_order = [&](bool reverse) {
        for (std::size_t index = 0; index < first.points.size(); ++index) {
            const auto second_index = reverse
                ? second.points.size() - index - 1
                : index;
            if (!edge_preview_same_point(
                    first.points[index], second.points[second_index])) {
                return false;
            }
        }
        return true;
    };
    return same_order(false) || same_order(true);
}

std::size_t restore_surviving_edge_references_after_history_delete(
    zima::document::PartDocument& document,
    const std::string& deleted_owner,
    const zima::kernel::BodyResult& input_before_deleted,
    const std::vector<zima::kernel::BodyResult>& old_boundaries) {
    const auto old_edge = [&](const zima::kernel::EdgeReference& reference)
            -> const zima::kernel::ViewerEdge* {
        for (const auto& boundary : old_boundaries) {
            const auto found = std::ranges::find_if(
                boundary.mesh.edges, [&](const auto& edge) {
                    return edge.reference == reference;
                });
            if (found != boundary.mesh.edges.end()) return &*found;
        }
        return nullptr;
    };
    const auto surviving_reference =
        [&](const zima::kernel::EdgeReference& reference)
            -> std::optional<zima::kernel::EdgeReference> {
        const auto* stale_edge = old_edge(reference);
        if (stale_edge == nullptr) return std::nullopt;
        std::vector<zima::kernel::EdgeReference> matches;
        for (const auto& candidate : input_before_deleted.mesh.edges) {
            if (!candidate.reference.valid() ||
                candidate.reference.owner_id == deleted_owner ||
                !viewer_edges_same_geometry(*stale_edge, candidate)) {
                continue;
            }
            if (std::ranges::find(matches, candidate.reference) == matches.end()) {
                matches.push_back(candidate.reference);
            }
        }
        // Repair only the unambiguous case: the referenced operational edge
        // was already present, geometrically unchanged, before the deleted
        // feature. A truly generated/modified edge remains dependent and the
        // normal calculation rejects its deletion.
        return matches.size() == 1
            ? std::optional<zima::kernel::EdgeReference>{matches.front()}
            : std::nullopt;
    };

    std::size_t restored{};
    for (auto& container : document.history) {
        if (container.feature_kind != zima::document::FeatureKind::Fillet &&
            container.feature_kind != zima::document::FeatureKind::Chamfer) {
            continue;
        }
        for (auto& route : container.edge_treatment.routes) {
            for (auto& reference : route) {
                if (reference.owner_id != deleted_owner) continue;
                if (const auto replacement = surviving_reference(reference)) {
                    reference = *replacement;
                    ++restored;
                }
            }
        }
    }
    return restored;
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
    const double cosine = std::clamp(edge_preview_dot(
        *first_direction, *second_direction), -0.999999, 0.999999);
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

EdgeTreatmentPreviewGeometry edge_treatment_preview_wire(
    const std::vector<std::vector<zima::kernel::ViewerEdge>>& groups,
    const zima::document::EdgeTreatmentParameters& parameters,
    zima::document::FeatureKind kind, const std::string& owner_id) {
    EdgeTreatmentPreviewGeometry result;
    const bool fillet = kind == zima::document::FeatureKind::Fillet;
    if (!std::isfinite(parameters.primary_size) ||
        parameters.primary_size <= 1.0e-9) return result;
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const auto& group = groups[group_index];
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
            for (std::size_t edge_index = 0;
                 edge_index < path.edges.size(); ++edge_index) {
                auto& edge = path.edges[edge_index];
                auto start = edge_treatment_section(edge.points.front(),
                    edge.edge_treatment_side_directions[0].front(),
                    edge.edge_treatment_side_directions[1].front(), parameters,
                    fillet, radius_at(traversed));
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
                        start = edge_treatment_section(edge.points.front(),
                            edge.edge_treatment_side_directions[0].front(),
                            edge.edge_treatment_side_directions[1].front(),
                            parameters, fillet, radius_at(traversed));
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
                    auto section = edge_treatment_section(edge.points[point_index],
                        edge.edge_treatment_side_directions[0][point_index],
                        edge.edge_treatment_side_directions[1][point_index],
                        parameters, fillet, radius_at(traversed));
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
            result.edges.push_back(std::move(first_rail));
            result.edges.push_back(std::move(second_rail));
            result.edges.push_back(std::move(first_section->wire));
            result.edges.push_back(std::move(last_section->wire));
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
                                                   const zima::kernel::Vec3& other,
                                                   double value,
                                                   const char* key) {
                    zima::kernel::ViewerDimension dimension;
                    dimension.value = value;
                    dimension.reference = {owner_id,
                        std::string{"parameter:"} + key, {}};
                    dimension.kind = zima::kernel::ViewerDimensionKind::Linear;
                    dimension.witness_first = path.edges.front().points.front();
                    dimension.witness_second = tangent;
                    const auto shift = edge_preview_difference(
                        other, dimension.witness_first);
                    const auto shift_direction = edge_preview_normalized(shift);
                    const double shift_distance = std::max(0.5, value * 0.35);
                    dimension.line_first = shift_direction
                        ? edge_preview_offset(dimension.witness_first,
                              *shift_direction, shift_distance)
                        : dimension.witness_first;
                    dimension.line_second = shift_direction
                        ? edge_preview_offset(dimension.witness_second,
                              *shift_direction, shift_distance)
                        : dimension.witness_second;
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
                        first_section->second_tangent,
                        parameters.primary_size, "primary");
                    using Mode =
                        zima::document::EdgeTreatmentParameters::ChamferMode;
                    if (parameters.chamfer_mode == Mode::TwoDistances) {
                        linear_dimension(first_section->second_tangent,
                            first_section->first_tangent,
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
    return result;
}

} // namespace zima::app::workspace_detail
