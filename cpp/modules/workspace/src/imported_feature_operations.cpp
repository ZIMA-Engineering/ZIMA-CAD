#include <zima/workspace/imported_feature_operations.hpp>
#include <zima/workspace/feature_reference_input.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cctype>

namespace zima::workspace {
const document::HistoryContainer& imported_feature(
    const Workspace& live, const std::string& id, const std::string& container) {
    const auto* state = live.open_part(id);
    if (!state) throw ImportOperationError("unsupported_document", "Imported feature properties require an open Part.");
    const auto* value = state->session.document().find_container(container);
    if (!value) throw ImportOperationError("container_not_found", "The requested container does not exist.");
    if (value->feature_kind != document::FeatureKind::ImportedStep)
        throw ImportOperationError("wrong_feature", "This container is not an imported body.");
    return *value;
}
bool commit_imported_feature(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, document::HistoryContainer value) {
    const auto& original = imported_feature(live, id, value.id);
    if (value.feature_kind != original.feature_kind ||
        value.feature_id != original.feature_id || value.feature_parent_id != original.feature_parent_id ||
        value.container_origin != original.container_origin)
        throw ImportOperationError("identity_changed", "Editing must preserve the container identity.");
    if (value.imported_step != original.imported_step)
        throw ImportOperationError("immutable_source", "Imported geometry and its original identities cannot be changed by a property edit.");
    try { document::validate_native_metadata_text(value.name); }
    catch (const std::exception& error) { throw ImportOperationError("invalid_arguments", error.what()); }
    if (value.name.empty() || std::ranges::all_of(value.name, [](unsigned char c) { return std::isspace(c) != 0; }))
        throw ImportOperationError("invalid_arguments", "Specify a nonempty object name.");
    auto* state = live.open_part(id);
    const auto& before = state->session.document();
    if (const auto* body = before.body_owner_for_object(value.id)) {
        if (body->derived_copy) throw ImportOperationError("read_only_body", "A derived Body cannot be edited directly.");
        if (body->scope.id != before.body_history.active_body_id())
            throw ImportOperationError("inactive_body", "Activate the owning Body before editing its history.");
    }
    if (value.combine_mode == document::CombineMode::Subtract) {
        const auto boundary = state->session.rollback_boundary(value.id);
        if (!boundary || !boundary->input_body)
            throw ImportOperationError("missing_input", "Imported subtraction requires an earlier calculated body.");
    }
    if (value == original) return false;
    PartCalculationPolicy policy;
    policy.reject_errors = true;
    policy.edited_document_id = id;
    policy.edited_history_limit = before.history_index(value.id);
    auto next = before;
    *next.find_container(value.id) = std::move(value);
    const auto& previous = state->session.calculated_boundaries();
    auto references = construction_reference_source_geometry(previous);
    append_reference_geometry(references, next.origin_viewer_mesh().original_references);
    append_reference_geometry(references, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    // Preserve the imported-feature GUI calculation boundary and refresh.
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    static_cast<void>(refresh_sketch_external_references(next, calculated));
    commit_part_document(live, id, std::move(next), std::move(calculated));
    return true;
}
bool set_imported_feature_reference(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, const std::string& container, std::size_t index,
    document::ConstructionReference source, bool derive_orientation) {
    static_cast<void>(imported_feature(live, id, container));
    auto value = prepare_part_feature_reference(live, id, container, index, std::move(source), derive_orientation);
    return commit_imported_feature(live, kernel, id, std::move(value));
}
}
