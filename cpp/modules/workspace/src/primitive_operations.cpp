#include <zima/workspace/primitive_operations.hpp>
#include <cmath>
#include <algorithm>
#include <type_traits>

namespace zima::workspace {
const std::vector<PrimitiveDefinition>& primitive_definitions() {
    using Part = document::PartDocument;
    using Kind = document::FeatureKind;
    static const std::vector<PrimitiveDefinition> definitions{
        {Kind::TwistedSheet,"twisted_sheet","Twisted Sheet",
            &Part::create_twisted_sheet_container}};
    return definitions;
}
const PrimitiveDefinition* primitive_definition(document::FeatureKind kind) {
    for(const auto& definition : primitive_definitions()) if(definition.kind == kind) return &definition;
    return nullptr;
}
namespace {
template<class Container> auto dimension_slots(Container& container) {
    using Value = std::remove_reference_t<decltype((container.twisted_sheet.width))>;
    using Slots = std::vector<std::pair<std::string,Value*>>;
    using Kind = document::FeatureKind;
    switch(container.feature_kind) {
    case Kind::TwistedSheet: return Slots{{"width",&container.twisted_sheet.width},
        {"length",&container.twisted_sheet.length},
        {"angle",&container.twisted_sheet.angle_degrees},
        {"thickness",&container.twisted_sheet.thickness},
        {"developed_length_correction",&container.twisted_sheet.developed_length_correction}};
    default: throw PrimitiveOperationError("wrong_feature", "This container is not a supported primitive.");
    }
}
void validate_dimensions(const document::HistoryContainer& container) {
    for(const auto& [name,value] : dimension_slots(container)) {
        const double minimum = name == "developed_length_correction" ? -1000000.0 : 0.001;
        const double maximum = name == "angle" ? 36000.0 : 1000000.0;
        if(!std::isfinite(*value) || *value < minimum || *value > maximum)
            throw PrimitiveOperationError("invalid_arguments", "Primitive dimension is outside its supported range.");
    }
    if(container.feature_kind==document::FeatureKind::TwistedSheet) {
        try {static_cast<void>(document::twisted_sheet_developed_length(container.twisted_sheet));}
        catch(const std::exception& error) {throw PrimitiveOperationError("invalid_arguments",error.what());}
    }
}
}
std::vector<std::pair<std::string,double>> primitive_dimensions(const document::HistoryContainer& container) {
    std::vector<std::pair<std::string,double>> values;
    for(const auto& [name,value] : dimension_slots(container)) values.emplace_back(name,*value);
    return values;
}
void assign_primitive_dimensions(document::HistoryContainer& container, const PrimitiveDimensionPatch& patch) {
    auto draft = container;
    const auto slots = dimension_slots(draft);
    for(const auto& [name,value] : patch) {
        const auto found = std::find_if(slots.begin(),slots.end(),[&](const auto& slot){return slot.first == name;});
        if(found == slots.end()) throw PrimitiveOperationError("invalid_arguments", "Unknown primitive dimension.");
        *found->second = value;
    }
    validate_dimensions(draft);
    container = std::move(draft);
}
bool commit_primitive(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, document::HistoryContainer committed, PrimitiveEditMode mode) {
    auto* state = workspace.open_part(document_id);
    if(!state) throw PrimitiveOperationError("unsupported_document", "Primitive operations require an open Part.");
    if(!primitive_definition(committed.feature_kind))
        throw PrimitiveOperationError("wrong_feature", "This container is not a supported primitive.");
    validate_dimensions(committed);
    const auto& before = state->session.document();
    const auto* existing = before.find_container(committed.id);
    PartCalculationPolicy policy;
    policy.reject_errors = true;
    if(mode == PrimitiveEditMode::Replace) {
        if(!existing) throw PrimitiveOperationError("container_not_found", "The requested container does not exist.");
        if(existing->feature_kind != committed.feature_kind)
            throw PrimitiveOperationError("wrong_feature", "This container is not a supported primitive.");
        if(existing->feature_id != committed.feature_id || existing->feature_parent_id != committed.feature_parent_id ||
            existing->container_origin != committed.container_origin)
            throw PrimitiveOperationError("identity_changed", "Editing must preserve the container identity.");
        if(*existing == committed) return false;
        policy.edited_document_id = document_id;
        policy.edited_history_limit = before.history_index(committed.id);
    } else if(existing || committed.id.empty() || committed.feature_id.empty()) {
        throw PrimitiveOperationError("identity_changed", "A new container must have a new nonempty identity.");
    }
    const auto* body = mode == PrimitiveEditMode::Replace ? before.body_owner_for_object(committed.id)
        : before.body_history.find(before.body_history.active_body_id());
    if(body && body->derived_copy)
        throw PrimitiveOperationError("read_only_body", "A derived Body cannot be edited directly.");
    auto next = before;
    if(mode == PrimitiveEditMode::Replace) *next.find_container(committed.id) = std::move(committed);
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
    auto calculated = calculate_part_with_resolved_references(kernel, next, &calculated_before, policy);
    static_cast<void>(refresh_sketch_external_references(next, calculated));
    state->session.commit(std::move(next), std::move(calculated));
    return true;
}
bool set_primitive_dimensions(Workspace& workspace, const kernel::OcctKernel& kernel,
    const std::string& document_id, const std::string& container_id,
    document::FeatureKind expected_kind, const PrimitiveDimensionPatch& patch) {
    const auto* state = workspace.open_part(document_id);
    if(!state) throw PrimitiveOperationError("unsupported_document", "Primitive operations require an open Part.");
    const auto* existing = state->session.document().find_container(container_id);
    if(!existing) throw PrimitiveOperationError("container_not_found", "The requested container does not exist.");
    if(existing->feature_kind != expected_kind)
        throw PrimitiveOperationError("wrong_feature", "The requested primitive type does not match the container.");
    if(patch.empty()) throw PrimitiveOperationError("invalid_arguments", "Specify at least one primitive dimension.");
    for(const auto& [name,value] : primitive_dimensions(*existing)) {
        const auto requested = patch.find(name);
        if(requested != patch.end() && requested->second != value && existing->value_locks.contains(name))
            throw PrimitiveOperationError("value_locked", "Unlock the dimension before changing it.");
    }
    auto next = *existing;
    assign_primitive_dimensions(next,patch);
    return commit_primitive(workspace,kernel,document_id,std::move(next),PrimitiveEditMode::Replace);
}
} // namespace zima::workspace
