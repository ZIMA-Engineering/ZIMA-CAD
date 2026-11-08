#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/kernel/curve_evaluation.hpp>
#include <zima/kernel/edge_treatment_chain.hpp>
#include <zima/kernel/edge_treatment_route_plan.hpp>
#include <algorithm>
#include <array>
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
    const bool has_exact_pick=seed_index.has_value();
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
    const auto material_side_signature=[&](std::size_t index,int end)
            ->std::optional<std::array<Vec3,2>> {
        const auto& edge=edges[index];
        if(edge.edge_treatment_side_directions.size()!=2||
           edge.edge_treatment_side_directions[0].size()!=edge.points.size()||
           edge.edge_treatment_side_directions[1].size()!=edge.points.size())
            return std::nullopt;
        const auto sample=end==0?std::size_t{}:edge.points.size()-1;
        const auto& first=edge.edge_treatment_side_directions[0][sample];
        const auto& second=edge.edge_treatment_side_directions[1][sample];
        const double first_length=std::hypot(std::hypot(first.x,first.y),first.z);
        const double second_length=std::hypot(std::hypot(second.x,second.y),second.z);
        if(!std::isfinite(first_length)||!std::isfinite(second_length)||
           first_length<=1.0e-12||second_length<=1.0e-12)
            return std::nullopt;
        return std::array{
            Vec3{first.x/first_length,first.y/first_length,
                first.z/first_length},
            Vec3{second.x/second_length,second.y/second_length,
                second.z/second_length}};
    };
    const auto dot=[](const Vec3& first,const Vec3& second) {
        return std::clamp(first.x*second.x+first.y*second.y+
            first.z*second.z,-1.,1.);
    };
    std::set<std::size_t> selected{*seed_index};
    std::vector<std::size_t> pending{*seed_index};
    // A live pick identifies one calculated fragment exactly. Let ordinary
    // tangent traversal add only compatible continuations. Programmatic
    // restoration still starts from every connected fragment represented by
    // the one persisted source identity, because no runtime index exists.
    if(seed_chain&&!has_exact_pick)
        for(const auto index:seed_indices)
            if(selected.insert(index).second)pending.push_back(index);
    std::set<VertexKey> visited_vertices;
    while (!pending.empty()) {
        const auto current = pending.back(); pending.pop_back();
        for (int end = 0; end < 2; ++end) {
            const auto current_direction = direction(current, end);
            const auto current_material=material_side_signature(current,end);
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
                const auto candidate_material=material_side_signature(
                    candidate.edge,candidate.end);
                // Geometric tangency alone is insufficient at a Boolean
                // branch: the same authored curve can continue from a real
                // corner into an old smooth seam.  When both calculated
                // fragments publish their exact face-side directions, retain
                // only a continuation with the same material opening.
                if(current_material&&candidate_material) {
                    const double current_opening=dot(
                        (*current_material)[0],(*current_material)[1]);
                    const double candidate_opening=dot(
                        (*candidate_material)[0],(*candidate_material)[1]);
                    if(std::abs(current_opening-candidate_opening)>1.0e-6)
                        continue;
                    // Equal opening angles are not enough. At a Boolean
                    // vertex the same 90-degree material wedge can continue
                    // onto a different pair of faces, making a visually sharp
                    // break even though the two edge curves are tangent. Both
                    // inward material-side directions must continue (their
                    // stored order is not significant). A true line/arc or
                    // split-edge continuation reaches 1 for one pairing;
                    // a same-angle wedge turned onto another side is stopped.
                    const double direct=std::min(
                        dot((*current_material)[0],(*candidate_material)[0]),
                        dot((*current_material)[1],(*candidate_material)[1]));
                    const double swapped=std::min(
                        dot((*current_material)[0],(*candidate_material)[1]),
                        dot((*current_material)[1],(*candidate_material)[0]));
                    if(std::max(direct,swapped)<1.-1.0e-6)continue;
                }
                const double alignment =
                    current_direction->x * candidate_direction->x +
                    current_direction->y * candidate_direction->y +
                    current_direction->z * candidate_direction->z;
                // Both vectors point from the common endpoint into their
                // respective edges. A real continuation is therefore
                // antiparallel. Parallel vectors describe a 180-degree return
                // along the side the route just came from; tangency alone must
                // never absorb that reversing segment.
                // This is only a floating-point equality allowance, not an
                // authored angle threshold: a real corner, however small,
                // remains a route boundary.
                constexpr double exact_tangent_dot_tolerance=1.0e-8;
                if (alignment<=-1.+exact_tangent_dot_tolerance)
                    continuations.push_back(candidate.edge);
            }
            std::sort(continuations.begin(),continuations.end());
            continuations.erase(std::unique(continuations.begin(),
                continuations.end()),continuations.end());
            if (continuations.size() != 1) continue;
            selected.insert(continuations.front());
            pending.push_back(continuations.front());
        }
    }
    std::vector<std::size_t> ordered_indices(selected.begin(),selected.end());
    if(const auto plan=build_edge_treatment_route_plan(edges,ordered_indices)) {
        ordered_indices.clear();
        ordered_indices.reserve(plan->segments.size());
        for(const auto& segment:plan->segments)
            ordered_indices.push_back(segment.edge_index);
        // A picked edge already has an authored point order.  Preserve that
        // direction for the complete route instead of letting the lexical
        // identity of a terminal decide which end is shown first.
        const auto picked=std::ranges::find_if(plan->segments,
            [&](const auto& segment) {
                return edges[segment.edge_index].reference==seed;
            });
        if(picked!=plan->segments.end()&&picked->reversed)
            std::ranges::reverse(ordered_indices);
    }
    std::vector<zima::kernel::EdgeReference> result;
    result.reserve(selected.size());
    std::set<std::tuple<std::string_view,std::string_view,std::string_view>> identities;
    for (const auto index : ordered_indices) {
        const auto& reference=edges[index].reference;
        if(identities.emplace(reference.owner_id,reference.semantic_key,reference.instance_path).second)
            result.push_back(reference);
    }
    return result;
}

} // namespace zima::kernel
