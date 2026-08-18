#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <cstddef>
#include <optional>
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

enum class CandidateKind { Occurrence, Container, Face, Edge, Vertex, Axis };

struct ViewerCandidate {
    CandidateKind kind{CandidateKind::Container};
    double distance{};
    std::size_t geometry_index{};
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;

    bool operator==(const ViewerCandidate&) const = default;
};

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

[[nodiscard]] std::vector<ViewerCandidate> ordered_viewer_candidates(
    const zima::kernel::ViewerMesh& mesh,
    const zima::kernel::Vec3& ray_origin,
    const zima::kernel::Vec3& ray_direction,
    double world_tolerance);

[[nodiscard]] std::vector<ViewerCandidate> filter_candidates(
    const std::vector<ViewerCandidate>& candidates,
    const std::vector<CandidateKind>& allowed_kinds);

[[nodiscard]] std::optional<ViewerCandidate> container_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& owner_id,
    const std::string& instance_path = {});
[[nodiscard]] std::optional<ViewerCandidate> occurrence_candidate(
    const zima::kernel::ViewerMesh& mesh, const std::string& instance_path);

}  // namespace zima::viewer
