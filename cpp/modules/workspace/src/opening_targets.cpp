#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/profile_operations.hpp>

namespace zima::workspace {
document::ExtrusionParameters::EndTarget prepare_opening_end_target(
    const document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated,
    const document::HistoryContainer& feature,const document::ExtrusionParameters::EndTarget& requested,bool thread_end) {
    if(feature.feature_kind!=document::FeatureKind::Thread)
        throw OpeningOperationError("wrong_feature","This container is not an opening.");
    if(!requested.reference.valid()||!requested.reference.instance_path.empty()||requested.kind==document::EndTargetKind::Point)
        throw OpeningOperationError("invalid_reference","An opening end requires an original face or plane of this Part.");
    auto allowed=sketch_external_reference_source_owners(part,feature.hole.sketch_id);
    allowed.insert(part.document_id+":origin");
    const auto* target_body=part.body_owner_for_object(feature.id);
    for(const auto& body:part.body_history.bodies()) {
        allowed.insert(body.origin().id);
        if(target_body&&body.scope.id==target_body->scope.id)break;
    }
    if(!allowed.contains(requested.reference.owner_id))
        throw OpeningOperationError("invalid_reference_source","The opening target must be an earlier object or an available Origin.");
    try {
        auto target=resolve_profile_end_target(part,calculated,feature.id,requested);
        if(thread_end&&target.kind!=document::EndTargetKind::Plane)
            throw OpeningOperationError("invalid_reference","The thread length target must be a plane or a planar original face.");
        return target;
    }catch(const ProfileOperationError& error) {
        throw OpeningOperationError(error.code,"The original opening end reference is unavailable.");
    }
}
bool refresh_opening_end_targets(document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated) {
    bool changed=false;
    for(auto& feature:part.history) {
        if(feature.feature_kind!=document::FeatureKind::Thread)continue;
        const auto refresh=[&](auto& targets,bool thread_end) {
            for(auto& target:targets)try {
                const auto next=prepare_opening_end_target(part,calculated,feature,target,thread_end);
                if(next!=target){target=next;changed=true;}
            }catch(const OpeningOperationError&) { /* Preserve the last target geometry and identity for repair. */ }
        };
        refresh(feature.thread.end_targets_forward,false);refresh(feature.thread.length_end_targets,true);
    }
    return changed;
}
} // namespace zima::workspace
