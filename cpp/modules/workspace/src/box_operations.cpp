#include <zima/workspace/box_operations.hpp>
#include <cmath>

namespace zima::workspace {
namespace {
void validate_dimensions(const document::BoxParameters& box) {
    for(const double value : {box.length, box.width, box.height})
        if(!std::isfinite(value) || value < 0.001 || value > 1000000.0)
            throw BoxOperationError("invalid_arguments", "Box dimensions must be between 0.001 and 1000000 mm.");
}

}
bool commit_box(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, document::HistoryContainer committed, BoxEditMode mode) {
    auto* state = workspace.open_part(document_id);
    if(!state) throw BoxOperationError("unsupported_document", "Box operations require an open Part.");
    if(committed.feature_kind != document::FeatureKind::Box)
        throw BoxOperationError("wrong_feature", "The selected container is not a Box.");
    validate_dimensions(committed.box);
    const auto& before = state->session.document();
    const auto* existing = before.find_container(committed.id);
    PartCalculationPolicy policy;
    policy.reject_errors = true;
    if(mode == BoxEditMode::Replace) {
        if(!existing) throw BoxOperationError("container_not_found", "The requested container does not exist.");
        if(existing->feature_kind != document::FeatureKind::Box)
            throw BoxOperationError("wrong_feature", "The selected container is not a Box.");
        if(existing->feature_id != committed.feature_id || existing->feature_parent_id != committed.feature_parent_id ||
            existing->container_origin != committed.container_origin)
            throw BoxOperationError("identity_changed", "Editing must preserve the container identity.");
        if(*existing == committed) return false;
        policy.edited_document_id = document_id;
        policy.edited_history_limit = before.history_index(committed.id);
    } else if(existing || committed.id.empty() || committed.feature_id.empty()) {
        throw BoxOperationError("identity_changed", "A new container must have a new nonempty identity.");
    }
    const auto* body = mode == BoxEditMode::Replace ? before.body_owner_for_object(committed.id)
        : before.body_history.find(before.body_history.active_body_id());
    if(body && body->derived_copy)
        throw BoxOperationError("read_only_body", "A derived Body cannot be edited directly.");
    auto next = before;
    if(mode == BoxEditMode::Replace) *next.find_container(committed.id) = std::move(committed);
    else {
        next.insert_history_entry(document::PartHistoryKind::Feature, committed.id);
        next.history.push_back(std::move(committed));
    }
    // Same calculation boundary and reference resolution as primitive properties.
    // Do not change placement semantics or recalculate dependent Assemblies here.
    const auto& calculated_before = state->session.calculated_boundaries();
    auto references = construction_reference_source_geometry(calculated_before);
    append_reference_geometry(references, next.origin_viewer_mesh().original_references);
    append_reference_geometry(references, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    auto calculated = calculate_part(kernel, next, &calculated_before, policy);
    static_cast<void>(refresh_sketch_external_references(next, calculated));
    state->session.commit(std::move(next), std::move(calculated));
    return true;
}
bool set_box_dimensions(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, const std::string& container_id, const BoxDimensionPatch& patch) {
    const auto* state = workspace.open_part(document_id);
    if(!state) throw BoxOperationError("unsupported_document", "Box operations require an open Part.");
    const auto* existing = state->session.document().find_container(container_id);
    if(!existing) throw BoxOperationError("container_not_found", "The requested container does not exist.");
    if(existing->feature_kind != document::FeatureKind::Box)
        throw BoxOperationError("wrong_feature", "The selected container is not a Box.");
    if(!patch.length && !patch.width && !patch.height)
        throw BoxOperationError("invalid_arguments", "Specify at least one Box dimension.");
    auto next = *existing;
    const auto update = [&](const char* key, double& target, const std::optional<double>& value) {
        if(!value) return;
        if(*value != target && existing->value_locks.contains(key))
            throw BoxOperationError("value_locked", "Unlock the dimension before changing it.");
        target = *value;
    };
    update("length", next.box.length, patch.length);
    update("width", next.box.width, patch.width);
    update("height", next.box.height, patch.height);
    return commit_box(workspace,kernel,document_id,std::move(next),BoxEditMode::Replace);
}
} // namespace zima::workspace
