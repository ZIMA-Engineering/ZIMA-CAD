#pragma once
#include <zima/document/part_document.hpp>
#include <algorithm>
#include <map>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>

namespace zima::app {
inline std::vector<kernel::ViewerEdge> treatment_selection_wire(
        const document::EdgeTreatmentParameters& parameters, std::size_t route,
        std::optional<std::size_t> segment, const kernel::ViewerMesh& input) {
    std::vector<kernel::ViewerEdge> result;
    if (route>=parameters.routes.size()) return result;
    const auto& selected=parameters.routes[route];
    for (std::size_t i=0;i<selected.size();++i) {
        if (segment && i!=*segment) continue;
        for (const auto& edge : input.edges)
            if (edge.reference==selected[i]) result.push_back(edge);
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
    const auto old_start=parameters.route_start_vertices[route];
    if (!segment || original.size()==1) {
        parameters.routes.erase(parameters.routes.begin()+route);
        parameters.route_start_vertices.erase(parameters.route_start_vertices.begin()+route);
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
    parameters.routes.insert(parameters.routes.begin()+route,groups.begin(),groups.end());
    parameters.route_start_vertices.insert(parameters.route_start_vertices.begin()+route,starts.begin(),starts.end());
}
}
