#include <zima/workspace/drill_point_operations.hpp>
#include <zima/kernel/drill_point_identity.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace zima::workspace {
bool commit_drill_point(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,DrillPointEditMode mode) {
    auto* state=live.open_part(id);
    if(!state)throw DrillPointOperationError("unsupported_document","Drill points require an open Part.");
    if(feature.feature_kind!=document::FeatureKind::DrillPoint||feature.combine_mode!=document::CombineMode::Subtract)
        throw DrillPointOperationError("wrong_feature","This container is not a drill point.");
    const auto& before=state->session.document();const auto* stored=before.find_container(feature.id);
    if(mode==DrillPointEditMode::Replace) {
        if(!stored)throw DrillPointOperationError("container_not_found","The requested container does not exist.");
        if(stored->feature_kind!=feature.feature_kind)throw DrillPointOperationError("wrong_feature","This container is not a drill point.");
        if(stored->feature_id!=feature.feature_id||stored->feature_parent_id!=feature.feature_parent_id||stored->container_origin!=feature.container_origin)
            throw DrillPointOperationError("identity_changed","Editing must preserve the container identity.");
        if(stored->value_locks.contains("angle")&&feature.value_locks.contains("angle")&&
            stored->drill_point.included_angle_degrees!=feature.drill_point.included_angle_degrees)
            throw DrillPointOperationError("value_locked","Unlock the dimension before changing it.");
    }else if(stored||feature.id.empty()||feature.feature_id.empty())
        throw DrillPointOperationError("identity_changed","A new container must have a new nonempty identity.");
    const auto* body=stored?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw DrillPointOperationError("read_only_body","A derived Body cannot be edited directly.");
    const double angle=feature.drill_point.included_angle_degrees;
    if(!std::isfinite(angle)||angle<1||angle>179)
        throw DrillPointOperationError("invalid_arguments","The drill-point angle must be between 1 and 179 degrees.");
    if(!stored&&feature.drill_point.bottom_faces.empty())
        throw DrillPointOperationError("missing_reference","Select at least one circular hole bottom.");
    const auto& previous=state->session.calculated_boundaries();
    if(previous.empty())throw DrillPointOperationError("missing_input","Drill points require a calculated input body.");
    const auto operations=before.kernel_operations();
    const auto limit=stored?static_cast<std::size_t>(std::distance(operations.begin(),std::ranges::find_if(operations,
        [&](const auto& operation){return operation.owner_id==feature.id;}))):before.body_operation_count_at_history_cursor();
    std::set<std::string> owners;
    for(std::size_t i=0;i<std::min(limit,operations.size());++i)owners.insert(operations[i].owner_id);
    const auto& references=previous.back().mesh.original_references.triangle_references;
    std::set<std::pair<std::string,std::string>> unique;
    for(const auto& face:feature.drill_point.bottom_faces) {
        if(!face.valid()||!face.instance_path.empty()||!owners.contains(face.owner_id)||
            std::ranges::find(references,face)==references.end())
            throw DrillPointOperationError("invalid_reference","Select an available original hole bottom before this feature.");
        if(!unique.emplace(face.owner_id,face.semantic_key).second)
            throw DrillPointOperationError("invalid_reference","A drill-point bottom may be selected only once.");
    }
    if(stored&&*stored==feature)return false;
    const auto feature_id=feature.id;const auto faces=feature.drill_point.bottom_faces;
    auto next=before;
    if(stored)*next.find_container(feature.id)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(std::move(feature));}
    auto geometry=construction_reference_source_geometry(previous);
    append_reference_geometry(geometry,next.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(geometry);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(stored){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(feature_id);}
    auto calculated=calculate_part(kernel,next,&previous,policy);
    // An explicit edit must not silently skip an invalid selected bottom.
    if(!faces.empty()) {
        // Original packets in the final boundary retain this feature's own
        // calculated faces even when a later operation trims the result.
        const auto& produced=calculated.back().mesh.original_references.triangle_references;
        for(const auto& face:faces)if(std::ranges::none_of(produced,[&](const auto& ref){return ref.owner_id==feature_id&&ref.semantic_key==kernel::drill_point_key("side",face);}))
            throw DrillPointOperationError("invalid_reference","Each selected face must be a valid circular hole bottom.");
    }
    static_cast<void>(refresh_sketch_external_references(next,calculated));
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
