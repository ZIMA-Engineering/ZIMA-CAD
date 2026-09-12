#include <zima/workspace/sweep_operations.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/kernel/stable_id.hpp>
#include <algorithm>
#include <cmath>
#include <set>

namespace zima::workspace {
namespace {
using Kind = document::FeatureKind;
bool supported(Kind kind) {
    return kind == Kind::Sweep2D || kind == Kind::Sweep3D || kind == Kind::HelicalSweep;
}
void validate_sweep(const document::HistoryContainer& feature) {
    if (!supported(feature.feature_kind))
        throw SweepOperationError("wrong_feature", "This container is not a supported Sweep.");
    const bool helical = feature.feature_kind == Kind::HelicalSweep;
    const auto dimension = helical ? feature.helical.pitch
        : feature.feature_kind == Kind::Sweep2D ? feature.sweep2d.thickness : feature.sweep3d.thickness;
    const auto minimum = helical ? 0.0001 : 0.001;
    if (!std::isfinite(dimension) || dimension < minimum || dimension > 1000000)
        throw SweepOperationError("invalid_arguments", "Sweep thickness or pitch is outside the supported range.");
    std::set<std::string> sketches;
    document::visit_feature_sketches(feature, [&](const auto& data, std::size_t) {
        const auto sketch = sketcher::Sketch::from_serialized(data);
        sketch.validate();
        if (sketch.owner_container_id != feature.id || !sketches.insert(sketch.id).second)
            throw SweepOperationError("invalid_sketch_owner", "Every embedded Sweep Sketch must have one owning feature.");
    });
    const auto& profiles = feature.feature_kind == Kind::Sweep2D ? feature.sweep2d.profiles : feature.sweep3d.profiles;
    std::set<std::string> profile_ids;
    std::set<std::pair<std::string, bool>> stations;
    for (const auto& profile : profiles) {
        const auto sketch = sketcher::Sketch::from_serialized(profile.sketch_serialized);
        if (profile.id.empty() || profile.point_id.empty() || profile.sketch_id != sketch.id ||
            !profile_ids.insert(profile.id).second || !stations.emplace(profile.point_id, profile.incoming).second)
            throw SweepOperationError("invalid_profile", "Sweep profiles require distinct identities and valid path stations.");
    }
}
void validate_path_plane_source(const document::PartDocument& part, const document::HistoryContainer& feature) {
    if (feature.feature_kind != Kind::Sweep2D || !feature.sweep2d.path_plane) return;
    const auto& reference = *feature.sweep2d.path_plane;
    if (!reference.instance_path.empty() || reference.owner_id.empty() || reference.semantic_key.empty())
        throw SweepOperationError("invalid_reference", "The Sweep path plane must belong to this Part.");
    if (!std::isfinite(reference.offset) || std::abs(reference.offset) > 1000000)
        throw SweepOperationError("invalid_arguments", "The path plane offset is outside the supported range.");
    if (reference.owner_id == feature.container_origin.id) return;
    auto allowed = sketch_external_reference_source_owners(part, sketcher::Sketch::from_serialized(feature.sweep2d.path_sketch).id);
    allowed.insert(part.document_id + ":origin");
    const auto* target = part.body_owner_for_object(feature.id);
    for (const auto& body : part.body_history.bodies()) {
        allowed.insert(body.origin().id);
        if (target && body.scope.id == target->scope.id) break;
    }
    if (!allowed.contains(reference.owner_id))
        throw SweepOperationError("invalid_reference_source", "The Sweep path plane must be an earlier object or an available Origin.");
}
void adopt_sources(document::PartDocument& next, const document::HistoryContainer& feature,
    const document::HistoryContainer* existing) {
    const bool creating = existing == nullptr;
    if (creating && feature.feature_kind != Kind::Sweep3D)
        throw SweepOperationError("wrong_feature", "Source adoption currently requires a 3D Sweep.");
    std::set<std::string> roots;
    const std::string retained_path = creating ? feature.sweep3d.path.id : std::string{};
    if (creating) {
        const auto path = std::ranges::find(next.constructions, retained_path, &document::ConstructionObject::id);
        if (path == next.constructions.end() || path->kind != document::ConstructionKind::Curve3D || !path->parent_construction_id.empty())
            throw SweepOperationError("construction_not_found", "Select a standalone 3D curve for the Sweep path.");
        if (path->suppressed)
            throw SweepOperationError("inactive_input", "Sweep inputs must be active before the history cursor.");
        roots.insert(path->id);
    }
    const auto& profiles = feature.feature_kind == Kind::Sweep2D ? feature.sweep2d.profiles : feature.sweep3d.profiles;
    for (const auto& profile : profiles) {
        if (existing) {
            const auto& previous = existing->feature_kind == Kind::Sweep2D ? existing->sweep2d.profiles : existing->sweep3d.profiles;
            if (std::ranges::any_of(previous, [&](const auto& old) { return old.sketch_id == profile.sketch_id; })) continue;
        }
        const auto source = std::ranges::find(next.sketches, profile.sketch_id, &sketcher::Sketch::id);
        const auto* owner = source == next.sketches.end() ? nullptr : next.find_container(source->owner_container_id);
        if (!owner || owner->feature_kind != Kind::Sketch || !roots.insert(owner->id).second)
            throw SweepOperationError("profile_owned", "Select distinct standalone Sketches for the Sweep profiles.");
        if (source->suppressed || owner->suppressed)
            throw SweepOperationError("inactive_input", "Sweep inputs must be active before the history cursor.");
    }
    if (roots.empty()) return;
    const auto* owner = next.body_history.find(next.body_history.active_body_id());
    if (!owner || owner->derived_copy)
        throw SweepOperationError("read_only_body", "Sweep inputs require an active editable Body.");
    auto body = *owner;
    for (const auto& root : roots) {
        const auto entry = std::ranges::find(body.entries, root, &document::PartHistoryEntry::id);
        if (entry == body.entries.end())
            throw SweepOperationError("inactive_body", "Every Sweep input must belong to the active Body.");
        if (static_cast<std::size_t>(entry - body.entries.begin()) >= body.cursor)
            throw SweepOperationError("inactive_input", "Sweep inputs must be active before the history cursor.");
    }
    const auto dependencies = part_history_dependency_graph(next);
    for (const auto& [source, consumer] : dependencies.edges) {
        if (!roots.contains(source)) continue;
        if (!roots.contains(consumer))
            throw SweepOperationError("input_in_use", "Another object depends on a Sweep input container.");
        throw SweepOperationError("input_dependency", "Sweep input containers must not depend on each other.");
    }
    for (const auto& [consumer, instance, source, semantic] : dependencies.references) {
        const auto alias = dependencies.owners.find(source);
        if (roots.contains(consumer) && alias != dependencies.owners.end() && roots.contains(alias->second) && alias->second != retained_path)
            throw SweepOperationError("input_dependency", "Sweep profiles must not reference a consumed Sketch container.");
    }
    if (existing) {
        const auto boundary = std::ranges::find(next.history_order, existing->id, &document::PartHistoryEntry::id);
        for (const auto& [source, consumer] : dependencies.edges) {
            if (!roots.contains(consumer) || roots.contains(source)) continue;
            const auto position = std::ranges::find(next.history_order, source, &document::PartHistoryEntry::id);
            if (position != next.history_order.end() && position >= boundary)
                throw SweepOperationError("history_dependency", "A new Sweep profile must not depend on this Sweep or later history.");
        }
    }
    // Remove the inputs once in the caller's draft; cached bodies and the live
    // document are unchanged until the complete new Sweep has calculated.
    const auto removed_before_cursor = std::ranges::count_if(body.entries.begin(),
        body.entries.begin() + static_cast<std::ptrdiff_t>(body.cursor), [&](const auto& entry) { return roots.contains(entry.id); });
    std::erase_if(body.entries, [&](const auto& entry) { return roots.contains(entry.id); });
    body.cursor -= static_cast<std::size_t>(removed_before_cursor);
    next.body_history.update_body(std::move(body));
    std::erase_if(next.history, [&](const auto& value) { return roots.contains(value.id); });
    std::erase_if(next.constructions, [&](const auto& value) { return roots.contains(value.id); });
    std::erase_if(next.sketches, [&](const auto& value) { return roots.contains(value.owner_container_id); });
    next.set_body_history(next.body_history);
}
}
document::Sweep3DProfile sweep_profile_from_source(const document::PartDocument& document,
    const std::string& feature_id, const SweepProfileSource& input) {
    const auto stored = std::ranges::find(document.sketches, input.sketch_id, &sketcher::Sketch::id);
    if (stored == document.sketches.end())
        throw SweepOperationError("sketch_not_found", "The requested Sketch does not exist.");
    auto sketch = *stored; sketch.owner_container_id = feature_id;
    return {kernel::make_stable_id(), input.point_id, sketch.id,
        sketch.serialized(), input.incoming, input.correspondence_start_point_id};
}
document::HistoryContainer sweep3d_from_sources(const document::PartDocument& document,
    const std::string& path_id, const document::Placement& path_placement, const std::vector<SweepProfileSource>& profiles) {
    const auto source = std::ranges::find(document.constructions, path_id, &document::ConstructionObject::id);
    if (source == document.constructions.end() || source->kind != document::ConstructionKind::Curve3D || !source->parent_construction_id.empty())
        throw SweepOperationError("construction_not_found", "Select a standalone 3D curve for the Sweep path.");
    if (profiles.empty() || profiles.size() > 5000)
        throw SweepOperationError("invalid_profile", "A Sweep requires between 1 and 5000 profile Sketches.");
    auto feature = document::PartDocument::create_sweep3d_container();
    feature.placement = path_placement;
    document::PartDocument::set_sweep3d_owned_path(feature, *source);
    for (const auto& input : profiles)
        feature.sweep3d.profiles.push_back(sweep_profile_from_source(document, feature.id, input));
    return feature;
}
void commit_sweep(Workspace& live, const kernel::OcctKernel& kernel, const std::string& id,
    document::HistoryContainer feature, SweepEditMode mode) {
    auto* state = live.open_part(id);
    if (!state) throw SweepOperationError("unsupported_document", "Sweep operations require an open Part.");
    validate_sweep(feature);
    const auto& before = state->session.document();
    const auto container_id = feature.id;
    const auto* existing = before.find_container(feature.id);
    PartCalculationPolicy policy;
    policy.reject_errors = true;
    if (mode == SweepEditMode::Replace || mode == SweepEditMode::ReplaceAdoptSources) {
        if (!existing) throw SweepOperationError("container_not_found", "The requested container does not exist.");
        if (existing->feature_kind != feature.feature_kind)
            throw SweepOperationError("wrong_feature", "This container is not the requested Sweep type.");
        if (existing->feature_id != feature.feature_id || existing->feature_parent_id != feature.feature_parent_id ||
            existing->container_origin != feature.container_origin)
            throw SweepOperationError("identity_changed", "Editing must preserve the container identity.");
        const bool helical = feature.feature_kind == Kind::HelicalSweep;
        const auto old_dimension = helical ? existing->helical.pitch
            : feature.feature_kind == Kind::Sweep2D ? existing->sweep2d.thickness : existing->sweep3d.thickness;
        const auto new_dimension = helical ? feature.helical.pitch
            : feature.feature_kind == Kind::Sweep2D ? feature.sweep2d.thickness : feature.sweep3d.thickness;
        if (feature.value_locks.contains(helical ? "pitch" : "thickness") && old_dimension != new_dimension)
            throw SweepOperationError("value_locked", "The requested value is locked.");
        policy.edited_document_id = id;
        policy.edited_history_limit = before.history_index(feature.id);
    } else if (existing || feature.id.empty() || feature.feature_id.empty() || feature.feature_parent_id != feature.id) {
        throw SweepOperationError("identity_changed", "A new container must have a new nonempty identity.");
    }
    const auto* body = existing ? before.body_owner_for_object(feature.id)
        : before.body_history.find(before.body_history.active_body_id());
    if (body && body->derived_copy)
        throw SweepOperationError("read_only_body", "A derived Body cannot be edited directly.");
    if (body && body->scope.id != before.body_history.active_body_id())
        throw SweepOperationError("inactive_body", "Activate the owning Body before editing its history.");
    auto next = before;
    if (mode == SweepEditMode::AdoptSources || mode == SweepEditMode::ReplaceAdoptSources) adopt_sources(next, feature, existing);
    if (existing) *next.find_container(feature.id) = std::move(feature);
    else {
        next.insert_history_entry(document::PartHistoryKind::Feature, feature.id);
        next.history.push_back(std::move(feature));
    }
    const auto& stored = *next.find_container(container_id);
    validate_path_plane_source(next, stored);
    // Adopted Sketch containers may precede the edited feature. Resolve its
    // validation boundary after removing those inputs, not at the old index.
    if (existing) policy.edited_history_limit = next.history_index(existing->id);
    const auto& previous = state->session.calculated_boundaries();
    auto references = construction_reference_source_geometry(previous);
    append_reference_geometry(references, next.origin_viewer_mesh().original_references);
    append_reference_geometry(references, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    state->session.commit(std::move(next), std::move(calculated));
}
} // namespace zima::workspace
