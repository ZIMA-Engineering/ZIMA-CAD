#pragma once
#include <zima/document/history_reference_view.hpp>
#include <zima/document/placement_types.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <span>
#include <tuple>

namespace zima::document {

template<class Reference> inline HistoryReferenceView::Owner solid_state_source_owner(const Reference& ref) {
    auto owner=ref.owner_id,key=ref.semantic_key;
    while(const auto parent=kernel::solid_state_parent(key)){owner=parent->first;key=parent->second;}
    return {std::move(owner),ref.instance_path};
}

// Evaluate an already stored state identity against its current source frame.
// Authored snapshots remain immutable; this packet is calculation input only.
inline HistoryReferenceView solid_state_alias_geometry(
        const kernel::ViewerReferenceGeometry& authored,const kernel::ViewerReferenceGeometry& calculated,
        std::set<HistoryReferenceView::Owner> changed) {
    using Identity=std::tuple<std::string,std::string,std::string>;
    using Source=std::tuple<std::string,std::string,std::string,unsigned>;
    std::map<Source,std::set<Identity>> aliases;
    const auto collect=[&](const auto& ref,unsigned kind) {
        auto owner=ref.owner_id,key=ref.semantic_key;
        bool state=false;
        while(const auto parent=kernel::solid_state_parent(key)){owner=parent->first;key=parent->second;state=true;}
        if(state&&changed.contains({owner,ref.instance_path}))
            aliases[{owner,key,ref.instance_path,kind}].insert({ref.owner_id,ref.semantic_key,ref.instance_path});
    };
    for(const auto& ref:authored.triangle_references)collect(ref,1);
    for(const auto& value:authored.edges)collect(value.reference,2);
    for(const auto& value:authored.points)collect(value.reference,4);
    for(const auto& value:authored.axes)collect(value.reference,8);
    HistoryReferenceView out{std::move(changed),calculated};
    history_reference_detail::retain(out.geometry,out.owners,true);
    if(aliases.empty())return out;
    kernel::ViewerReferenceGeometry projected;projected.vertices=calculated.vertices;
    const auto project=[&](const auto& reference,unsigned kind,const auto& append) {
        const auto found=aliases.find({reference.owner_id,reference.semantic_key,reference.instance_path,kind});
        if(found==aliases.end())return;
        for(const auto& [owner,key,path]:found->second) {
            auto ref=reference;ref.owner_id=owner;ref.semantic_key=key;ref.instance_path=path;
            out.owners.insert({owner,path});append(std::move(ref));
        }
    };
    for(std::size_t i=0;i<calculated.triangle_references.size();++i)
        project(calculated.triangle_references[i],1,[&](auto ref) {
            projected.triangle_references.push_back(std::move(ref));
            projected.triangles.insert(projected.triangles.end(),calculated.triangles.begin()+i*3,calculated.triangles.begin()+i*3+3);
        });
    for(const auto& value:calculated.edges)project(value.reference,2,[&](auto ref){auto item=value;item.reference=std::move(ref);projected.edges.push_back(std::move(item));});
    for(const auto& value:calculated.points)project(value.reference,4,[&](auto ref){auto item=value;item.reference=std::move(ref);projected.points.push_back(std::move(item));});
    for(const auto& value:calculated.axes)project(value.reference,8,[&](auto ref){auto item=value;item.reference=std::move(ref);projected.axes.push_back(std::move(item));});
    history_reference_detail::retain(projected,out.owners,true);
    history_reference_detail::append(out.geometry,projected);return out;
}

// Build an evaluation view from calculated original-source packets. The packet
// is an alias at a history boundary, not newly owned state topology. Persisted
// source references and their side/offset/branch choices are never rewritten.
inline HistoryReferenceView solid_state_reference_view(
        const kernel::ViewerReferenceGeometry& authored,
        const kernel::ViewerReferenceGeometry& calculated,
        std::set<HistoryReferenceView::Owner> owners,
        std::span<const ConstructionReference> required,
        bool preserve_authored_side=false) {
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
    std::map<const kernel::SurfaceGeometry*,
        std::shared_ptr<const kernel::SurfaceGeometry>> oriented_surfaces;
    for(const auto& ref:required) {
        if(!result.owners.contains({ref.owner_id,ref.instance_path}))continue;
        const Key key{ref.owner_id,ref.instance_path,ref.semantic_key};
        const auto source=before.find(key),target=after.find(key);
        if(source==before.end() || target==after.end() || source->second!=target->second ||
           (source->second&(source->second-1))!=0)
            throw std::invalid_argument("A solid state reference has no unique correspondence.");
        if(preserve_authored_side&&source->second==1) {
            // Straightening and restoring are orientation-preserving material
            // maps. A later replayed Boolean may reverse the OCCT boundary
            // that inherited this oldest face identity, but it must not flip
            // the persisted semantic side used to place that Boolean itself.
            const auto matches=[&](const auto& face) {
                return face.owner_id==ref.owner_id&&
                    face.instance_path==ref.instance_path&&
                    face.semantic_key==ref.semantic_key;
            };
            const auto original=std::ranges::find_if(
                authored.triangle_references,matches);
            if(original==authored.triangle_references.end()||
                    !original->surface)continue;
            for(auto& face:result.geometry.triangle_references)
                if(matches(face)&&face.surface&&
                        face.surface->reversed!=original->surface->reversed) {
                    const auto* identity=face.surface.get();
                    auto replacement=oriented_surfaces.find(identity);
                    if(replacement==oriented_surfaces.end()) {
                        auto geometry=std::make_shared<kernel::SurfaceGeometry>(
                            *face.surface);
                        geometry->reversed=original->surface->reversed;
                        replacement=oriented_surfaces.emplace(identity,
                            std::move(geometry)).first;
                    }
                    face.surface=replacement->second;
                }
        }
    }
    return result;
}
} // namespace zima::document
