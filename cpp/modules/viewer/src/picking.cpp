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
            if (!edge.infinite) {
                segment_parameter = std::clamp(segment_parameter, 0.0, 1.0);
            }
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
        const bool infinite_sketch_axis =
            axis.reference.semantic_key == "sketch_axis:x" ||
            axis.reference.semantic_key == "sketch_axis:y";
        if (!infinite_sketch_axis) {
            segment_parameter = std::clamp(segment_parameter, 0.0, 1.0);
        }
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
    std::vector<std::size_t> dimension_indices;
    for (std::size_t index = 0; index < mesh.dimensions.size(); ++index) {
        const auto& dimension = mesh.dimensions[index];
        if (dimension.kind == zima::kernel::ViewerDimensionKind::Angular) {
            const Vec3 vertex = dimension.witness_first;
            const Vec3 radial = subtract(dimension.line_first, vertex);
            const double normal_length = length(dimension.plane_normal);
            if (length(radial) > 1.0e-12 && normal_length > 1.0e-12) {
                const Vec3 normal{
                    dimension.plane_normal.x / normal_length,
                    dimension.plane_normal.y / normal_length,
                    dimension.plane_normal.z / normal_length};
                constexpr int samples = 48;
                zima::kernel::ViewerEdge arc;
                arc.reference = dimension.reference;
                for (int sample = 0; sample <= samples; ++sample) {
                    const double angle = dimension.sweep_degrees *
                        3.14159265358979323846 / 180.0 * sample / samples;
                    const auto perpendicular = cross(normal, radial);
                    const double projection = dot(normal, radial);
                    arc.points.push_back({
                        vertex.x + radial.x * std::cos(angle) +
                            perpendicular.x * std::sin(angle) +
                            normal.x * projection * (1.0 - std::cos(angle)),
                        vertex.y + radial.y * std::cos(angle) +
                            perpendicular.y * std::sin(angle) +
                            normal.y * projection * (1.0 - std::cos(angle)),
                        vertex.z + radial.z * std::cos(angle) +
                            perpendicular.z * std::sin(angle) +
                            normal.z * projection * (1.0 - std::cos(angle))});
                }
                lines.edges.push_back(std::move(arc));
                dimension_indices.push_back(index);
                continue;
            }
        }
        lines.edges.push_back({
            {dimension.line_first, dimension.line_second}, dimension.reference});
        dimension_indices.push_back(index);
    }
    std::vector<DimensionPickCandidate> result;
    for (const auto& candidate : ordered_edge_candidates(
            lines, ray_origin, ray_direction, world_tolerance)) {
        if (candidate.edge >= dimension_indices.size()) continue;
        const auto dimension_index = dimension_indices[candidate.edge];
        if (std::none_of(result.begin(), result.end(), [&](const auto& existing) {
                return existing.dimension == dimension_index;
            })) {
            result.push_back(
                {dimension_index, candidate.distance, candidate.reference});
        }
    }
    return result;
}

std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    zima::kernel::ViewerMesh references;
    references.vertices = mesh.original_references.vertices;
    references.triangles = mesh.original_references.triangles;
    references.triangle_references = mesh.original_references.triangle_references;
    references.edges = mesh.original_references.edges;
    references.points = mesh.original_references.points;
    references.axes = mesh.original_references.axes;
    return ordered_viewer_candidates(
        mesh, references, ray_origin, ray_direction, world_tolerance);
}

std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::ViewerMesh& references,
    const Vec3& ray_origin,
    const Vec3& ray_direction,
    double world_tolerance) {
    std::vector<ViewerCandidate> result;
    const auto persisted_face_hits = ordered_ray_candidates(
        references, ray_origin, ray_direction);
    const auto append_geometry = [&](const zima::kernel::ViewerMesh& source,
                                     CandidateGeometry geometry) {
        const auto faces = ordered_ray_candidates(source, ray_origin, ray_direction);
        for (const auto& face : faces) {
            // A Plane's filled interior quad (built-in Origin XY/YZ/XZ, or a
            // user construction Plane's entity quad) is display/hit-test
            // geometry for its rectangular border only; the interior itself
            // must not be selectable -- see the matching border-edge
            // Face/Container generation below, which is the sole way a Plane
            // is now offered/selected.
            if (face.reference.semantic_key == "plane" ||
                face.reference.semantic_key.starts_with("origin:plane:")) continue;
            const bool origin_reference =
                face.reference.semantic_key.starts_with("origin:");
            const bool persisted_occurrence = geometry == CandidateGeometry::Display &&
                std::any_of(persisted_face_hits.begin(), persisted_face_hits.end(),
                    [&](const auto& persisted) {
                        return persisted.reference.instance_path ==
                            face.reference.instance_path;
                    });
            if (!origin_reference && !face.reference.instance_path.empty() &&
                !persisted_occurrence &&
                std::none_of(result.begin(), result.end(), [&](const ViewerCandidate& item) {
                    return item.kind == CandidateKind::Occurrence &&
                        item.instance_path == face.reference.instance_path;
                })) {
                result.push_back({CandidateKind::Occurrence, face.distance, face.triangle,
                                  {}, {}, face.reference.instance_path, geometry});
            }
            if (!persisted_occurrence) {
                result.push_back({CandidateKind::Face, face.distance, face.triangle,
                                  face.reference.owner_id, face.reference.semantic_key,
                                  face.reference.instance_path, geometry});
            }
            const bool persisted_container = persisted_occurrence;
            constexpr std::string_view entity_suffix{":entity"};
            const bool datum_entity = face.reference.owner_id.ends_with(entity_suffix) &&
                face.reference.semantic_key == "plane";
            const std::string container_owner = datum_entity
                ? face.reference.owner_id.substr(0,
                    face.reference.owner_id.size() - entity_suffix.size())
                : face.reference.owner_id;
            if (!origin_reference && !persisted_container &&
                std::none_of(result.begin(), result.end(), [&](const ViewerCandidate& item) {
                    return item.kind == CandidateKind::Container &&
                        item.owner_id == container_owner &&
                        item.instance_path == face.reference.instance_path;
                })) {
                result.push_back({CandidateKind::Container, face.distance, face.triangle,
                                  container_owner, datum_entity ? "plane" : "",
                                  face.reference.instance_path, geometry});
            }
        }
        for (const auto& edge : ordered_edge_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            // Plane borders in the display packet are unscaled model-space
            // helpers. Their one visible/pickable screen-constant copy lives
            // in the rebuilt persisted-reference packet.
            if (geometry == CandidateGeometry::Display &&
                edge.edge < source.edges.size() && source.edges[edge.edge].overlay &&
                (edge.reference.semantic_key == "border" ||
                 edge.reference.semantic_key.starts_with("origin:plane:"))) {
                continue;
            }
            // A Plane is selected through its rectangular border only: the
            // 4 border segments (built-in Origin "origin:plane:<key>", or a
            // user construction Plane's "border") resolve to the same
            // Face/Container candidate pair the filled interior used to
            // produce, so every existing Plane-picking command (which
            // matches on CandidateKind::Face) and the ordinary whole-object
            // Container selection keep working -- only the pickable region
            // moved from the interior quad to its outline.
            if (edge.reference.semantic_key == "border" ||
                edge.reference.semantic_key.starts_with("origin:plane:")) {
                constexpr std::string_view entity_suffix{":entity"};
                const bool datum_entity =
                    edge.reference.owner_id.ends_with(entity_suffix) &&
                    edge.reference.semantic_key == "border";
                result.push_back({CandidateKind::Plane, edge.distance, edge.edge,
                                  edge.reference.owner_id,
                                  datum_entity ? "plane" : edge.reference.semantic_key,
                                  edge.reference.instance_path, geometry});
                const std::string container_owner = datum_entity
                    ? edge.reference.owner_id.substr(0,
                        edge.reference.owner_id.size() - entity_suffix.size())
                    : edge.reference.owner_id;
                if (std::none_of(result.begin(), result.end(),
                        [&](const ViewerCandidate& item) {
                            return item.kind == CandidateKind::Container &&
                                item.owner_id == container_owner &&
                                item.instance_path == edge.reference.instance_path;
                        })) {
                    result.push_back({CandidateKind::Container, edge.distance, edge.edge,
                                      container_owner, datum_entity ? "plane" : "",
                                      edge.reference.instance_path, geometry});
                }
                continue;
            }
            const auto kind = edge.reference.semantic_key.starts_with("trim_piece:")
                ? CandidateKind::SketchTrimPiece
                : edge.reference.semantic_key.starts_with("external_edge:")
                  || edge.reference.semantic_key.starts_with("external_axis:")
                  || edge.reference.semantic_key.starts_with("external_face:")
                ? CandidateKind::SketchExternalReference
                : edge.reference.semantic_key.starts_with("text:")
                ? CandidateKind::SketchText
                : edge.reference.semantic_key.starts_with("segment:")
                ? CandidateKind::SketchSegment
                : edge.reference.semantic_key.starts_with("circle:") ||
                  edge.reference.semantic_key.starts_with("arc:") ||
                  edge.reference.semantic_key.starts_with("corner_radius:") ||
                  edge.reference.semantic_key.starts_with("ellipse:") ||
                  edge.reference.semantic_key.starts_with("elliptical_arc:") ||
                  edge.reference.semantic_key.starts_with("bspline:")
                    ? CandidateKind::SketchCurve : CandidateKind::Edge;
            if ((kind != CandidateKind::SketchText &&
                 kind != CandidateKind::SketchExternalReference) ||
                std::none_of(result.begin(), result.end(), [&](const auto& candidate) {
                    return candidate.kind == kind &&
                        candidate.owner_id == edge.reference.owner_id &&
                        candidate.semantic_key == edge.reference.semantic_key &&
                        candidate.geometry == geometry;
                })) {
                result.push_back({kind, edge.distance, edge.edge,
                                  edge.reference.owner_id, edge.reference.semantic_key,
                                  edge.reference.instance_path, geometry});
            }
            const bool native_sketch_geometry =
                kind == CandidateKind::SketchSegment ||
                kind == CandidateKind::SketchCurve ||
                kind == CandidateKind::SketchText;
            if (native_sketch_geometry &&
                std::none_of(result.begin(), result.end(),
                    [&](const ViewerCandidate& candidate) {
                        return candidate.kind == CandidateKind::Container &&
                            candidate.semantic_key == "sketch" &&
                            candidate.owner_id == edge.reference.owner_id &&
                            candidate.instance_path ==
                                edge.reference.instance_path &&
                            candidate.geometry == geometry;
                    })) {
                // Outside Sketcher the same hit is offered as the complete
                // Sketch leaf container. Active Sketcher contracts filter
                // this candidate out and continue to consume the individual
                // persisted geometry candidate above.
                result.push_back({CandidateKind::Container, edge.distance,
                    edge.edge, edge.reference.owner_id, "sketch",
                    edge.reference.instance_path, geometry});
            }
        }
        for (const auto& vertex : ordered_vertex_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            const auto kind = vertex.reference.semantic_key.starts_with(
                    "external_point:") ||
                    vertex.reference.semantic_key.starts_with("sketch_midpoint:") ||
                    vertex.reference.semantic_key.starts_with("sketch_intersection:") ||
                    vertex.reference.semantic_key.starts_with("sketch_curve_keypoint:")
                ? CandidateKind::SketchExternalReference
                : (vertex.reference.semantic_key.starts_with("point:") ||
                   vertex.reference.semantic_key.starts_with(
                       "corner_radius_handle:"))
                    ? CandidateKind::SketchPoint : CandidateKind::Vertex;
            result.push_back({kind, vertex.distance, vertex.point,
                              vertex.reference.owner_id, vertex.reference.semantic_key,
                              vertex.reference.instance_path, geometry});
            if (vertex.reference.semantic_key ==
                "container:origin-marker") {
                // Clicking the characteristic origin dot confirms the same
                // solid container as clicking its wire.  Point-taking
                // commands can still consume the preceding exact Vertex
                // candidate from this one common ordered list.
                result.push_back({CandidateKind::Container, vertex.distance,
                    vertex.point, vertex.reference.owner_id, "solid",
                    vertex.reference.instance_path, geometry});
            }
            // A construction Point is deliberately both a history container
            // and a persisted point reference. Ordinary selection offers the
            // container; point-taking commands filter the same ordered list
            // to the Vertex candidate. This avoids a synthetic "point in a
            // point" UI object while preserving the real Origin child ID.
            constexpr std::string_view origin_suffix{":origin"};
            if (vertex.reference.semantic_key == "point" &&
                vertex.reference.owner_id.ends_with(origin_suffix)) {
                result.push_back({CandidateKind::Container, vertex.distance,
                    vertex.point,
                    vertex.reference.owner_id.substr(0,
                        vertex.reference.owner_id.size() - origin_suffix.size()),
                    "point", vertex.reference.instance_path, geometry});
            }
        }
        for (const auto& axis : ordered_axis_candidates(
                source, ray_origin, ray_direction, world_tolerance)) {
            const auto kind = axis.reference.semantic_key.starts_with("sketch_axis:")
                ? CandidateKind::SketchAxis : CandidateKind::Axis;
            result.push_back({kind, axis.distance, axis.axis,
                              axis.reference.owner_id, axis.reference.semantic_key,
                              axis.reference.instance_path, geometry});
            constexpr std::string_view entity_suffix{":entity"};
            if (axis.reference.semantic_key == "axis" &&
                axis.reference.owner_id.ends_with(entity_suffix)) {
                result.push_back({CandidateKind::Container, axis.distance, axis.axis,
                    axis.reference.owner_id.substr(0,
                        axis.reference.owner_id.size() - entity_suffix.size()),
                    "axis", axis.reference.instance_path, geometry});
            }
        }
    };
    append_geometry(mesh, CandidateGeometry::Display);
    append_geometry(references, CandidateGeometry::OriginalReference);
    for (const auto& dimension : ordered_dimension_candidates(
            mesh, ray_origin, ray_direction, world_tolerance)) {
        result.push_back({CandidateKind::Dimension, dimension.distance,
                          dimension.dimension, dimension.reference.owner_id,
                          dimension.reference.semantic_key,
                          dimension.reference.instance_path});
    }
    const double ray_length_squared = dot(ray_direction, ray_direction);
    if (ray_length_squared > 1.0e-18) {
        for (std::size_t index = 0; index < mesh.constraint_markers.size(); ++index) {
            const auto& marker = mesh.constraint_markers[index];
            if (!marker.reference.valid()) continue;
            const double ray_parameter = dot(
                subtract(marker.position, ray_origin), ray_direction) /
                ray_length_squared;
            if (ray_parameter < 0.0) continue;
            const auto ray_point = add_scaled(
                ray_origin, ray_direction, ray_parameter);
            if (length(subtract(marker.position, ray_point)) <= world_tolerance) {
                result.push_back({CandidateKind::SketchConstraint, ray_parameter,
                    index, marker.reference.owner_id,
                    marker.reference.semantic_key,
                    marker.reference.instance_path});
            }
        }
    }
    const auto priority = [](const ViewerCandidate& candidate) {
        if (candidate.kind == CandidateKind::SketchExternalReference &&
            (candidate.semantic_key.starts_with("external_point:") ||
             candidate.semantic_key.starts_with("sketch_midpoint:") ||
             candidate.semantic_key.starts_with("sketch_intersection:") ||
             candidate.semantic_key.starts_with("sketch_curve_keypoint:"))) {
            return 0;
        }
        switch (candidate.kind) {
        case CandidateKind::SketchConstraint: return 0;
        case CandidateKind::Dimension: return 0;
        case CandidateKind::SketchTrimPiece: return 0;
        case CandidateKind::SketchExternalReference: return 1;
        case CandidateKind::SketchText: return 2;
        case CandidateKind::SketchCurve: return 2;
        // A real editable Sketch point owns an ordinary LMB gesture wherever
        // its marker overlaps annotations or reference geometry. Dimensions,
        // constraints and axes remain in this same ordered list for RMB
        // cycling, but may not prevent direct point dragging.
        case CandidateKind::SketchPoint: return -1;
        case CandidateKind::Vertex: return 0;
        case CandidateKind::Axis: return 1;
        case CandidateKind::SketchAxis: return 1;
        case CandidateKind::SketchSegment: return 2;
        case CandidateKind::Edge: return 2;
        case CandidateKind::Plane: return 3;
        case CandidateKind::Face: return 3;
        case CandidateKind::Container: return 4;
        case CandidateKind::Occurrence: return 5;
        }
        return 5;
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
        const auto point_container = [](const ViewerCandidate& candidate) {
            return candidate.kind == CandidateKind::Container &&
                candidate.semantic_key == "point";
        };
        if (point_container(left) != point_container(right)) {
            return point_container(left);
        }
        if (priority(left) != priority(right)) {
            return priority(left) < priority(right);
        }
        if (std::abs(left.distance - right.distance) > 1.0e-9) {
            return left.distance < right.distance;
        }
        return false;
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

std::vector<ViewerCandidate> filter_candidates(
    const std::vector<ViewerCandidate>& candidates,
    const std::vector<CandidateKind>& allowed_kinds,
    const std::function<bool(const ViewerCandidate&)>& candidate_filter) {
    auto result = filter_candidates(candidates, allowed_kinds);
    if (candidate_filter) {
        std::erase_if(result, [&](const auto& candidate) {
            return !candidate_filter(candidate);
        });
    }
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
    if (auto display = find_in(
            mesh.triangle_references, CandidateGeometry::Display)) return display;
    const auto find_point = [&](const auto& points, CandidateGeometry geometry)
        -> std::optional<ViewerCandidate> {
        constexpr std::string_view origin_suffix{":origin"};
        const std::string point_origin_id = owner_id + std::string(origin_suffix);
        const auto point = std::find_if(points.begin(), points.end(),
            [&](const zima::kernel::ViewerPoint& value) {
                return value.reference.semantic_key == "point" &&
                    value.reference.owner_id == point_origin_id &&
                    value.reference.instance_path == instance_path;
            });
        if (point == points.end()) return std::nullopt;
        return ViewerCandidate{CandidateKind::Container, 0.0,
            static_cast<std::size_t>(std::distance(points.begin(), point)),
            owner_id, "point", point->reference.instance_path, geometry};
    };
    if (auto original_point = find_point(
            mesh.original_references.points,
            CandidateGeometry::OriginalReference)) return original_point;
    if (auto display_point = find_point(
            mesh.points, CandidateGeometry::Display)) return display_point;
    const std::string entity_id = owner_id + ":entity";
    const auto find_axis = [&](const auto& axes, CandidateGeometry geometry)
        -> std::optional<ViewerCandidate> {
        const auto axis = std::find_if(axes.begin(), axes.end(), [&](const auto& value) {
            return value.reference.owner_id == entity_id &&
                value.reference.semantic_key == "axis" &&
                value.reference.instance_path == instance_path;
        });
        if (axis == axes.end()) return std::nullopt;
        return ViewerCandidate{CandidateKind::Container, 0.0,
            static_cast<std::size_t>(std::distance(axes.begin(), axis)),
            owner_id, "axis", instance_path, geometry};
    };
    if (auto original_axis = find_axis(mesh.original_references.axes,
            CandidateGeometry::OriginalReference)) return original_axis;
    if (auto display_axis = find_axis(mesh.axes,
            CandidateGeometry::Display)) return display_axis;
    const auto find_plane = [&](const auto& references, CandidateGeometry geometry)
        -> std::optional<ViewerCandidate> {
        const auto plane = std::find_if(references.begin(), references.end(),
            [&](const auto& value) {
                return value.owner_id == entity_id &&
                    value.semantic_key == "plane" &&
                    value.instance_path == instance_path;
            });
        if (plane == references.end()) return std::nullopt;
        return ViewerCandidate{CandidateKind::Container, 0.0,
            static_cast<std::size_t>(std::distance(references.begin(), plane)),
            owner_id, "plane", instance_path, geometry};
    };
    if (auto original_plane = find_plane(
            mesh.original_references.triangle_references,
            CandidateGeometry::OriginalReference)) return original_plane;
    if (auto display_plane = find_plane(
            mesh.triangle_references, CandidateGeometry::Display)) {
        return display_plane;
    }
    const auto sketch_edge = std::find_if(mesh.edges.begin(), mesh.edges.end(),
        [&](const zima::kernel::ViewerEdge& edge) {
            const auto& key = edge.reference.semantic_key;
            return edge.reference.owner_id == owner_id &&
                edge.reference.instance_path == instance_path &&
                (key.starts_with("segment:") || key.starts_with("circle:") ||
                 key.starts_with("arc:") || key.starts_with("ellipse:") ||
                 key.starts_with("corner_radius:") ||
                 key.starts_with("elliptical_arc:") ||
                 key.starts_with("bspline:") || key.starts_with("text:"));
        });
    if (sketch_edge == mesh.edges.end()) return std::nullopt;
    return ViewerCandidate{CandidateKind::Container, 0.0,
        static_cast<std::size_t>(std::distance(mesh.edges.begin(), sketch_edge)),
        owner_id, "sketch", instance_path, CandidateGeometry::Display};
}

}  // namespace zima::viewer
