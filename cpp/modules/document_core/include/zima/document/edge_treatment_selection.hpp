#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>

namespace zima::document {
inline bool same_treatment_edge_geometry(const kernel::ViewerEdge& first,
        const kernel::ViewerEdge& second) {
    if(first.points.size()<2||second.points.size()<2)return false;
    const auto squared_distance=[](const kernel::Vec3& first,
            const kernel::Vec3& second) {
        const double x=first.x-second.x,y=first.y-second.y,z=first.z-second.z;
        return x*x+y*y+z*z;
    };
    const auto length=[&](const kernel::ViewerEdge& edge) {
        double result{};
        for(std::size_t i=1;i<edge.points.size();++i)
            result+=std::sqrt(squared_distance(edge.points[i-1],edge.points[i]));
        return result;
    };
    const double first_length=length(first),second_length=length(second);
    const double tolerance=1.0e-7*std::max({1.0,first_length,second_length});
    if(std::abs(first_length-second_length)>tolerance)return false;
    const auto near_segment=[&](const kernel::Vec3& point,
            const kernel::Vec3& start,const kernel::Vec3& end) {
        const kernel::Vec3 delta{end.x-start.x,end.y-start.y,end.z-start.z};
        const kernel::Vec3 offset{point.x-start.x,point.y-start.y,point.z-start.z};
        const double denominator=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
        const double parameter=denominator<=1.0e-30?0.0:std::clamp(
            (offset.x*delta.x+offset.y*delta.y+offset.z*delta.z)/denominator,
            0.0,1.0);
        return squared_distance(point,{start.x+parameter*delta.x,
            start.y+parameter*delta.y,start.z+parameter*delta.z})<=
            tolerance*tolerance;
    };
    const auto follows=[&](const auto& source,const auto& target) {
        return std::ranges::all_of(source.points,[&](const auto& point) {
            for(std::size_t i=1;i<target.points.size();++i)
                if(near_segment(point,target.points[i-1],target.points[i]))
                    return true;
            return false;
        });
    };
    return follows(first,second)&&follows(second,first);
}

inline std::vector<kernel::EdgeReference> edge_treatment_intervals_from_seed(
        const std::vector<kernel::EdgeReference>& route,
        const kernel::EdgeReference& seed) {
    static_cast<void>(seed);
    // Exact persisted endpoint identities and exact curve tangents already
    // prove one unambiguous continuous route. Do not discard a circular or
    // ordinary segment merely because another member is a Boolean interval:
    // mixed-provenance line/arc routes are valid authored treatment paths.
    return route;
}

inline std::vector<kernel::EdgeReference> actual_edge_treatment_intervals(
        const std::vector<kernel::EdgeReference>& route) {
    return route;
}

inline std::vector<kernel::ViewerEdge> edge_treatment_intervals_from_seed(
        const std::vector<kernel::ViewerEdge>& route,
        const kernel::EdgeReference& seed) {
    static_cast<void>(seed);
    return route;
}

inline std::vector<kernel::ViewerEdge> actual_edge_treatment_intervals(
        const std::vector<kernel::ViewerEdge>& route) {
    return route;
}

inline std::vector<kernel::ViewerEdge> treatment_selection_wire(
        const document::EdgeTreatmentParameters& parameters, std::size_t route,
        std::optional<std::size_t> segment, const kernel::ViewerMesh& input) {
    std::vector<kernel::ViewerEdge> result;
    if (route>=parameters.routes.size()) return result;
    const auto selected=segment ? parameters.routes[route] :
        actual_edge_treatment_intervals(parameters.routes[route]);
    std::set<std::pair<bool,std::size_t>> appended;
    for (std::size_t i=0;i<selected.size();++i) {
        if (segment && i!=*segment) continue;
        std::vector<std::size_t> exact,descendants;
        for (std::size_t edge_index=0;edge_index<input.edges.size();++edge_index) {
            const auto& reference=input.edges[edge_index].reference;
            if(reference==selected[i])exact.push_back(edge_index);
            else if(kernel::edge_reference_matches_or_descends_from(
                        reference,selected[i]))descendants.push_back(edge_index);
        }
        // A two-sided or later Boolean operation can split one persisted
        // authored edge into several current operational fragments.  Those
        // fragments are the real Fillet/Chamfer input and carry the current
        // face guides; an obsolete exact parent or original preview edge must
        // never hide them.
        if(!descendants.empty()) {
            for(const auto edge_index:descendants)
                if(appended.insert({false,edge_index}).second)
                    result.push_back(input.edges[edge_index]);
            continue;
        }
        if(!exact.empty()) {
            for(const auto edge_index:exact)
                if(appended.insert({false,edge_index}).second)
                    result.push_back(input.edges[edge_index]);
            continue;
        }
        if(!kernel::is_boolean_intersection_interval(selected[i])) {
            bool found_original=false;
            for(std::size_t edge_index=0;
                    edge_index<input.original_references.edges.size();
                    ++edge_index) {
                const auto& edge=input.original_references.edges[edge_index];
                if(edge.reference!=selected[i])continue;
                found_original=true;
                // A compact saved rollback packet can retain the authored
                // reference wire separately from the current Body wire.  The
                // latter owns the real adjacent-face guides but may be
                // deliberately untagged when two authored Feature sides meet.
                // Reconnect only one geometrically identical anonymous Body
                // edge; ambiguity remains unresolved instead of guessing.
                std::optional<std::size_t> current;
                for(std::size_t candidate=0;candidate<input.edges.size();++candidate) {
                    const auto& body_edge=input.edges[candidate];
                    const bool complete_guides=
                        body_edge.edge_treatment_side_directions.size()==2&&
                        body_edge.edge_treatment_side_references.size()==2&&
                        std::ranges::all_of(
                            body_edge.edge_treatment_side_directions,
                            [&](const auto& directions) {
                                return directions.size()==body_edge.points.size();
                            });
                    if(body_edge.reference.valid()||!complete_guides||
                            !same_treatment_edge_geometry(edge,body_edge))continue;
                    if(current) {current.reset();break;}
                    current=candidate;
                }
                if(current) {
                    if(appended.insert({false,*current}).second) {
                        auto resolved=input.edges[*current];
                        resolved.reference=edge.reference;
                        result.push_back(std::move(resolved));
                    }
                    continue;
                }
                if(appended.insert({true,edge_index}).second)
                    result.push_back(edge);
            }
            if(found_original)continue;
        }
    }
    return result;
}

// Splitting uses persisted endpoint ancestry from the real input boundary.
// It does not ask OCCT to rediscover contours during a Tree interaction.
inline void remove_treatment_selection(document::EdgeTreatmentParameters& parameters,
        std::size_t route, std::optional<std::size_t> segment,
        const kernel::ViewerMesh& input) {
    if (route>=parameters.routes.size()) throw std::runtime_error("Trasa již neexistuje");
    const auto original=parameters.routes[route];
    if (segment && *segment>=original.size()) throw std::runtime_error("Segment již neexistuje");
    parameters.route_start_vertices.resize(parameters.routes.size());
    parameters.route_end_faces.resize(parameters.routes.size());
    const auto old_start=parameters.route_start_vertices[route];
    if (!segment || original.size()==1) {
        parameters.routes.erase(parameters.routes.begin()+route);
        parameters.route_start_vertices.erase(parameters.route_start_vertices.begin()+route);
        parameters.route_end_faces.erase(parameters.route_end_faces.begin()+route);
        return;
    }
    using Vertex=kernel::VertexReference;
    const auto key=[](const Vertex& v) {return std::pair{v.owner_id,v.semantic_key};};
    using Key=decltype(key(old_start));
    std::vector<std::vector<Vertex>> endpoints(original.size());
    std::map<Key,std::vector<std::size_t>> incident;
    for (std::size_t i=0;i<original.size();++i) {
        const auto edge=std::ranges::find_if(input.edges,[&](const auto& e){return e.reference==original[i];});
        if (edge==input.edges.end()) throw std::runtime_error("Chybí uložená vstupní hrana trasy");
        endpoints[i]=edge->edge_treatment_endpoint_references;
        for (const auto& v : endpoints[i]) if (v.valid()) incident[key(v)].push_back(i);
    }
    if (parameters.fillet_mode==document::EdgeTreatmentParameters::FilletMode::Linear &&
        (!old_start.valid() || !incident.contains(key(old_start))))
        throw std::runtime_error("Chybí uložený počátek R1 proměnného zaoblení");
    std::map<Key,std::size_t> distance;
    std::queue<Key> queue;
    if (old_start.valid()) {distance[key(old_start)]=0;queue.push(key(old_start));}
    while (!queue.empty()) {
        const auto current=queue.front();queue.pop();
        for (auto edge : incident[current]) for (const auto& next : endpoints[edge])
            if (next.valid() && !distance.contains(key(next))) {
                distance[key(next)]=distance[current]+1;queue.push(key(next));
            }
    }
    std::set<std::size_t> remaining;
    for (std::size_t i=0;i<original.size();++i) if (i!=*segment) remaining.insert(i);
    std::vector<std::vector<kernel::EdgeReference>> groups;
    std::vector<Vertex> starts;
    while (!remaining.empty()) {
        std::vector<std::size_t> connected{*remaining.begin()};remaining.erase(connected.front());
        for (std::size_t i=0;i<connected.size();++i)
            for (const auto& v : endpoints[connected[i]]) if (v.valid())
                for (const auto neighbor : incident[key(v)])
                    if (remaining.erase(neighbor)) connected.push_back(neighbor);
        std::sort(connected.begin(),connected.end());
        std::map<Key,std::pair<Vertex,int>> degrees;
        std::vector<kernel::EdgeReference> group;
        for (const auto i : connected) {
            group.push_back(original[i]);
            for (const auto& v : endpoints[i]) if (v.valid()) {
                auto& entry=degrees[key(v)];entry.first=v;++entry.second;
            }
        }
        Vertex start;
        std::size_t best=std::numeric_limits<std::size_t>::max();
        for (const auto& [k,entry] : degrees) if (entry.second==1) {
            const auto found=distance.find(k);
            const auto rank=found==distance.end() ? best : found->second;
            if (!start.valid() || rank<best) {start=entry.first;best=rank;}
        }
        // Closed constant-radius contours need no R1 endpoint.
        groups.push_back(std::move(group));starts.push_back(start);
    }
    parameters.routes.erase(parameters.routes.begin()+route);
    parameters.route_start_vertices.erase(parameters.route_start_vertices.begin()+route);
    parameters.route_end_faces.erase(parameters.route_end_faces.begin()+route);
    parameters.routes.insert(parameters.routes.begin()+route,groups.begin(),groups.end());
    parameters.route_start_vertices.insert(parameters.route_start_vertices.begin()+route,starts.begin(),starts.end());
    // Removing a segment creates new route endpoints. Reusing the old end
    // faces would attach them to the wrong geometry, so the new fragments are
    // deliberately perpendicular until the user runs Auto or picks a face.
    parameters.route_end_faces.insert(parameters.route_end_faces.begin()+route,
        groups.size(),document::EdgeTreatmentParameters::RouteEndFaces{});
}
}
