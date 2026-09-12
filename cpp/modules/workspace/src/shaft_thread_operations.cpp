#include <zima/workspace/shaft_thread_operations.hpp>
#include <zima/kernel/shaft_thread_geometry.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
kernel::ViewerReferenceGeometry shaft_thread_input_references(const PartState& state,const std::string& id) {
    const auto& part=state.session.document();const auto& calculated=state.session.calculated_boundaries();
    if(calculated.empty())throw ShaftThreadOperationError("missing_input","Shaft threading requires a calculated input body.");
    auto references=calculated.back().mesh.original_references;
    const auto operations=part.kernel_operations();
    const auto limit=id.empty()?part.body_operation_count_at_history_cursor():static_cast<std::size_t>(std::distance(operations.begin(),
        std::ranges::find_if(operations,[&](const auto& operation){return operation.owner_id==id;})));
    std::set<std::string> owners;
    for(std::size_t i=0;i<std::min(limit,operations.size());++i)owners.insert(operations[i].owner_id);
    for(auto& reference:references.triangle_references)if(!owners.contains(reference.owner_id))reference={};
    return references;
}
bool commit_shaft_thread(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,ShaftThreadEditMode mode) {
    auto* state=live.open_part(id);
    if(!state)throw ShaftThreadOperationError("unsupported_document","Shaft threading requires an open Part.");
    if(feature.feature_kind!=document::FeatureKind::ShaftThread)
        throw ShaftThreadOperationError("wrong_feature","This container is not a shaft thread.");
    const auto& before=state->session.document();const auto* stored=before.find_container(feature.id);
    if(mode==ShaftThreadEditMode::Replace) {
        if(!stored)throw ShaftThreadOperationError("container_not_found","The requested container does not exist.");
        if(stored->feature_kind!=feature.feature_kind)throw ShaftThreadOperationError("wrong_feature","This container is not a shaft thread.");
        if(stored->feature_id!=feature.feature_id||stored->feature_parent_id!=feature.feature_parent_id||stored->container_origin!=feature.container_origin)
            throw ShaftThreadOperationError("identity_changed","Editing must preserve the container identity.");
    } else if(stored||feature.id.empty()||feature.feature_id.empty())
        throw ShaftThreadOperationError("identity_changed","A new container must have a new nonempty identity.");
    if(stored) {
        const auto locked=[&](const char* key,double old_value,double new_value) {
            return stored->value_locks.contains(key)&&feature.value_locks.contains(key)&&old_value!=new_value;
        };
        const auto& old=stored->shaft_thread;const auto& now=feature.shaft_thread;
        if(locked("root_diameter",old.root_diameter,now.root_diameter)||locked("length",old.length,now.length)||
            locked("runout_pitch_factor",old.runout_pitch_factor,now.runout_pitch_factor))
            throw ShaftThreadOperationError("value_locked","Unlock the dimension before changing it.");
    }
    const auto* body=stored?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw ShaftThreadOperationError("read_only_body","A derived Body cannot be edited directly.");
    const auto& p=feature.shaft_thread;
    for(const auto number:{p.nominal_diameter,p.root_diameter,p.pitch,p.length})
        if(!std::isfinite(number)||number<.001||number>1000000)
            throw ShaftThreadOperationError("invalid_arguments","Shaft thread dimensions must be between 0.001 and 1000000 mm.");
    if(!std::isfinite(p.runout_pitch_factor)||p.runout_pitch_factor<0||p.runout_pitch_factor>1000000)
        throw ShaftThreadOperationError("invalid_arguments","The shaft thread runout factor is outside the supported range.");
    const auto references=shaft_thread_input_references(*state,mode==ShaftThreadEditMode::Create?std::string{}:feature.id);
    // Properties clears missing original selections before accepting an edit.
    // Explicit commands use the same requirement, without fallback geometry.
    const auto bind=[&](kernel::FaceReference& ref) {
        if(!ref.valid()||!ref.instance_path.empty())throw ShaftThreadOperationError("invalid_reference","Select an original face of this Part for the shaft thread.");
        const auto found=std::ranges::find_if(references.triangle_references,[&](const auto& source){return source==ref&&source.surface;});
        if(found==references.triangle_references.end())throw ShaftThreadOperationError("missing_reference","The original shaft thread reference is unavailable before this feature.");
        ref=*found;
    };
    bind(feature.shaft_thread.cylinder);bind(feature.shaft_thread.start);
    if(feature.shaft_thread.chamfer)bind(*feature.shaft_thread.chamfer);
    if(feature.shaft_thread.end_condition==document::EndCondition::UpTo) {
        if(!feature.shaft_thread.end)throw ShaftThreadOperationError("missing_reference","Select a shaft thread end face.");
        bind(*feature.shaft_thread.end);
    }
    static_cast<void>(kernel::resolve_shaft_thread(document::PartDocument::shaft_thread_request(feature,&references)));
    if(stored&&*stored==feature)return false;
    auto next=before;
    if(stored)*next.find_container(feature.id)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(std::move(feature));}
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(stored){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(stored->id);}
    auto calculated=calculate_part(kernel,next,&state->session.calculated_boundaries(),policy);
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
