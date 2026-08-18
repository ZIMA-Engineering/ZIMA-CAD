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

std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<ViewerCandidate> result;
    const auto faces = ordered_ray_candidates(mesh, ray_origin, ray_direction);
    for (const auto& face : faces) {
        result.push_back({CandidateKind::Face, face.distance, face.triangle,
                          face.reference.owner_id, face.reference.semantic_key});
        if (std::none_of(result.begin(), result.end(), [&](const ViewerCandidate& item) {
                return item.kind == CandidateKind::Container &&
                    item.owner_id == face.reference.owner_id;
            })) {
            result.push_back({CandidateKind::Container, face.distance, face.triangle,
                              face.reference.owner_id, {}});
        }
    }
    for (const auto& edge : ordered_edge_candidates(
            mesh, ray_origin, ray_direction, world_tolerance)) {
        result.push_back({CandidateKind::Edge, edge.distance, edge.edge,
                          edge.reference.owner_id, edge.reference.semantic_key});
    }
    for (const auto& vertex : ordered_vertex_candidates(
            mesh, ray_origin, ray_direction, world_tolerance)) {
        result.push_back({CandidateKind::Vertex, vertex.distance, vertex.point,
                          vertex.reference.owner_id, vertex.reference.semantic_key});
    }
    const auto priority = [](CandidateKind kind) {
        switch (kind) {
        case CandidateKind::Vertex: return 0;
        case CandidateKind::Edge: return 1;
        case CandidateKind::Face: return 2;
        case CandidateKind::Container: return 3;
        }
        return 4;
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
        if (std::abs(left.distance - right.distance) > 1.0e-9) {
            return left.distance < right.distance;
        }
        return priority(left.kind) < priority(right.kind);
    });
    return result;
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
    const zima::kernel::ViewerMesh& mesh, const std::string& owner_id) {
    if (owner_id.empty()) return std::nullopt;
    const auto triangle = std::find_if(
        mesh.triangle_references.begin(), mesh.triangle_references.end(),
        [&](const zima::kernel::FaceReference& reference) {
            return reference.valid() && reference.owner_id == owner_id;
        });
    if (triangle == mesh.triangle_references.end()) return std::nullopt;
    return ViewerCandidate{
        CandidateKind::Container,
        0.0,
        static_cast<std::size_t>(std::distance(
            mesh.triangle_references.begin(), triangle)),
        owner_id,
        {},
    };
}

}  // namespace zima::viewer
