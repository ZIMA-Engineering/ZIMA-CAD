#include <zima/viewer/picking.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>

namespace zima::viewer {
namespace {

using Vec3 = zima::kernel::Vec3;

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 add_scaled(const Vec3& origin, const Vec3& direction, double scale) {
    return {origin.x + direction.x * scale, origin.y + direction.y * scale,
            origin.z + direction.z * scale};
}

Vec3 cross(const Vec3& left, const Vec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

double dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length(const Vec3& value) {
    return std::sqrt(dot(value, value));
}

}  // namespace

std::vector<PickCandidate> ordered_ray_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction) {
    constexpr double epsilon = 1.0e-9;
    std::vector<PickCandidate> candidates;
    for (std::size_t triangle = 0; triangle * 3 + 2 < mesh.triangles.size(); ++triangle) {
        if (triangle >= mesh.triangle_references.size() ||
            !mesh.triangle_references[triangle].valid()) continue;
        const auto first = mesh.triangles[triangle * 3];
        const auto second = mesh.triangles[triangle * 3 + 1];
        const auto third = mesh.triangles[triangle * 3 + 2];
        if (first >= mesh.vertices.size() || second >= mesh.vertices.size() ||
            third >= mesh.vertices.size()) continue;
        const Vec3& a = mesh.vertices[first];
        const Vec3 edge_one = subtract(mesh.vertices[second], a);
        const Vec3 edge_two = subtract(mesh.vertices[third], a);
        const Vec3 h = cross(ray_direction, edge_two);
        const double determinant = dot(edge_one, h);
        if (std::abs(determinant) < epsilon) continue;
        const double inverse = 1.0 / determinant;
        const Vec3 s = subtract(ray_origin, a);
        const double u = inverse * dot(s, h);
        if (u < 0.0 || u > 1.0) continue;
        const Vec3 q = cross(s, edge_one);
        const double v = inverse * dot(ray_direction, q);
        if (v < 0.0 || u + v > 1.0) continue;
        const double distance = inverse * dot(edge_two, q);
        if (distance > epsilon) {
            candidates.push_back(
                {triangle, distance, mesh.triangle_references[triangle]});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const PickCandidate& left, const PickCandidate& right) {
            return left.distance < right.distance;
        });
    std::vector<PickCandidate> unique_faces;
    unique_faces.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (std::none_of(unique_faces.begin(), unique_faces.end(),
                [&](const PickCandidate& existing) {
                    return existing.reference == candidate.reference;
                })) {
            unique_faces.push_back(candidate);
        }
    }
    return unique_faces;
}

std::size_t next_candidate_index(
    std::size_t current, std::size_t candidate_count) {
    return candidate_count == 0 ? 0 : (current + 1) % candidate_count;
}

std::vector<EdgePickCandidate> ordered_edge_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<EdgePickCandidate> candidates;
    const double ray_length_squared = dot(ray_direction, ray_direction);
    if (ray_length_squared <= 1.0e-18 || world_tolerance < 0.0) return candidates;
    for (std::size_t edge_index = 0; edge_index < mesh.edges.size(); ++edge_index) {
        const auto& edge = mesh.edges[edge_index];
        if (!edge.reference.valid() || edge.points.size() < 2) continue;
        double nearest_ray_distance = std::numeric_limits<double>::max();
        bool hit = false;
        for (std::size_t segment = 1; segment < edge.points.size(); ++segment) {
            const Vec3& p0 = edge.points[segment - 1];
            const Vec3 segment_direction = subtract(edge.points[segment], p0);
            const double segment_length_squared = dot(segment_direction, segment_direction);
            if (segment_length_squared <= 1.0e-18) continue;
            const Vec3 origin_delta = subtract(p0, ray_origin);
            const double b = dot(segment_direction, ray_direction);
            const double d = dot(segment_direction, origin_delta);
            const double e = dot(ray_direction, origin_delta);
            const double denominator =
                segment_length_squared * ray_length_squared - b * b;
            double segment_parameter = denominator > 1.0e-18
                ? (b * e - ray_length_squared * d) / denominator : 0.0;
            segment_parameter = std::clamp(segment_parameter, 0.0, 1.0);
            const Vec3 segment_point = add_scaled(p0, segment_direction, segment_parameter);
            double ray_parameter = dot(subtract(segment_point, ray_origin), ray_direction) /
                ray_length_squared;
            ray_parameter = std::max(ray_parameter, 0.0);
            const Vec3 ray_point = add_scaled(ray_origin, ray_direction, ray_parameter);
            if (length(subtract(segment_point, ray_point)) <= world_tolerance) {
                hit = true;
                nearest_ray_distance = std::min(nearest_ray_distance, ray_parameter);
            }
        }
        if (hit) candidates.push_back({edge_index, nearest_ray_distance, edge.reference});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) { return left.distance < right.distance; });
    return candidates;
}

std::vector<VertexPickCandidate> ordered_vertex_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<VertexPickCandidate> candidates;
    const double ray_length_squared = dot(ray_direction, ray_direction);
    if (ray_length_squared <= 1.0e-18 || world_tolerance < 0.0) return candidates;
    for (std::size_t index = 0; index < mesh.points.size(); ++index) {
        const auto& point = mesh.points[index];
        if (!point.reference.valid()) continue;
        const double ray_parameter =
            dot(subtract(point.position, ray_origin), ray_direction) / ray_length_squared;
        if (ray_parameter < 0.0) continue;
        const Vec3 ray_point = add_scaled(ray_origin, ray_direction, ray_parameter);
        if (length(subtract(point.position, ray_point)) <= world_tolerance) {
            candidates.push_back({index, ray_parameter, point.reference});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) { return left.distance < right.distance; });
    return candidates;
}

std::vector<AxisPickCandidate> ordered_axis_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<AxisPickCandidate> candidates;
    const double ray_length_squared = dot(ray_direction, ray_direction);
    if (ray_length_squared <= 1.0e-18 || world_tolerance < 0.0) return candidates;
    for (std::size_t index = 0; index < mesh.axes.size(); ++index) {
        const auto& axis = mesh.axes[index];
        if (!axis.reference.valid() || axis.display_length <= 0.0) continue;
        const Vec3 segment_direction{
            axis.direction.x * axis.display_length,
            axis.direction.y * axis.display_length,
            axis.direction.z * axis.display_length};
        const Vec3 start{
            axis.point.x - segment_direction.x * 0.5,
            axis.point.y - segment_direction.y * 0.5,
            axis.point.z - segment_direction.z * 0.5};
        const double segment_length_squared = dot(segment_direction, segment_direction);
        const Vec3 origin_delta = subtract(start, ray_origin);
        const double b = dot(segment_direction, ray_direction);
        const double d = dot(segment_direction, origin_delta);
        const double e = dot(ray_direction, origin_delta);
        const double denominator =
            segment_length_squared * ray_length_squared - b * b;
        double segment_parameter = denominator > 1.0e-18
            ? (b * e - ray_length_squared * d) / denominator : 0.5;
        segment_parameter = std::clamp(segment_parameter, 0.0, 1.0);
        const Vec3 segment_point = add_scaled(start, segment_direction, segment_parameter);
        double ray_parameter = dot(subtract(segment_point, ray_origin), ray_direction) /
            ray_length_squared;
        ray_parameter = std::max(ray_parameter, 0.0);
        const Vec3 ray_point = add_scaled(ray_origin, ray_direction, ray_parameter);
        if (length(subtract(segment_point, ray_point)) <= world_tolerance) {
            candidates.push_back({index, ray_parameter, axis.reference});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) { return left.distance < right.distance; });
    return candidates;
}

std::vector<DimensionPickCandidate> ordered_dimension_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    zima::kernel::ViewerMesh lines;
    for (const auto& dimension : mesh.dimensions) {
        lines.edges.push_back({
            {dimension.line_first, dimension.line_second}, dimension.reference});
    }
    std::vector<DimensionPickCandidate> result;
    for (const auto& candidate : ordered_edge_candidates(
            lines, ray_origin, ray_direction, world_tolerance)) {
        result.push_back({candidate.edge, candidate.distance, candidate.reference});
    }
    return result;
}

std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<ViewerCandidate> result;
    const auto append_geometry = [&](const zima::kernel::ViewerMesh& source,
                                     CandidateGeometry geometry) {
        const auto faces = ordered_ray_candidates(source, ray_origin, ray_direction);
        for (const auto& face : faces) {
            if (!face.reference.instance_path.empty() &&
                std::none_of(result.begin(), result.end(), [&](const ViewerCandidate& item) {
                    return item.kind == CandidateKind::Occurrence &&
                        item.instance_path == face.reference.instance_path;
                })) {
                result.push_back({CandidateKind::Occurrence, face.distance, face.triangle,
                                  {}, {}, face.reference.instance_path, geometry});
            }
            result.push_back({CandidateKind::Face, face.distance, face.triangle,
                              face.reference.owner_id, face.reference.semantic_key,
                              face.reference.instance_path, geometry});
            if (std::none_of(result.begin(), result.end(), [&](const ViewerCandidate& item) {
                    return item.kind == CandidateKind::Container &&
                        item.owner_id == face.reference.owner_id &&
                        item.instance_path == face.reference.instance_path;
                })) {
                result.push_back({CandidateKind::Container, face.distance, face.triangle,
                                  face.reference.owner_id, {},
                                  face.reference.instance_path, geometry});
            }
        }
        for (const auto& edge : ordered_edge_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            const auto kind = edge.reference.semantic_key.starts_with("segment:")
                ? CandidateKind::SketchSegment
                : edge.reference.semantic_key.starts_with("circle:") ||
                  edge.reference.semantic_key.starts_with("arc:") ||
                  edge.reference.semantic_key.starts_with("ellipse:") ||
                  edge.reference.semantic_key.starts_with("bspline:")
                    ? CandidateKind::SketchCurve : CandidateKind::Edge;
            result.push_back({kind, edge.distance, edge.edge,
                              edge.reference.owner_id, edge.reference.semantic_key,
                              edge.reference.instance_path, geometry});
        }
        for (const auto& vertex : ordered_vertex_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            const auto kind = vertex.reference.semantic_key.starts_with("point:")
                ? CandidateKind::SketchPoint : CandidateKind::Vertex;
            result.push_back({kind, vertex.distance, vertex.point,
                              vertex.reference.owner_id, vertex.reference.semantic_key,
                              vertex.reference.instance_path, geometry});
        }
        for (const auto& axis : ordered_axis_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            result.push_back({CandidateKind::Axis, axis.distance, axis.axis,
                              axis.reference.owner_id, axis.reference.semantic_key,
                              axis.reference.instance_path, geometry});
        }
    };
    append_geometry(mesh, CandidateGeometry::Display);
    zima::kernel::ViewerMesh references;
    references.vertices = mesh.original_references.vertices;
    references.triangles = mesh.original_references.triangles;
    references.triangle_references = mesh.original_references.triangle_references;
    references.edges = mesh.original_references.edges;
    references.points = mesh.original_references.points;
    references.axes = mesh.original_references.axes;
    append_geometry(references, CandidateGeometry::OriginalReference);
    for (const auto& dimension : ordered_dimension_candidates(
            mesh, ray_origin, ray_direction, world_tolerance)) {
        result.push_back({CandidateKind::Dimension, dimension.distance,
                          dimension.dimension, dimension.reference.owner_id,
                          dimension.reference.semantic_key,
                          dimension.reference.instance_path});
    }
    const auto priority = [](CandidateKind kind) {
        switch (kind) {
        case CandidateKind::Dimension: return 0;
        case CandidateKind::SketchCurve: return 2;
        case CandidateKind::SketchPoint: return 0;
        case CandidateKind::Vertex: return 0;
        case CandidateKind::Axis: return 1;
        case CandidateKind::SketchSegment: return 2;
        case CandidateKind::Edge: return 2;
        case CandidateKind::Face: return 3;
        case CandidateKind::Container: return 4;
        case CandidateKind::Occurrence: return 5;
        }
        return 5;
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
        if (std::abs(left.distance - right.distance) > 1.0e-9) {
            return left.distance < right.distance;
        }
        return priority(left.kind) < priority(right.kind);
    });
    return result;
}

std::optional<ViewerCandidate> occurrence_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& instance_path) {
    if (instance_path.empty()) return std::nullopt;
    const auto find_in = [&](const auto& references, CandidateGeometry geometry)
        -> std::optional<ViewerCandidate> {
        const auto triangle = std::find_if(
        references.begin(), references.end(),
        [&](const zima::kernel::FaceReference& reference) {
            return reference.valid() && reference.instance_path == instance_path;
        });
        if (triangle == references.end()) return std::nullopt;
        return ViewerCandidate{
            CandidateKind::Occurrence, 0.0,
            static_cast<std::size_t>(std::distance(references.begin(), triangle)),
            {}, {}, instance_path, geometry};
    };
    if (auto original = find_in(
            mesh.original_references.triangle_references,
            CandidateGeometry::OriginalReference)) return original;
    return find_in(mesh.triangle_references, CandidateGeometry::Display);
}

std::vector<ViewerCandidate> filter_candidates(
    const std::vector<ViewerCandidate>& candidates,
    const std::vector<CandidateKind>& allowed_kinds) {
    std::vector<ViewerCandidate> result;
    std::copy_if(candidates.begin(), candidates.end(), std::back_inserter(result),
        [&](const ViewerCandidate& candidate) {
            return std::find(allowed_kinds.begin(), allowed_kinds.end(), candidate.kind)
                != allowed_kinds.end();
        });
    return result;
}

std::optional<ViewerCandidate> container_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& owner_id,
    const std::string& instance_path) {
    if (owner_id.empty()) return std::nullopt;
    const auto find_in = [&](const auto& references, CandidateGeometry geometry)
        -> std::optional<ViewerCandidate> {
        const auto triangle = std::find_if(
        references.begin(), references.end(),
        [&](const zima::kernel::FaceReference& reference) {
            return reference.valid() && reference.owner_id == owner_id &&
                reference.instance_path == instance_path;
        });
        if (triangle == references.end()) return std::nullopt;
        return ViewerCandidate{
            CandidateKind::Container, 0.0,
            static_cast<std::size_t>(std::distance(references.begin(), triangle)),
            owner_id, {}, triangle->instance_path, geometry,
        };
    };
    if (auto original = find_in(
            mesh.original_references.triangle_references,
            CandidateGeometry::OriginalReference)) return original;
    return find_in(mesh.triangle_references, CandidateGeometry::Display);
}

}  // namespace zima::viewer
