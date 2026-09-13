#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
namespace zima::kernel {
// Shared traversal over persisted viewer edges. Geometry indices select
// an offered runtime edge, never define a persisted topology identity.
inline std::vector<EdgeReference> tangent_edge_route(const std::vector<ViewerEdge>& edges,
    const EdgeReference& seed,double angular_tolerance_degrees=35,
    double position_tolerance=1e-6,std::optional<std::size_t> picked_index={}) {
    if(!seed.valid()||!std::isfinite(angular_tolerance_degrees)||angular_tolerance_degrees<0||
        angular_tolerance_degrees>90||!std::isfinite(position_tolerance)||position_tolerance<0)return {};
    struct Endpoint { std::size_t edge{}; int end{}; };
    const double tolerance = std::max(1.0e-6, position_tolerance);
    using VertexKey = std::pair<std::string, std::string>;
    const auto endpoint_key = [](const zima::kernel::ViewerEdge& edge, int end)
            -> std::optional<VertexKey> {
        if (edge.edge_treatment_endpoint_references.size() != 2) {
            return std::nullopt;
        }
        const auto& reference = edge.edge_treatment_endpoint_references[
            end == 0 ? 0 : 1];
        if (!reference.valid()) return std::nullopt;
        // All candidate edges were already restricted to one occurrence
        // below. The endpoint's persisted ZIMA owner + semantic key is the
        // exact topological connection; spatial proximity is not.
        return VertexKey{reference.owner_id, reference.semantic_key};
    };
    std::map<VertexKey, std::vector<Endpoint>> endpoints;
    std::optional<std::size_t> seed_index;
    for (std::size_t index = 0; index < edges.size(); ++index) {
        const auto& edge = edges[index];
        // A geometrically continuous result edge can cross the persisted
        // provenance boundary between two additive containers. Fillet and
        // Chamfer operate on the real input body, so their tangent route must
        // cross that boundary as well. The stable owner remains attached to
        // every member returned below; only the occurrence path is common.
        if (!edge.reference.valid() ||
            edge.reference.instance_path != seed.instance_path ||
            edge.points.size() < 2) continue;
        if (const auto front = endpoint_key(edge, 0)) {
            endpoints[*front].push_back({index, 0});
        }
        if (const auto back = endpoint_key(edge, 1)) {
            endpoints[*back].push_back({index, 1});
        }
        if (picked_index && index == *picked_index &&
            edge.reference.owner_id == seed.owner_id &&
            edge.reference.semantic_key == seed.semantic_key) {
            seed_index = index;
        }
    }
    // Programmatic route restoration has no picked geometry index. Resolve
    // it only when the persisted edge identity is unique in the displayed
    // body. A live hover/click always takes the exact offered mesh edge above.
    if (!seed_index) {
        for (std::size_t index = 0; index < edges.size(); ++index) {
            const auto& edge = edges[index];
            if (edge.points.size() < 2 || !edge.reference.valid() ||
                edge.reference.owner_id != seed.owner_id ||
                edge.reference.semantic_key != seed.semantic_key ||
                edge.reference.instance_path != seed.instance_path) {
                continue;
            }
            if (seed_index) return {};
            seed_index = index;
        }
    }
    if (!seed_index) return {};
    const auto direction = [&](std::size_t index, int end)
        -> std::optional<zima::kernel::Vec3> {
        const auto& points = edges[index].points;
        const auto& origin = end == 0 ? points.front() : points.back();
        for (std::size_t step = 1; step < points.size(); ++step) {
            const auto& target = end == 0 ? points[step]
                                         : points[points.size() - 1 - step];
            zima::kernel::Vec3 value{target.x - origin.x,
                target.y - origin.y, target.z - origin.z};
            const double length = std::hypot(std::hypot(value.x, value.y), value.z);
            if (length > tolerance) return zima::kernel::Vec3{
                value.x / length, value.y / length, value.z / length};
        }
        return std::nullopt;
    };
    const double limit = std::cos(angular_tolerance_degrees *
        std::numbers::pi / 180.0);
    std::set<std::size_t> selected{*seed_index};
    std::vector<std::size_t> pending{*seed_index};
    std::set<VertexKey> visited_vertices;
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        for (int end = 0; end < 2; ++end) {
            const auto current_direction = direction(current, end);
            const auto vertex = endpoint_key(edges[current], end);
            if (!current_direction || !vertex) continue;
            // Once the route has crossed one vertex, do not enter the same
            // junction again from the newly added edge and accidentally take
            // a different branch back out of it.
            if (!visited_vertices.insert(*vertex).second) continue;
            std::vector<std::size_t> continuations;
            for (const auto candidate : endpoints[*vertex]) {
                if (candidate.edge == current || selected.contains(candidate.edge)) continue;
                const auto candidate_direction = direction(candidate.edge, candidate.end);
                if (!candidate_direction) continue;
                const double alignment =
                    current_direction->x * candidate_direction->x +
                    current_direction->y * candidate_direction->y +
                    current_direction->z * candidate_direction->z;
                // Both vectors point away from their common endpoint. A
                // continuous route therefore has antiparallel tangents. The
                // old abs(dot) also accepted a curve that doubled back on the
                // same side, which could pull a disconnected-looking Fillet
                // boundary into the selected route.
                if (alignment <= -limit) continuations.push_back(candidate.edge);
            }
            std::sort(continuations.begin(), continuations.end());
            continuations.erase(std::unique(continuations.begin(), continuations.end()),
                                continuations.end());
            if (continuations.size() != 1) continue;
            selected.insert(continuations.front());
            pending.push_back(continuations.front());
        }
    }
    std::vector<zima::kernel::EdgeReference> result;
    result.reserve(selected.size());
    for (const auto index : selected) result.push_back(edges[index].reference);
    return result;
}
} // namespace zima::kernel
