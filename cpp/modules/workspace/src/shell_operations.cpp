#include <zima/workspace/shell_operations.hpp>
#include <zima/workspace/operation_input.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace zima::workspace {
std::vector<kernel::FaceReference> shell_input_faces(const PartState& state,const std::string& id) {
    const auto* input=calculated_operation_input(state.session,id);
    if(!input||input->mesh.triangles.empty())
        throw ShellOperationError("missing_input","Shell requires a calculated input body.");
    std::set<std::pair<std::string,std::string>> unique;std::vector<kernel::FaceReference> result;
    for(const auto& face:input->mesh.triangle_references)
        if(face.valid()&&face.instance_path.empty()&&unique.emplace(face.owner_id,face.semantic_key).second)
            result.push_back(face);
    return result;
}
bool commit_shell(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    document::HistoryContainer feature,ShellEditMode mode) {
    auto* state=live.open_part(id);
    if(!state)throw ShellOperationError("unsupported_document","Shell operations require an open Part.");
    if(feature.feature_kind!=document::FeatureKind::Shell||feature.combine_mode!=document::CombineMode::Add)
        throw ShellOperationError("wrong_feature","This container is not a Shell.");
    const auto& before=state->session.document();const auto* stored=before.find_container(feature.id);
    if(mode==ShellEditMode::Replace) {
        if(!stored)throw ShellOperationError("container_not_found","The requested container does not exist.");
        if(stored->feature_kind!=feature.feature_kind)throw ShellOperationError("wrong_feature","This container is not a Shell.");
        if(stored->feature_id!=feature.feature_id||stored->feature_parent_id!=feature.feature_parent_id||stored->container_origin!=feature.container_origin)
            throw ShellOperationError("identity_changed","Editing must preserve the container identity.");
        if(stored->value_locks.contains("thickness")&&feature.value_locks.contains("thickness")&&stored->shell.thickness!=feature.shell.thickness)
            throw ShellOperationError("value_locked","Unlock the dimension before changing it.");
    }else if(stored||feature.id.empty()||feature.feature_id.empty())
        throw ShellOperationError("identity_changed","A new container must have a new nonempty identity.");
    const auto* body=stored?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw ShellOperationError("read_only_body","A derived Body cannot be edited directly.");
    if(!std::isfinite(feature.shell.thickness)||feature.shell.thickness<.001||feature.shell.thickness>1e6)
        throw ShellOperationError("invalid_arguments","Shell thickness must be between 0.001 and 1000000 mm.");
    const auto available=shell_input_faces(*state,stored?feature.id:std::string{});
    std::set<std::pair<std::string,std::string>> unique;
    for(const auto& face:feature.shell.removed_faces) {
        if(!face.valid()||!face.instance_path.empty()||std::ranges::find(available,face)==available.end())
            throw ShellOperationError("invalid_reference","Select an available face of this Shell input body.");
        if(!unique.emplace(face.owner_id,face.semantic_key).second)
            throw ShellOperationError("invalid_reference","A Shell opening face may be selected only once.");
    }
    if(stored&&*stored==feature)return false;
    const auto feature_id=feature.id;auto next=before;
    if(stored)*next.find_container(feature.id)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(std::move(feature));}
    const auto& previous=state->session.calculated_boundaries();
    // Preserve the existing Properties reference-resolution sequence. Shell
    // has no independent placement editor and does not change that contract.
    auto references=construction_reference_source_geometry(previous);
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(stored){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(feature_id);}
    auto calculated=calculate_part(kernel,next,&previous,policy);
    static_cast<void>(refresh_sketch_external_references(next,calculated));
    state->session.commit(std::move(next),std::move(calculated));return true;
}
} // namespace zima::workspace
