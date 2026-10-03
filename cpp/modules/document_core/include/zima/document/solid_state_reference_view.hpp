#pragma once
#include <zima/document/history_reference_view.hpp>
#include <zima/document/placement_types.hpp>
#include <span>
#include <tuple>

namespace zima::document {

// Build an evaluation view from calculated original-source packets. The packet
// is an alias at a history boundary, not newly owned state topology. Persisted
// source references and their side/offset/branch choices are never rewritten.
inline HistoryReferenceView solid_state_reference_view(
        const kernel::ViewerReferenceGeometry& authored,
        const kernel::ViewerReferenceGeometry& calculated,
        std::set<HistoryReferenceView::Owner> owners,
        std::span<const ConstructionReference> required) {
    // Validate before compaction (which dereferences mesh indices). Do not copy
    // the complete authored mesh merely to inspect its reference inventory.
    const auto validate=[](const kernel::ViewerReferenceGeometry& packet) {
        if(packet.triangles.size()%3 || packet.triangles.size()/3!=packet.triangle_references.size())
            throw std::invalid_argument("History reference view is invalid.");
        for(auto index:packet.triangles)if(index>=packet.vertices.size())
            throw std::invalid_argument("History reference view is invalid.");
    };
    validate(authored);validate(calculated);
    for(const auto& owner:owners)if(owner.first.empty())
        throw std::invalid_argument("History reference view is invalid.");
    HistoryReferenceView result{std::move(owners),calculated};
    history_reference_detail::retain(result.geometry,result.owners,true);

    using Key=std::tuple<std::string,std::string,std::string>;
    const auto inventory=[&](const kernel::ViewerReferenceGeometry& packet) {
        std::map<Key,unsigned> keys;
        const auto add=[&](const auto& ref,unsigned kind) {
            if(result.owners.contains({ref.owner_id,ref.instance_path}))
                keys[{ref.owner_id,ref.instance_path,ref.semantic_key}]|=kind;
        };
        // A face has multiple triangles and an edge may have multiple sampled
        // pieces. Repeated keys within one topology kind are not new identities.
        for(const auto& ref:packet.triangle_references)add(ref,1);
        for(const auto& edge:packet.edges)add(edge.reference,2);
        for(const auto& point:packet.points)add(point.reference,4);
        for(const auto& axis:packet.axes)add(axis.reference,8);
        return keys;
    };
    const auto before=inventory(authored),after=inventory(result.geometry);
    for(const auto& ref:required) {
        if(!result.owners.contains({ref.owner_id,ref.instance_path}))continue;
        const Key key{ref.owner_id,ref.instance_path,ref.semantic_key};
        const auto source=before.find(key),target=after.find(key);
        if(source==before.end() || target==after.end() || source->second!=target->second ||
           (source->second&(source->second-1))!=0)
            throw std::invalid_argument("A solid state reference has no unique correspondence.");
    }
    return result;
}
} // namespace zima::document
