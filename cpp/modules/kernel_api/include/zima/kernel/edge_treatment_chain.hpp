#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace zima::kernel {
struct EdgeTreatmentChainEndpoint {
    VertexReference reference;
    Vec3 position;
};
struct EdgeTreatmentChain {
    bool endpoints_complete{};
    std::vector<EdgeTreatmentChainEndpoint> endpoints;
};

// One original curve can have several calculated segments at a periodic seam.
// Only persisted vertex identities establish connectivity. Coordinates merely
// reject inconsistent copies of the SAME vertex; proximity never joins IDs.
inline std::optional<EdgeTreatmentChain> edge_treatment_chain(
        const std::vector<const ViewerEdge*>& segments) {
    if(segments.empty()||!segments.front()||!segments.front()->reference.valid())return {};
    if(segments.size()==1) {
        const auto& edge=*segments.front();const auto& ends=edge.edge_treatment_endpoint_references;
        if(edge.points.empty()||ends.size()!=2||
            std::ranges::any_of(ends,[](const auto& ref){return !ref.valid();}))return EdgeTreatmentChain{};
        EdgeTreatmentChain result;result.endpoints_complete=true;
        if(ends.front()!=ends.back())result.endpoints={{ends.front(),edge.points.front()},{ends.back(),edge.points.back()}};
        return result;
    }
    using Key=std::tuple<std::string,std::string,std::string>;
    struct Vertex {EdgeTreatmentChainEndpoint value;std::vector<std::size_t> edges;};
    std::map<Key,Vertex> vertices;
    std::vector<std::array<Key,2>> connections;
    std::set<const ViewerEdge*> unique;
    for(const auto* edge:segments) {
        if(!edge||edge->reference!=segments.front()->reference||!unique.insert(edge).second||
            edge->points.empty()||edge->edge_treatment_endpoint_references.size()!=2)return {};
        connections.emplace_back();
        for(std::size_t end=0;end<2;++end) {
            const auto& ref=edge->edge_treatment_endpoint_references[end];
            if(!ref.valid())return {};
            const auto position=end==0?edge->points.front():edge->points.back();
            const Key key{ref.owner_id,ref.semantic_key,ref.instance_path};
            auto& vertex=vertices[key];
            if(vertex.edges.empty())vertex.value={ref,position};
            else {
                const auto& other=vertex.value.position;
                const auto error=std::hypot(position.x-other.x,position.y-other.y,position.z-other.z);
                if(!std::isfinite(error)||error>1e-6)return {};
            }
            vertex.edges.push_back(connections.size()-1);
            if(vertex.edges.size()>2)return {};
            connections.back()[end]=key;
        }
    }
    std::set<std::size_t> visited{0};std::vector<std::size_t> pending{0};
    while(!pending.empty()) {
        const auto edge=pending.back();pending.pop_back();
        for(const auto& point:connections[edge])for(const auto next:vertices.at(point).edges)
            if(visited.insert(next).second)pending.push_back(next);
    }
    if(visited.size()!=segments.size())return {};
    if(segments.size()==2&&vertices.size()==2) {
        const auto& first=segments.front()->points;const auto& second=segments.back()->points;
        const auto equal=[](const auto& a,const auto& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
        if(first.size()==second.size()&&
            (std::equal(first.begin(),first.end(),second.begin(),equal)||
             std::equal(first.begin(),first.end(),second.rbegin(),equal)))return {};
    }
    EdgeTreatmentChain result;result.endpoints_complete=true;
    for(const auto& [id,vertex]:vertices)if(vertex.edges.size()==1)result.endpoints.push_back(vertex.value);
    if(!result.endpoints.empty()&&result.endpoints.size()!=2)return {};
    return result;
}
} // namespace zima::kernel
