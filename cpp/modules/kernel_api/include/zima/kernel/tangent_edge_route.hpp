#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/kernel/edge_treatment_chain.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
namespace zima::kernel {
// Shared traversal over persisted viewer edges. Geometry indices select
// an offered runtime edge, never define a persisted topology identity.
inline std::vector<EdgeReference> tangent_edge_route(const std::vector<ViewerEdge>& edges,
    const EdgeReference& seed,double position_tolerance=1e-6,
    std::optional<std::size_t> picked_index={}) {
    if(!seed.valid()||!std::isfinite(position_tolerance)||
       position_tolerance<0)return {};
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
    std::vector<const ViewerEdge*> seed_segments;
    std::vector<std::size_t> seed_indices;
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
        if(edge.reference==seed) {
            seed_segments.push_back(&edge);seed_indices.push_back(index);
        }
    }
    // Programmatic route restoration has no picked geometry index. Resolve
    // it only when the persisted identity names one connected curve. A live
    // hover/click retains the exact offered runtime edge above.
    const bool seed_chain=seed_segments.size()==1||edge_treatment_chain(seed_segments).has_value();
    if (!seed_index) {
        if(!seed_chain)return {};
        seed_index=seed_indices.front();
    }
    if (!seed_index) return {};
    const auto direction = [&](std::size_t index, int end)
        -> std::optional<zima::kernel::Vec3> {
        const auto& edge=edges[index];
        if(!edge.exact_spline)return std::nullopt;
        const auto& curve=*edge.exact_spline;
        try {curve.validate();}
        catch(const std::exception&) {return std::nullopt;}
        const auto& endpoint=end==0?edge.points.front():edge.points.back();
        const auto first=bspline_value(curve,0.);
        const auto last=bspline_value(curve,1.);
        const auto squared_distance=[&](const Vec3& point) {
            const double x=point.x-endpoint.x;
            const double y=point.y-endpoint.y;
            const double z=point.z-endpoint.z;
            return x*x+y*y+z*z;
        };
        const double first_distance=squared_distance(first);
        const double last_distance=squared_distance(last);
        const bool at_first=first_distance<=last_distance;
        const double coordinate_scale=std::max({1.,std::abs(endpoint.x),
            std::abs(endpoint.y),std::abs(endpoint.z)});
        const double endpoint_tolerance=std::max(tolerance,
            64.*std::numeric_limits<double>::epsilon()*coordinate_scale);
        if(std::min(first_distance,last_distance)>
                endpoint_tolerance*endpoint_tolerance)
            return std::nullopt;
        auto tangent=bspline_derivative(curve,at_first?0.:1.);
        if(!at_first) {
            tangent.x=-tangent.x;tangent.y=-tangent.y;
            tangent.z=-tangent.z;
        }
        const double length=std::hypot(
            std::hypot(tangent.x,tangent.y),tangent.z);
        if(!std::isfinite(length)||length<=1.0e-12*coordinate_scale)
            return std::nullopt;
        return zima::kernel::Vec3{tangent.x/length,
            tangent.y/length,tangent.z/length};
    };
    std::set<std::size_t> selected{*seed_index};
    std::vector<std::size_t> pending{*seed_index};
    if(seed_chain)for(const auto index:seed_indices)if(selected.insert(index).second)pending.push_back(index);
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
            std::vector<std::size_t> incident_edges;
            for(const auto& candidate:endpoints[*vertex])
                incident_edges.push_back(candidate.edge);
            std::sort(incident_edges.begin(),incident_edges.end());
            incident_edges.erase(std::unique(incident_edges.begin(),
                incident_edges.end()),incident_edges.end());
            // Tangent propagation is a curve continuation, not a choice at a
            // topological junction.  Even when exactly one of several exits
            // is geometrically tangent, a third incident edge is
            // an authored termination and must stop automatic Fillet/Chamfer
            // selection.  The user can still select the next edge explicitly.
            if(incident_edges.size()!=2)continue;
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
                // The exact native curve derivatives must be antiparallel.
                // This is only a floating-point equality allowance, not an
                // authored angle threshold: a real corner, however small,
                // remains a route boundary.
                constexpr double exact_tangent_dot_tolerance=1.0e-8;
                if (alignment <= -1.+exact_tangent_dot_tolerance)
                    continuations.push_back(candidate.edge);
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
    std::set<std::tuple<std::string_view,std::string_view,std::string_view>> identities;
    for (const auto index : selected) {
        const auto& reference=edges[index].reference;
        if(identities.emplace(reference.owner_id,reference.semantic_key,reference.instance_path).second)
            result.push_back(reference);
    }
    return result;
}
} // namespace zima::kernel
