#include <zima/workspace/profile_operations.hpp>
#include <zima/document/profile_targets.hpp>
#include <algorithm>
#include <set>

namespace zima::workspace {
document::HistoryContainer profile_from_sketch(const assembly::AssemblyDocument& doc,
    const std::string& sketch_id, document::FeatureKind kind) {
    if (kind != document::FeatureKind::Extrusion && kind != document::FeatureKind::Revolution)
        throw ProfileOperationError("wrong_feature", "The requested profile type does not match the container.");
    const auto sketch = std::ranges::find(doc.sketches, sketch_id, &sketcher::Sketch::id);
    if (sketch == doc.sketches.end())
        throw ProfileOperationError("sketch_not_found", "The requested Sketch does not exist.");
    const auto* owner = doc.find_sketch_container(sketch->owner_container_id);
    if (!owner) throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
    auto value = kind == document::FeatureKind::Extrusion
        ? document::PartDocument::create_extrusion_container(sketch_id)
        : document::PartDocument::create_revolution_container(sketch_id);
    value.id = owner->id; value.feature_parent_id = owner->id;
    value.container_origin = owner->container_origin; value.placement = owner->placement;
    value.suppressed = owner->suppressed;
    value.combine_mode = document::CombineMode::Subtract;
    if (value.placement.value_locks.erase("profile_offset")) value.value_locks.insert("profile_offset");
    if (kind == document::FeatureKind::Extrusion) value.extrusion.profile_plane_offset = sketch->plane_offset;
    else value.revolution.profile_plane_offset = sketch->plane_offset;
    return value;
}
void commit_assembly_profile(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, document::HistoryContainer value,
    std::vector<std::string> targets, ProfileEditMode mode,
    const std::optional<sketcher::Sketch>& owned_sketch) {
    auto* state = live.open_assembly(id);
    if (!state) throw ProfileOperationError("unsupported_document", "Cut operations require an open Assembly.");
    validate_profile_definition(value);
    if (value.combine_mode != document::CombineMode::Subtract)
        throw ProfileOperationError("invalid_arguments", "An Assembly profile must subtract material.");
    const auto& before = state->session.document();
    const auto* existing = before.find_cut(value.id);
    if (mode == ProfileEditMode::Create) {
        if (existing || before.find_sketch_container(value.id) || value.id.empty() || value.feature_id.empty())
            throw ProfileOperationError("identity_changed", "A new container must have a new nonempty identity.");
    } else if (mode == ProfileEditMode::TransformSketch) {
        const auto* owner = before.find_sketch_container(value.id);
        if (!owner || existing)
            throw ProfileOperationError("container_not_found", "The requested container does not exist.");
        if (value.feature_parent_id != value.id || value.container_origin != owner->container_origin ||
            value.feature_id.empty() || value.feature_id == owner->feature_id)
            throw ProfileOperationError("identity_changed", "Editing must preserve the container identity.");
    } else {
        if (mode != ProfileEditMode::Replace || !existing)
            throw ProfileOperationError("container_not_found", "The requested container does not exist.");
        if (existing->definition.feature_kind != value.feature_kind)
            throw ProfileOperationError("wrong_feature", "The requested profile type does not match the container.");
        if (existing->definition.feature_id != value.feature_id ||
            existing->definition.container_origin != value.container_origin || value.feature_parent_id != value.id)
            throw ProfileOperationError("identity_changed", "Editing must preserve the container identity.");
    }
    std::set<std::string> seen;
    for (const auto& target : targets) {
        const auto* occurrence = before.find_occurrence(target);
        if (!occurrence || occurrence->source_kind != assembly::ComponentSourceKind::Part || occurrence->derived_copy)
            throw ProfileOperationError("invalid_cut_target", "A cut target must be an immediate editable Part occurrence.");
        if (!seen.insert(target).second)
            throw ProfileOperationError("duplicate_target", "A cut target cannot be assigned twice.");
    }
    const bool extrusion = value.feature_kind == document::FeatureKind::Extrusion;
    const auto sketch_id = extrusion ? value.extrusion.sketch_id : value.revolution.sketch_id;
    auto sketches = before.sketches;
    if (owned_sketch) {
        if (owned_sketch->id != sketch_id)
            throw ProfileOperationError("identity_changed", "The profile Sketch identity does not match its feature.");
        const auto found = std::ranges::find(sketches, sketch_id, &sketcher::Sketch::id);
        if (found != sketches.end()) {
            if (!found->owner_container_id.empty() && found->owner_container_id != value.id)
                throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
            *found = *owned_sketch;
        } else sketches.push_back(*owned_sketch);
    }
    const auto profile = std::ranges::find(sketches, sketch_id, &sketcher::Sketch::id);
    if (profile == sketches.end()) throw ProfileOperationError("sketch_not_found", "Internal profile Sketch no longer exists");
    if (!profile->owner_container_id.empty() && profile->owner_container_id != value.id)
        throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
    if (mode == ProfileEditMode::TransformSketch && profile->owner_container_id != value.id)
        throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
    profile->validate();
    if (!extrusion) value.revolution.axis_segment_id = revolution_axis_segment_id(*profile, value.revolution.axis_segment_id);
    profile->owner_container_id = value.id;
    profile->plane_offset = extrusion ? value.extrusion.profile_plane_offset : value.revolution.profile_plane_offset;
    normalize_owned_profile_front_references(value.placement.references);

    // Resolve dependencies into a private document. A rejected cutter must not
    // publish an intermediate refresh or alter the source Part or its history.
    auto next = live.prepare_assembly_calculation(id);
    next.sketches = std::move(sketches);
    if (extrusion) {
        auto geometry = next.build_scene().original_references;
        append_reference_geometry(geometry, next.origin_viewer_mesh().original_references);
        append_reference_geometry(geometry, next.construction_viewer_mesh().original_references);
        const auto prepare = [&](document::EndCondition condition, auto& references) {
            if (condition != document::EndCondition::UpTo) return;
            if (references.size() != 1 || references.front().kind == document::EndTargetKind::Point)
                throw ProfileOperationError("missing_reference", "Select exactly one extrusion end reference.");
            const auto resolved = document::resolve_profile_target(references.front(), geometry);
            if (!resolved) throw ProfileOperationError("missing_reference", "The original extrusion target is unavailable.");
            references.front() = *resolved;
        };
        prepare(value.extrusion.end_condition_forward, value.extrusion.end_targets_forward);
        if (value.extrusion.extent_mode == document::ProfileExtentMode::TwoSides)
            prepare(value.extrusion.end_condition_reverse, value.extrusion.end_targets_reverse);
    }
    if (mode == ProfileEditMode::TransformSketch)
        std::erase_if(next.sketch_containers, [&](const auto& container) { return container.id == value.id; });
    assembly::AssemblyCut cut{std::move(value), std::move(targets)};
    if (existing) *next.find_cut(cut.definition.id) = std::move(cut);
    else next.cuts.push_back(std::move(cut));
    next.validate_sketch_containers();
    calculate_resolved_assembly_cuts(kernel, next);
    state->session.commit(std::move(next));
}
} // namespace zima::workspace
