#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <vector>

namespace zima::kernel {

// Runtime ordering for one explicitly selected Fillet/Chamfer route.  The
// persisted endpoint identities, rather than OCCT enumeration or coordinate
// proximity, determine connectivity and direction.  Coordinates only reject
// inconsistent copies of the same persisted endpoint.
struct EdgeTreatmentRouteSegment {
    std::size_t edge_index{};
    bool reversed{};
    VertexReference start;
    VertexReference end;
};

struct EdgeTreatmentRouteTerminal {
    VertexReference reference;
    Vec3 position;
    std::size_t selected_incidence{};
    std::size_t body_incidence{};
};

struct EdgeTreatmentRoutePlan {
    std::vector<EdgeTreatmentRouteSegment> segments;
    // Closed routes have no artificial start/end cap.  The first segment is
    // nevertheless deterministic so generated topology remains reproducible.
    bool closed{};
    // Empty for a closed route; otherwise exactly R1/start and R2/end.
    std::vector<EdgeTreatmentRouteTerminal> terminals;
};

inline bool edge_treatment_route_supports_linear_fillet(
        const EdgeTreatmentRoutePlan& route) {
    return !route.closed&&route.terminals.size()==2;
}

inline std::optional<EdgeTreatmentRoutePlan>
build_edge_treatment_route_plan(
        const std::vector<ViewerEdge>& body_edges,
        std::span<const std::size_t> selected_edge_indices,
        const std::optional<VertexReference>& authored_start = {},
        double coordinate_tolerance = 1.0e-6) {
    if(selected_edge_indices.empty()||
       !std::isfinite(coordinate_tolerance)||coordinate_tolerance<0.)
        return {};

    using VertexKey=std::tuple<std::string,std::string,std::string>;
    const auto vertex_key=[](const VertexReference& reference) {
        return VertexKey{reference.owner_id,reference.semantic_key,
            reference.instance_path};
    };
    const auto edge_key=[](const EdgeReference& reference) {
        return std::tuple{reference.owner_id,reference.semantic_key,
            reference.instance_path};
    };
    const auto endpoint_position=[](const ViewerEdge& edge,
            std::size_t end) {
        return end==0?edge.points.front():edge.points.back();
    };
    const auto distance=[](const Vec3& first,const Vec3& second) {
        return std::hypot(std::hypot(first.x-second.x,
            first.y-second.y),first.z-second.z);
    };
    struct VertexData {
        VertexReference reference;
        Vec3 position;
        std::vector<std::pair<std::size_t,std::size_t>> selected;
        std::set<std::size_t> body;
    };
    struct Connection {
        std::array<VertexKey,2> ends;
    };
    std::map<VertexKey,VertexData> vertices;
    const auto append_vertex=[&](const ViewerEdge& edge,
            std::size_t edge_index,std::size_t end,bool selected) {
        if(edge.points.size()<2||
           edge.edge_treatment_endpoint_references.size()!=2)
            return false;
        const auto& reference=
            edge.edge_treatment_endpoint_references[end];
        if(!reference.valid())return false;
        const auto position=endpoint_position(edge,end);
        const auto key=vertex_key(reference);
        auto& vertex=vertices[key];
        if(!vertex.reference.valid()) {
            vertex.reference=reference;
            vertex.position=position;
        } else if(distance(vertex.position,position)>coordinate_tolerance) {
            return false;
        }
        vertex.body.insert(edge_index);
        if(selected)vertex.selected.emplace_back(edge_index,end);
        return true;
    };

    for(std::size_t edge_index=0;edge_index<body_edges.size();++edge_index) {
        const auto& edge=body_edges[edge_index];
        if(edge.parameter_seam||!edge.reference.valid()||edge.points.size()<2||
           edge.edge_treatment_endpoint_references.size()!=2)
            continue;
        if(!append_vertex(edge,edge_index,0,false)||
           !append_vertex(edge,edge_index,1,false))
            return {};
    }

    std::set<std::size_t> selected_unique;
    std::map<std::size_t,Connection> connections;
    for(const auto edge_index:selected_edge_indices) {
        if(edge_index>=body_edges.size()||
           !selected_unique.insert(edge_index).second)
            return {};
        const auto& edge=body_edges[edge_index];
        if(edge.parameter_seam||!edge.reference.valid()||edge.points.size()<2||
           edge.edge_treatment_endpoint_references.size()!=2)
            return {};
        if(!append_vertex(edge,edge_index,0,true)||
           !append_vertex(edge,edge_index,1,true))
            return {};
        connections.emplace(edge_index,Connection{{
            vertex_key(edge.edge_treatment_endpoint_references[0]),
            vertex_key(edge.edge_treatment_endpoint_references[1])}});
    }

    std::vector<VertexKey> terminals;
    for(const auto& [key,vertex]:vertices) {
        if(vertex.selected.empty())continue;
        if(vertex.selected.size()>2)return {};
        if(vertex.selected.size()==1)terminals.push_back(key);
    }
    const bool closed=terminals.empty();
    if(!closed&&terminals.size()!=2)return {};

    VertexKey start;
    if(authored_start&&authored_start->valid()) {
        start=vertex_key(*authored_start);
        const auto found=vertices.find(start);
        if(found==vertices.end()||found->second.selected.empty()||
           (!closed&&std::ranges::find(terminals,start)==terminals.end()))
            return {};
    } else if(closed) {
        const auto found=std::ranges::find_if(vertices,
            [](const auto& entry) {
                return !entry.second.selected.empty();
            });
        if(found==vertices.end())return {};
        start=found->first;
    } else {
        start=*std::ranges::min_element(terminals);
    }

    EdgeTreatmentRoutePlan result;
    result.closed=closed;
    result.segments.reserve(selected_unique.size());
    std::set<std::size_t> visited;
    auto current=start;
    while(visited.size()<selected_unique.size()) {
        const auto vertex=vertices.find(current);
        if(vertex==vertices.end())return {};
        std::vector<std::size_t> candidates;
        for(const auto& [edge_index,end]:vertex->second.selected) {
            static_cast<void>(end);
            if(!visited.contains(edge_index))candidates.push_back(edge_index);
        }
        std::ranges::sort(candidates,[&](std::size_t first,
                std::size_t second) {
            return std::tuple{edge_key(body_edges[first].reference),first}<
                std::tuple{edge_key(body_edges[second].reference),second};
        });
        if(candidates.empty())return {};
        // Only the first vertex of a closed route can offer both directions.
        // Its stable edge identity provides a reproducible direction.
        if(candidates.size()>1&&!(closed&&visited.empty()))return {};
        const auto edge_index=candidates.front();
        const auto& connection=connections.at(edge_index);
        const bool reversed=connection.ends[0]!=current;
        if(reversed&&connection.ends[1]!=current)return {};
        const auto next=connection.ends[reversed?0:1];
        const auto& edge=body_edges[edge_index];
        result.segments.push_back({edge_index,reversed,
            edge.edge_treatment_endpoint_references[reversed?1:0],
            edge.edge_treatment_endpoint_references[reversed?0:1]});
        visited.insert(edge_index);
        current=next;
    }
    if((closed&&current!=start)||
       (!closed&&current==start)||
       (!closed&&std::ranges::find(terminals,current)==terminals.end()))
        return {};

    if(!closed) {
        const auto append_terminal=[&](const VertexKey& key) {
            const auto& vertex=vertices.at(key);
            result.terminals.push_back({vertex.reference,vertex.position,
                vertex.selected.size(),vertex.body.size()});
        };
        append_terminal(start);
        append_terminal(current);
    }
    return result;
}

} // namespace zima::kernel
