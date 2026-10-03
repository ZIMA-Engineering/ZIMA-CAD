#pragma once
#include <zima/document/part_document.hpp>
#include <zima/kernel/solid_state_ancestry.hpp>
#include <zima/kernel/solid_state_history.hpp>
#include <zima/kernel/sheet_material.hpp>

namespace zima::document {
// Explicit calculation only. Validate against persisted viewer data; transport
// comes from the source trajectory. Authored placement and references stay intact.
inline std::vector<kernel::SolidStateFaceTransfer> solid_state_face_transfers(
        const PartDocument& document,const BodyHistory& body,const std::string& boundary,
        const kernel::ViewerReferenceGeometry& authored,
        const std::vector<kernel::HistoryOperation>& operations) {
    using namespace kernel::sheet_material;
    const auto unsupported=[](){throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");};
    const auto ancestor=[&](const ConstructionReference& ref) {
        auto owner=ref.owner_id,key=ref.semantic_key;
        std::set<std::string> visited;
        while(const auto* feature=document.find_container(owner)) {
            if(!is_solid_state(feature->feature_kind))break;
            const auto parent=kernel::solid_state_parent(key);
            if(!parent||!visited.insert(owner).second)unsupported();
            owner=parent->first;key=parent->second;
        }
        return std::pair{owner,key};
    };
    const auto matches=[](const auto& candidate,const ConstructionReference& ref) {
        return candidate.owner_id==ref.owner_id&&candidate.semantic_key==ref.semantic_key&&candidate.instance_path==ref.instance_path;
    };
    const auto face=[&](const auto& geometry,const ConstructionReference& ref)->const kernel::FaceReference* {
        const auto found=std::ranges::find_if(geometry.triangle_references,[&](const auto& f){return matches(f,ref);});
        return found==geometry.triangle_references.end()?nullptr:&*found;
    };
    // Compare each attachment's creation state with this replay boundary.
    // Selecting another independent source must not constrain an unchanged join.
    std::vector<kernel::HistoryOperation> sources;
    std::vector<kernel::SolidStateChange> changes;
    std::map<std::string,std::size_t> indices;
    for(const auto& operation:operations) {
        if(operation.body.id!=body.scope.id)continue;
        if(const auto* state=std::get_if<kernel::SolidStateRequest>(&operation.primitive))
            changes.push_back({sources.size(),operation.owner_id,operation.body.id,
                state->restore,state->all,operation.suppressed,state->coefficient,state->owners});
        else {indices.emplace(operation.owner_id,sources.size());sources.push_back(operation);}
        if(operation.owner_id==boundary)break;
    }
    const auto target=kernel::solid_states_before(sources,changes,sources.size());
    std::vector<kernel::SolidStateFaceTransfer> result;
    std::set<std::string> moved;
    for(const auto& entry:body.entries) {
        if(entry.id==boundary)break;
        const auto* feature=document.find_container(entry.id);
        const auto* construction=document.find_construction(entry.id);
        const auto* refs=feature&&!feature->suppressed?&feature->placement.references:
            construction&&!construction->suppressed?&construction->references:nullptr;
        if(!refs||(feature&&is_solid_state(feature->feature_kind)))continue;
        // A descendant follows the same end-section contract as the first join.
        // Origin-only and mixed-parent attachments still fail the face checks.
        const bool descendant=std::ranges::any_of(*refs,[&](const auto& ref){return moved.contains(ref.owner_id);});
        const auto index=indices.find(entry.id);
        // A positional end face defines the material attachment. Carry the
        // whole frame, including any numerical in-plane coordinates, and let
        // descendants inherit its movement even when extra points/edges fully
        // constrain this first join. A point with an orientation-only face is
        // a different placement and retains its existing reference evaluation.
        const auto source_face=std::ranges::find_if(*refs,[&](const auto& ref) {
            return !ref.orientation_only&&target.contains(ref.owner_id)&&face(authored,ref);
        });
        const bool material_frame_source=source_face!=refs->end()&&
            std::ranges::all_of(*refs,[&](const auto& ref) {
                return ref.owner_id==source_face->owner_id&&ref.instance_path==source_face->instance_path;
            });
        std::optional<kernel::SolidSourceStates> original;
        bool affected=descendant;
        for(const auto& ref:*refs) {
            const auto* source=document.find_container(ref.owner_id);
            if((!source||!is_solid_state(source->feature_kind))&&!material_frame_source)continue;
            const auto [owner,key]=ancestor(ref);
            if(index==indices.end()){affected=true;break;}
            if(!original)original=kernel::solid_states_before(sources,changes,index->second);
            const auto before=original->find(owner);
            const auto after=target.find(owner);
            if(before==original->end()||after==target.end()||before->second.straight!=after->second.straight||
                (before->second.straight&&before->second.coefficient!=after->second.coefficient))affected=true;
        }
        if(!affected)continue;
        bool state_reference=false;
        const ConstructionReference* selected=nullptr;
        const kernel::FaceReference* before=nullptr;
        for(const auto& ref:*refs) {
            const auto* owner=document.find_container(ref.owner_id);
            if((!owner||!is_solid_state(owner->feature_kind))&&!moved.contains(ref.owner_id)&&!material_frame_source)continue;
            state_reference=true;
            if(const auto* candidate=face(authored,ref);candidate&&candidate->surface&&
                candidate->surface->kind==kernel::SurfaceGeometry::Kind::Plane) {
                if(selected&&!matches(*candidate,*selected))unsupported();
                selected=&ref;before=candidate;
            }
        }
        if(!state_reference)continue;
        if(!feature||!selected||!feature->placement.value_locks.empty())unsupported();
        const auto [owner,key]=ancestor(*selected);
        // A reversed feature retains its authored start/end side identities.
        // Either semantic cap can be the physical continuation end; the kernel
        // independently checks the selected attachment against that exact end.
        if(!key.starts_with("end:from:")&&!key.starts_with("start:from:")&&
            !key.starts_with("sweep:cap:end:from:")&&!key.starts_with("sweep:cap:start:from:"))unsupported();
        const auto on_plane=[&](kernel::Vec3 p) {
            return std::abs(dot(sub(p,before->surface->origin),before->surface->axis))<=1e-7;
        };
        bool position=false,orientation=false;
        for(const auto& ref:*refs) {
            if(ref.owner_id!=selected->owner_id||ref.instance_path!=selected->instance_path||ref.use_axis||ref.solution_branch)unsupported();
            if(ref.semantic_key!=selected->semantic_key) {
                const auto point=std::ranges::find_if(authored.points,[&](const auto& p){return matches(p.reference,ref);});
                if(point!=authored.points.end()) {if(!on_plane(point->position))unsupported();}
                else {
                    const auto edge=std::ranges::find_if(authored.edges,[&](const auto& e){return matches(e.reference,ref);});
                    if(edge==authored.edges.end()||edge->points.size()<2)unsupported();
                    const auto first=edge->points.front(),delta=sub(edge->points.back(),first);
                    const double length=std::sqrt(dot(delta,delta));if(length<1e-7)unsupported();
                    const auto axis=mul(delta,1/length);
                    const auto straight=[&](const auto& points) {
                        double previous=-1e-7;
                        for(const auto p:points) {
                            const auto d=sub(p,first),normal=cross(d,axis);const double along=dot(d,axis);
                            if(!on_plane(p)||dot(normal,normal)>1e-14||along<previous-1e-7||along>length+1e-7)return false;
                            previous=along;
                        }
                        return true;
                    };
                    if(!straight(edge->points))unsupported();
                    // A persisted degree-one spline is also a straight edge.
                    // Checking every pole prevents accepting a curved edge
                    // merely because its display polyline has two endpoints.
                    if(edge->exact_spline) {
                        edge->exact_spline->validate();
                        if(!straight(edge->exact_spline->poles))unsupported();
                    }
                }
                if(ancestor(ref).first!=owner)unsupported();
            }
            position|=!ref.orientation_only;orientation|=ref.orientation_drives_rotation;
        }
        if(!position||!orientation)unsupported();
        result.push_back({feature->id,owner});moved.insert(feature->id);
        if(!feature->container_origin.id.empty())moved.insert(feature->container_origin.id);
    }
    return result;
}
} // namespace zima::document
