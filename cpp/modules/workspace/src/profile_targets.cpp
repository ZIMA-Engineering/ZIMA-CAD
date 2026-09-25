#include <zima/workspace/profile_operations.hpp>
#include <zima/document/profile_targets.hpp>
#include <algorithm>

namespace zima::workspace {
document::ExtrusionParameters::EndTarget resolve_profile_end_target(
    const document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated,
    const std::string& frame_owner,const document::ExtrusionParameters::EndTarget& requested) {
    if(!requested.reference.valid() || !requested.reference.instance_path.empty() || requested.kind==document::EndTargetKind::Point)
        throw ProfileOperationError("invalid_reference","The extrusion target must belong to this Part.");
    kernel::ViewerReferenceGeometry selected;
    const auto collect=[&](const kernel::ViewerReferenceGeometry& source) {
        if(!selected.triangle_references.empty())return;
        for(std::size_t i=0;i<source.triangle_references.size();++i) {
            const auto& ref=source.triangle_references[i];if(ref.owner_id!=requested.reference.owner_id || ref.semantic_key!=requested.reference.semantic_key || ref.instance_path!=requested.reference.instance_path)continue;
            if(i*3+2>=source.triangles.size())throw ProfileOperationError("invalid_reference","The persisted target face geometry is incomplete.");
            for(std::size_t j=0;j<3;++j) {
                const auto index=source.triangles[i*3+j];if(index>=source.vertices.size())throw ProfileOperationError("invalid_reference","The persisted target face geometry is incomplete.");
                selected.triangles.push_back(static_cast<std::uint32_t>(selected.vertices.size()));selected.vertices.push_back(source.vertices[index]);
            }
            selected.triangle_references.push_back(ref);
        }
    };
    if(!calculated.empty())collect(calculated.back().mesh.original_references);
    if(selected.triangle_references.empty())collect(part.origin_viewer_mesh().original_references);
    if(selected.triangle_references.empty())collect(part.body_origin_reference_geometry());
    if(selected.triangle_references.empty())collect(part.history_origin_reference_geometry_before(frame_owner));
    if(selected.triangle_references.empty())collect(part.construction_viewer_mesh().original_references);
    selected=part.construction_reference_geometry_for(frame_owner,std::move(selected));
    const auto result=document::resolve_profile_target(requested,selected);
    if(!result)throw ProfileOperationError("missing_reference","The original extrusion target is unavailable.");
    return *result;
}
document::ExtrusionParameters::EndTarget prepare_profile_end_target(
    const document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated,
    const document::HistoryContainer& feature,const document::ExtrusionParameters::EndTarget& requested) {
    if((feature.feature_kind!=document::FeatureKind::Extrusion && feature.feature_kind!=document::FeatureKind::Feature) || requested.kind==document::EndTargetKind::Point)
        throw ProfileOperationError("invalid_reference","Extrusion end references require a plane or an original face.");
    if(!requested.reference.valid() || !requested.reference.instance_path.empty())
        throw ProfileOperationError("invalid_reference","The extrusion target must belong to this Part.");
    auto allowed=sketch_external_reference_source_owners(part,
        feature.feature_kind==document::FeatureKind::Feature ? feature.feature.sketch_id : feature.extrusion.sketch_id);
    allowed.insert(part.document_id+":origin");
    const auto* target_body=part.body_owner_for_object(feature.id);
    for(const auto& body:part.body_history.bodies()) {
        allowed.insert(body.origin().id);
        if(target_body && body.scope.id==target_body->scope.id)break;
    }
    if(!allowed.contains(requested.reference.owner_id))
        throw ProfileOperationError("invalid_reference_source","The extrusion target must be an earlier object or an available Origin.");
    return resolve_profile_end_target(part,calculated,feature.id,requested);
}

bool refresh_profile_end_targets(document::PartDocument& part,const std::vector<kernel::BodyResult>& calculated) {
    bool changed=false;
    for(auto& feature:part.history) {
        if(feature.feature_kind!=document::FeatureKind::Extrusion && feature.feature_kind!=document::FeatureKind::Feature)continue;
        const auto refresh=[&](auto& targets) {
            for(auto& target:targets)try {
                const auto next=prepare_profile_end_target(part,calculated,feature,target);
                if(next!=target){target=next;changed=true;}
            }catch(const ProfileOperationError&) { /* The original ID and last geometry stay available for repair. */ }
        };
        if(feature.feature_kind==document::FeatureKind::Feature) {
            for(auto& side:feature.feature.sides)refresh(side.targets);
        } else {
            refresh(feature.extrusion.end_targets_forward);refresh(feature.extrusion.end_targets_reverse);
        }
    }
    return changed;
}
} // namespace zima::workspace
