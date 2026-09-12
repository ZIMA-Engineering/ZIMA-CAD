#include <zima/workspace/sweep_operations.hpp>
#include <zima/document/feature_sketches.hpp>
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
}
void commit_sweep(Workspace& live, const kernel::OcctKernel& kernel, const std::string& id,
    document::HistoryContainer feature, SweepEditMode mode) {
    auto* state = live.open_part(id);
    if (!state) throw SweepOperationError("unsupported_document", "Sweep operations require an open Part.");
    validate_sweep(feature);
    const auto& before = state->session.document();
    const auto* existing = before.find_container(feature.id);
    PartCalculationPolicy policy;
    policy.reject_errors = true;
    if (mode == SweepEditMode::Replace) {
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
    if (existing) *next.find_container(feature.id) = std::move(feature);
    else {
        next.insert_history_entry(document::PartHistoryKind::Feature, feature.id);
        next.history.push_back(std::move(feature));
    }
    const auto& previous = state->session.calculated_boundaries();
    auto references = construction_reference_source_geometry(previous);
    append_reference_geometry(references, next.origin_viewer_mesh().original_references);
    append_reference_geometry(references, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    state->session.commit(std::move(next), std::move(calculated));
}
} // namespace zima::workspace
