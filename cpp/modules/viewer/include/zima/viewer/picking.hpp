#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace zima::viewer {

struct PickCandidate {
    std::size_t triangle{};
    double distance{};
    zima::kernel::FaceReference reference;

    bool operator==(const PickCandidate&) const = default;
};

struct EdgePickCandidate {
    std::size_t edge{};
    double distance{};
    zima::kernel::EdgeReference reference;
};

struct VertexPickCandidate {
    std::size_t point{};
    double distance{};
    zima::kernel::VertexReference reference;
};

struct AxisPickCandidate {
    std::size_t axis{};
    double distance{};
    zima::kernel::AxisReference reference;
};
struct DimensionPickCandidate {
    std::size_t dimension{};
    double distance{};
    zima::kernel::EdgeReference reference;
};

enum class CandidateKind {
    Occurrence, Container, Plane, Face, Edge, Vertex, Axis, SketchAxis, SketchSegment,
    SketchPoint, Dimension, SketchConstraint, SketchCurve, SketchText, SketchExternalReference,
    SketchTrimPiece
};

enum class CandidateGeometry { Display, OriginalReference };

struct ViewerCandidate {
    CandidateKind kind{CandidateKind::Container};
    double distance{};
    std::size_t geometry_index{};
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    CandidateGeometry geometry{CandidateGeometry::Display};

    bool operator==(const ViewerCandidate&) const = default;
};

// Stable identity for one topological edge, mirroring Python's
// (owner_id, edge_index) TopologyKey used throughout zima_cad/viewer.py for
// hover/selection/reference-highlight bookkeeping. C++ edges do not carry a
// numeric index, so owner_id + semantic_key + instance_path (already the
// full persisted reference identity) plays the same role.
struct EdgeKey {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;

    bool operator==(const EdgeKey&) const = default;
    bool operator<(const EdgeKey& other) const {
        if (owner_id != other.owner_id) return owner_id < other.owner_id;
        if (semantic_key != other.semantic_key)
            return semantic_key < other.semantic_key;
        return instance_path < other.instance_path;
    }
};

[[nodiscard]] inline EdgeKey edge_key(const zima::kernel::EdgeReference& reference) {
    return EdgeKey{reference.owner_id, reference.semantic_key, reference.instance_path};
}

// True when the current hover/confirmation candidate should recolour this
// already-persisted display wire.  A Tree-only empty occurrence path denotes
// the whole local Part body; Assembly occurrences match only their exact
// stable instance path.  Keeping this decision on ViewerEdge metadata avoids
// deriving a second face-boundary overlay for whole-body highlighting.
[[nodiscard]] bool candidate_recolors_wire_edge(
    const ViewerCandidate& candidate,
    const zima::kernel::ViewerEdge& edge);

// Ordinary Part history hover/selection presents a native container through
// its complete persisted source wire, not through the few result-body edges
// that survived later operations. Imported STEP containers deliberately stay
// on the cheap result-wire path because replaying their complete source wire
// would make hover proportional to the full import size.
[[nodiscard]] bool candidate_uses_original_container_wire_edge(
    const ViewerCandidate& candidate,
    const zima::kernel::ViewerEdge& edge);

// Only an exact Face candidate needs a derived screen-space boundary and
// silhouette. Whole solid Containers recolour their already-calculated GL
// wire instead, avoiding duplicate geometry and an O(triangle-count) paint.
[[nodiscard]] bool candidate_uses_face_boundary_overlay(
    const ViewerCandidate& candidate);

// Returns the already-calculated Body wire bounding every visible generated
// face of one Fillet/Chamfer feature. Treatment faces remain hit targets only;
// callers colour these exact persisted edges instead of a face overlay.
[[nodiscard]] std::vector<PickCandidate> ordered_ray_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction);

[[nodiscard]] std::size_t next_candidate_index(
    std::size_t current, std::size_t candidate_count);

[[nodiscard]] std::vector<EdgePickCandidate> ordered_edge_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);

[[nodiscard]] std::vector<VertexPickCandidate> ordered_vertex_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);
[[nodiscard]] std::vector<AxisPickCandidate> ordered_axis_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);
[[nodiscard]] std::vector<DimensionPickCandidate> ordered_dimension_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);

[[nodiscard]] std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);
[[nodiscard]] std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::ViewerMesh& persisted_references,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);

[[nodiscard]] std::vector<ViewerCandidate> filter_candidates(
    const std::vector<ViewerCandidate>& candidates,
    const std::vector<CandidateKind>& allowed_kinds);
[[nodiscard]] std::vector<ViewerCandidate> filter_candidates(
    const std::vector<ViewerCandidate>& candidates,
    const std::vector<CandidateKind>& allowed_kinds,
    const std::function<bool(const ViewerCandidate&)>& candidate_filter);

[[nodiscard]] std::optional<ViewerCandidate> container_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& owner_id,
    const std::string& instance_path = {});
[[nodiscard]] std::optional<ViewerCandidate> occurrence_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& instance_path);

}  // namespace zima::viewer
