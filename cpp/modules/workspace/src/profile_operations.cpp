#include <zima/workspace/part_transactions.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
void normalize_owned_profile_front_references(
        std::vector<zima::document::ConstructionReference>& references,
        bool preserve_front_through_origin_triad) {
    const auto first = std::find_if(references.begin(), references.end(),
        [](const auto& reference) {
            return !reference.orientation_only && reference.supports_offset &&
                !reference.owner_id.empty();
        });
    if (first == references.end()) return;
    const auto owner = first->owner_id;
    const auto path = first->instance_path;
    const auto semantic = first->semantic_key;
    const auto same_source = [&](const auto& reference) {
        return reference.owner_id == owner &&
            reference.instance_path == path &&
            reference.semantic_key == semantic;
    };
    const auto second_plane = std::find_if(std::next(first), references.end(),
        [&](const auto& reference) {
            return !reference.orientation_only && reference.supports_offset &&
                !same_source(reference);
        });
    for (auto& reference : references) {
        if (reference.orientation_only) continue;
        const bool front = same_source(reference);
        const bool top = second_plane != references.end() &&
            &reference == &*second_plane;
        reference.orientation_drives_rotation = front || top;
        reference.orientation_role = front ? "front" : top ? "top" : "none";
    }
    // Erasing an earlier orientation-only row can invalidate first.
    auto explicit_front = *first;
    std::erase_if(references, [&](const auto& reference) {
        return reference.orientation_only && !same_source(reference);
    });
    if (preserve_front_through_origin_triad) {
        // resolve_placement() deliberately collapses the ordinary three
        // planes of the main Origin to the document identity frame.  An
        // owned Sketch/profile is different: its first positional plane is
        // explicitly its FRONT, also while the feature dialog is only
        // showing a transient preview.  The calculated Sketch path already
        // supplies this non-persisted orientation twin; do the same here so
        // the cyan local origin cannot jump to the last reference until the
        // first profile calculation/drag refreshes it.
        explicit_front.orientation_only = true;
        explicit_front.orientation_drives_rotation = true;
        explicit_front.orientation_role = "front";
        references.push_back(std::move(explicit_front));
    }
}

std::string revolution_axis_segment_id(
    const zima::sketcher::Sketch& sketch,
    const std::string& configured_id) {
    const auto valid_axis = [&](const auto& segment) {
        return segment.construction && segment.centerline;
    };
    if (!configured_id.empty()) {
        const auto configured = std::find_if(
            sketch.segments.begin(), sketch.segments.end(), [&](const auto& segment) {
                return segment.id == configured_id && valid_axis(segment);
            });
        if (configured != sketch.segments.end()) return configured->id;
    }
    std::string result;
    for (const auto& segment : sketch.segments) {
        if (!valid_axis(segment)) continue;
        if (!result.empty()) {
            throw std::runtime_error(
                "Skica rotace smí obsahovat právě jednu zelenou konstrukční osu.");
        }
        result = segment.id;
    }
    if (result.empty()) {
        throw std::runtime_error(
            "Ve skice rotace nakreslete zelenou konstrukční osu.");
    }
    return result;
}


namespace {
bool is_profile(document::FeatureKind kind) {
    return kind == document::FeatureKind::Extrusion || kind == document::FeatureKind::Revolution;
}
}
void validate_profile_definition(const document::HistoryContainer& value) {
    if (!is_profile(value.feature_kind)) throw ProfileOperationError("wrong_feature", "This container is not an Extrusion or Revolution.");
    const bool extrusion = value.feature_kind == document::FeatureKind::Extrusion;
    const auto number = [](double value, double minimum, double maximum) {
        if (!std::isfinite(value) || value < minimum || value > maximum)
            throw ProfileOperationError("invalid_arguments", "Profile dimension is outside the supported range.");
    };
    number(extrusion ? value.extrusion.profile_plane_offset : value.revolution.profile_plane_offset, -1000000, 1000000);
    number(extrusion ? value.extrusion.thin_thickness : value.revolution.thin_thickness, 0.001, 1000000);
    number(extrusion ? value.extrusion.length_forward : value.revolution.angle_degrees, 0.001, extrusion ? 1000000 : 360);
    number(extrusion ? value.extrusion.length_reverse : value.revolution.angle_reverse, 0.001, extrusion ? 1000000 : 360);
    if (!extrusion) return;
    const auto side = [&](document::EndCondition condition, const auto& targets) {
        if (condition == document::EndCondition::UpTo && (targets.empty() || !targets.front().reference.valid()))
            throw ProfileOperationError("missing_reference", "Select a target point, plane or planar face.");
        if (condition == document::EndCondition::ThroughAll && value.combine_mode != document::CombineMode::Subtract)
            throw ProfileOperationError("invalid_arguments", "Through all is available only for a cut.");
    };
    side(value.extrusion.end_condition_forward, value.extrusion.end_targets_forward);
    if (value.extrusion.extent_mode == document::ProfileExtentMode::TwoSides)
        side(value.extrusion.end_condition_reverse, value.extrusion.end_targets_reverse);
}
document::HistoryContainer profile_from_sketch(const document::PartDocument& part,
    const std::string& sketch_id, document::FeatureKind kind) {
    if (!is_profile(kind)) throw ProfileOperationError("wrong_feature", "This container is not an Extrusion or Revolution.");
    const auto sketch = std::ranges::find(part.sketches, sketch_id, &sketcher::Sketch::id);
    if (sketch == part.sketches.end()) throw ProfileOperationError("sketch_not_found", "The requested Sketch does not exist.");
    const auto* owner = part.find_container(sketch->owner_container_id);
    if (!owner || owner->feature_kind != document::FeatureKind::Sketch)
        throw ProfileOperationError("profile_owned", "Select a standalone Sketch container for conversion to a profile feature.");
    auto value = kind == document::FeatureKind::Extrusion ? document::PartDocument::create_extrusion_container(sketch_id)
        : document::PartDocument::create_revolution_container(sketch_id);
    value.id = owner->id; value.feature_parent_id = owner->id; value.container_origin = owner->container_origin;
    value.placement = owner->placement; value.suppressed = owner->suppressed; value.combine_mode = owner->combine_mode;
    if (kind == document::FeatureKind::Extrusion) value.extrusion.profile_plane_offset = sketch->plane_offset;
    else value.revolution.profile_plane_offset = sketch->plane_offset;
    return value;
}
void commit_profile(Workspace& live, const kernel::OcctKernel& kernel, const std::string& id,
    document::HistoryContainer value, ProfileEditMode mode, const std::optional<sketcher::Sketch>& owned_sketch) {
    auto* state = live.open_part(id);
    if (!state) throw ProfileOperationError("unsupported_document", "Profile operations require an open Part.");
    validate_profile_definition(value);
    const auto& before = state->session.document();
    const auto* existing = before.find_container(value.id);
    if (mode == ProfileEditMode::Create) {
        if (existing || value.id.empty() || value.feature_id.empty())
            throw ProfileOperationError("identity_changed", "A new container must have a new nonempty identity.");
    } else {
        if (!existing) throw ProfileOperationError("container_not_found", "The requested container does not exist.");
        const bool transform = mode == ProfileEditMode::TransformSketch;
        if (transform ? existing->feature_kind != document::FeatureKind::Sketch : existing->feature_kind != value.feature_kind)
            throw ProfileOperationError("wrong_feature", "The requested profile type does not match the container.");
        if (existing->container_origin != value.container_origin || value.feature_parent_id != existing->id ||
            (!transform && value.feature_id != existing->feature_id) || (transform && (value.feature_id.empty() || value.feature_id == existing->feature_id)))
            throw ProfileOperationError("identity_changed", "Editing must preserve the container identity.");
    }
    const auto* body = existing ? before.body_owner_for_object(value.id) : before.body_history.find(before.body_history.active_body_id());
    if (body && body->derived_copy) throw ProfileOperationError("read_only_body", "A derived Body cannot be edited directly.");
    const bool extrusion = value.feature_kind == document::FeatureKind::Extrusion;
    const auto sketch_id = extrusion ? value.extrusion.sketch_id : value.revolution.sketch_id;
    normalize_owned_profile_front_references(value.placement.references);
    auto next = before;
    if (owned_sketch) {
        if (owned_sketch->id != sketch_id) throw ProfileOperationError("identity_changed", "The profile Sketch identity does not match its feature.");
        const auto found = std::ranges::find(next.sketches, sketch_id, &sketcher::Sketch::id);
        if (found != next.sketches.end() && !found->owner_container_id.empty() && found->owner_container_id != value.id)
            throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
        if (found == next.sketches.end()) next.sketches.push_back(*owned_sketch);
        else *found = *owned_sketch;
    }
    const auto profile = std::ranges::find(next.sketches, sketch_id, &sketcher::Sketch::id);
    if (profile == next.sketches.end()) throw ProfileOperationError("sketch_not_found", "Internal profile Sketch no longer exists");
    if (!profile->owner_container_id.empty() && profile->owner_container_id != value.id)
        throw ProfileOperationError("profile_owned", "The Sketch already belongs to another container.");
    if (!extrusion) value.revolution.axis_segment_id = revolution_axis_segment_id(*profile, value.revolution.axis_segment_id);
    const auto source = extrusion ? value.extrusion.profile_source : value.revolution.profile_source;
    if (source == document::ProfileSource::Internal) {
        profile->owner_container_id = value.id;
        const auto first = std::ranges::find_if(value.placement.references, [](const auto& reference) { return !reference.owner_id.empty(); });
        if (first != value.placement.references.end() && first->supports_offset) profile->plane = sketcher::SketchPlane::XZ;
        profile->plane_offset = extrusion ? value.extrusion.profile_plane_offset : value.revolution.profile_plane_offset;
    }
    const auto container_id = value.id;
    if (existing) *next.find_container(container_id) = std::move(value);
    else {next.insert_history_entry(document::PartHistoryKind::Feature, container_id); next.history.push_back(std::move(value));}
    const auto& previous = state->session.calculated_boundaries();
    if(extrusion) {
        auto& parameters=next.find_container(container_id)->extrusion;
        const auto prepare=[&](document::EndCondition condition,auto& targets) {
            if(condition!=document::EndCondition::UpTo)return;
            if(targets.size()!=1)throw ProfileOperationError("missing_reference","Select exactly one extrusion end reference.");
            targets.front()=prepare_profile_end_target(next,previous,*next.find_container(container_id),targets.front());
        };
        prepare(parameters.end_condition_forward,parameters.end_targets_forward);
        if(parameters.extent_mode==document::ProfileExtentMode::TwoSides)prepare(parameters.end_condition_reverse,parameters.end_targets_reverse);
    }
    auto geometry = construction_reference_source_geometry(previous);
    append_reference_geometry(geometry, next.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry, next.construction_viewer_mesh().original_references);
    next.resolve_constructions(geometry);
    PartCalculationPolicy policy; policy.reject_errors = true;
    if (existing) {policy.edited_document_id = id; policy.edited_history_limit = before.history_index(container_id);}
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    static_cast<void>(refresh_sketch_external_references(next, calculated));
    commit_part_document(live,id,std::move(next), std::move(calculated));
}
} // namespace zima::workspace
