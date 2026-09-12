#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <cmath>
#include <algorithm>

namespace zima::workspace {
namespace {
void require_opening(const document::HistoryContainer& value) {
    if(value.feature_kind!=document::FeatureKind::Thread)
        throw OpeningOperationError("wrong_feature","This container is not an opening.");
}
void validate(const document::HistoryContainer& value) {
    require_opening(value);
    for(const auto& [name,number]:opening_dimensions(value)) {
        const bool angle=name=="chamfer_angle"||name=="drill_point_angle";
        const bool runout=name=="runout_pitch_factor";
        if(!std::isfinite(number)||number<(angle?1.0:runout?0.0:0.001)||number>(angle?179.0:runout?100.0:1000000.0))
            throw OpeningOperationError("invalid_arguments","Opening dimension is outside the supported range.");
    }
    if(value.combine_mode!=document::CombineMode::Subtract || value.thread.side!=document::ThreadSide::Internal ||
        value.thread.extent_mode!=document::ProfileExtentMode::OneSide)
        throw OpeningOperationError("invalid_arguments","An opening removes material in one direction.");
    if(value.thread.enabled&&value.thread.profile_diameter>=value.thread.nominal_diameter)
        throw OpeningOperationError("invalid_arguments","The bore diameter must be smaller than the nominal thread diameter.");
    if(value.thread.end_condition_forward!=document::EndCondition::Length && value.hole.drill_point_enabled)
        throw OpeningOperationError("invalid_arguments","A drill point requires a blind opening.");
}
}
std::vector<std::pair<std::string,double>> opening_dimensions(const document::HistoryContainer& value) {
    require_opening(value);const auto& p=value.thread;
    return {{"nominal_diameter",p.nominal_diameter},{"pitch",p.pitch},{"bore_diameter",p.profile_diameter},
        {"bore_length",p.bore_length},{"thread_length",p.length_forward},{"chamfer_depth",p.chamfer_depth},
        {"chamfer_angle",p.chamfer_angle_degrees},{"drill_point_angle",value.hole.drill_point_angle_degrees},
        {"runout_pitch_factor",p.runout_pitch_factor}};
}
bool commit_opening(Workspace& workspace,const kernel::OcctKernel& kernel,const std::string& document_id,
    document::HistoryContainer committed,OpeningEditMode mode) {
    auto* state=workspace.open_part(document_id);
    if(!state)throw OpeningOperationError("unsupported_document","Opening operations require an open Part.");
    normalize_owned_profile_front_references(committed.placement.references);
    validate(committed);
    const auto& before=state->session.document();const auto* existing=before.find_container(committed.id);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(mode==OpeningEditMode::Replace) {
        if(!existing)throw OpeningOperationError("container_not_found","The requested container does not exist.");
        require_opening(*existing);
        if(existing->feature_id!=committed.feature_id||existing->feature_parent_id!=committed.feature_parent_id||existing->container_origin!=committed.container_origin)
            throw OpeningOperationError("identity_changed","Editing must preserve the container identity.");
        const auto& old=existing->hole;const auto& now=committed.hole;
        if(old.sketch_id!=now.sketch_id||old.circle_id!=now.circle_id||old.chamfer_sketch_id!=now.chamfer_sketch_id||
            old.tip_sketch_id!=now.tip_sketch_id||old.chamfer_point_ids!=now.chamfer_point_ids||old.tip_point_ids!=now.tip_point_ids||
            old.chamfer_axis_segment_id!=now.chamfer_axis_segment_id||old.tip_axis_segment_id!=now.tip_axis_segment_id)
            throw OpeningOperationError("identity_changed","Editing must preserve the opening profile identities.");
        if(*existing==committed)return false;
        policy.edited_document_id=document_id;policy.edited_history_limit=before.history_index(committed.id);
    } else if(existing||committed.id.empty()||committed.feature_id.empty())
        throw OpeningOperationError("identity_changed","A new container must have a new nonempty identity.");
    const auto* body=existing?before.body_owner_for_object(committed.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw OpeningOperationError("read_only_body","A derived Body cannot be edited directly.");
    auto next=before;
    if(existing)*next.find_container(committed.id)=std::move(committed);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,committed.id);next.history.push_back(std::move(committed));}
    // Same placement resolution and explicit calculation as current Otvor OK.
    const auto& previous=state->session.calculated_boundaries();
    auto references=construction_reference_source_geometry(previous);
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    auto calculated=calculate_part(kernel,next,&previous,policy);
    static_cast<void>(refresh_sketch_external_references(next,calculated));
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
