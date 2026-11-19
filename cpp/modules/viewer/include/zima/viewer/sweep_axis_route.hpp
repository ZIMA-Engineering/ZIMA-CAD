#pragma once
#include <zima/viewer/picking.hpp>
#include <zima/kernel/placement_route.hpp>

namespace zima::viewer {
// A calculated Sweep axis names its authored path segment. Offer the owned
// path's existing whole-route reference directly after that visible segment.
// The individual axis reference and the placement route solver are unchanged.
inline std::optional<ViewerCandidate> sweep_axis_route_source(const ViewerCandidate& axis,
        const std::vector<kernel::ViewerEdge>& references) {
    constexpr std::string_view prefix="centerline:from:";
    if((axis.kind!=CandidateKind::Axis&&axis.kind!=CandidateKind::Edge)||
       !axis.semantic_key.starts_with(prefix))return {};
    const auto source=std::string_view(axis.semantic_key).substr(prefix.size());
    std::optional<ViewerCandidate> result;
    for(std::size_t i=0;i<references.size();++i) {
        const auto& edge=references[i];const auto& ref=edge.reference;
        if(edge.display_owner_id!=axis.owner_id||ref.instance_path!=axis.instance_path)continue;
        auto key=std::string_view(ref.semantic_key);
        if(key.starts_with(kernel::placement_evaluated_prefix))key.remove_prefix(kernel::placement_evaluated_prefix.size());
        if(!kernel::is_placement_route_member(key))continue;
        const auto colon=key.find(':');
        if(key!=source&&(key.starts_with("curve:")||colon==std::string_view::npos||key.substr(colon+1)!=source))continue;
        if(result&&(result->owner_id!=ref.owner_id||result->semantic_key!=key))return {};
        auto candidate=axis;candidate.kind=CandidateKind::Edge;candidate.geometry=CandidateGeometry::OriginalReference;
        candidate.geometry_index=i;candidate.owner_id=ref.owner_id;candidate.semantic_key=key;
        result=std::move(candidate);
    }
    return result;
}
}
