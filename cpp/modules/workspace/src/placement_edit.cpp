#include <zima/workspace/placement_edit.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace zima::workspace {
bool placement_angle_uses_reference_correction(
    const std::vector<document::ConstructionReference>& references,
    const kernel::ViewerReferenceGeometry& geometry, const kernel::Vec3& origin,
    std::size_t axis) {
    if (axis >= 3 || !std::any_of(references.begin(), references.end(),
        [](const auto& reference) { return reference.orientation_drives_rotation; })) return false;
    return document::orientation_constraint_state(references, geometry, true, origin).constrained_axes[axis];
}
namespace {
struct Fields {
    std::array<double*, 3> position, rotation, absolute, correction;
    std::vector<document::ConstructionReference>& references;
    const std::set<std::string>& locks;
    std::string lock_prefix;
};
bool assign(Fields fields, const kernel::ViewerReferenceGeometry& geometry,
    std::string_view key, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Placement value must be finite.");
    if (fields.locks.contains(fields.lock_prefix + std::string(key))) return false;
    const kernel::Vec3 origin{*fields.position[0], *fields.position[1], *fields.position[2]};
    if (key == "x" || key == "y" || key == "z") {
        const std::size_t axis = key == "x" ? 0 : key == "y" ? 1 : 2;
        if (document::point_constraint_state(fields.references, geometry).constrained_axes[axis]) return false;
        *fields.position[axis] = value; return true;
    }
    if (key.starts_with("reference_offset:")) {
        const auto suffix = key.substr(std::string_view("reference_offset:").size());
        std::size_t index{};
        const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
        if (error != std::errc{} || end != suffix.data() + suffix.size()) return false;
        for (auto& reference : fields.references) {
            if (reference.orientation_only || (reference.owner_id.empty() && reference.semantic_key.empty())) continue;
            if (index-- != 0) continue;
            if (!reference.supports_offset || reference.offset_locked) return false;
            reference.offset = value; return true;
        }
        return false;
    }
    if (key == "rotation_x" || key == "rotation_y" || key == "rotation_z") {
        const std::size_t axis = key == "rotation_x" ? 0 : key == "rotation_y" ? 1 : 2;
        if (placement_angle_uses_reference_correction(fields.references, geometry, origin, axis)) {
            const auto correction_key = fields.lock_prefix + "rotation_offset_" + std::string(key.substr(9));
            if (fields.locks.contains(correction_key)) return false;
            *fields.correction[axis] = value;
        } else { *fields.absolute[axis] = value; *fields.rotation[axis] = value; }
        return true;
    }
    return false;
}
void check_construction(const document::ConstructionObject* existing,
    const document::ConstructionObject& value, ConstructionEditMode mode) {
    if (mode == ConstructionEditMode::Replace) {
        if (!existing) throw std::runtime_error("Construction object no longer exists");
        if (value.kind != existing->kind || value.entity_id != existing->entity_id ||
            value.entity_parent_id != existing->entity_parent_id ||
            value.parent_construction_id != existing->parent_construction_id ||
            value.container_origin != existing->container_origin)
            throw std::runtime_error("Editing must preserve the construction identity.");
    } else if (existing || value.id.empty() || !value.parent_construction_id.empty())
        throw std::runtime_error("A new construction must have a new root identity.");
}
template<class Document> void replace_construction(Document& next,
    document::ConstructionObject value, ConstructionEditMode mode) {
    if (mode == ConstructionEditMode::Replace) *next.find_construction(value.id) = std::move(value);
    else {
        if constexpr (requires { next.insert_history_entry(document::PartHistoryKind::Construction, value.id); })
            next.insert_history_entry(document::PartHistoryKind::Construction, value.id);
        next.constructions.push_back(std::move(value));
    }
}
template<class Document> void require_resolved(const Document& next, const std::string& id) {
    const auto* resolved = next.find_construction(id);
    if (!resolved || !resolved->reference_valid)
        throw std::runtime_error("Construction definition has a missing or cyclic reference");
    const auto* curve = resolved->kind == document::ConstructionKind::Curve3D ? resolved
        : resolved->parent_construction_id.empty() ? nullptr
        : next.find_construction(resolved->parent_construction_id);
    if (curve && curve->kind == document::ConstructionKind::Curve3D)
        static_cast<void>(document::curve3d_route(*curve));
}
}
bool assign_placement_dimension(document::Placement& value,
    const kernel::ViewerReferenceGeometry& geometry, std::string_view key, double number) {
    return assign({{&value.x, &value.y, &value.z},
        {&value.rotation_x, &value.rotation_y, &value.rotation_z},
        {&value.absolute_rotation_x, &value.absolute_rotation_y, &value.absolute_rotation_z},
        {&value.rotation_offset_x, &value.rotation_offset_y, &value.rotation_offset_z},
        value.references, value.value_locks, {}}, geometry, key, number);
}
bool assign_placement_dimension(document::ConstructionObject& value,
    const kernel::ViewerReferenceGeometry& geometry, std::string_view key, double number) {
    return assign({{&value.origin.x, &value.origin.y, &value.origin.z},
        {&value.rotation.x, &value.rotation.y, &value.rotation.z},
        {&value.absolute_rotation.x, &value.absolute_rotation.y, &value.absolute_rotation.z},
        {&value.rotation_offset_x, &value.rotation_offset_y, &value.rotation_offset_z},
        value.references, value.value_locks, "placement:"}, geometry, key, number);
}
void commit_part_parameter_edit(PartState& part, const kernel::OcctKernel& kernel,
    document::PartDocument next, const std::string& owner, bool parameter,
    const PartCalculationPolicy& policy) {
    const auto& previous = part.session.calculated_boundaries();
    if (parameter) {
        const auto* edited = next.find_container(owner);
        if (edited && (edited->feature_kind == document::FeatureKind::Extrusion ||
            edited->feature_kind == document::FeatureKind::Revolution)) {
            const bool extrusion = edited->feature_kind == document::FeatureKind::Extrusion;
            const auto source = extrusion ? edited->extrusion.profile_source : edited->revolution.profile_source;
            const auto& sketch_id = extrusion ? edited->extrusion.sketch_id : edited->revolution.sketch_id;
            if (source == document::ProfileSource::Internal) {
                const auto owned = std::ranges::find_if(next.sketches,
                    [&](const auto& sketch) { return sketch.id == sketch_id; });
                if (owned == next.sketches.end()) throw std::runtime_error("Internal profile Sketch no longer exists");
                owned->owner_container_id = edited->id;
                owned->plane_offset = extrusion ? edited->extrusion.profile_plane_offset : edited->revolution.profile_plane_offset;
            }
        }
        next.resolve_constructions(construction_reference_source_geometry(previous));
    }
    auto calculated = calculate_part_with_resolved_references(kernel, next, &previous, policy);
    if (const auto* edited = next.find_container(owner);
        edited && edited->feature_kind == document::FeatureKind::ShaftThread) {
        const auto operations = next.kernel_operations();
        bool exact = calculated.size() == operations.size();
        for (std::size_t i = 0; exact && i < calculated.size(); ++i)
            exact = calculated[i].source_fingerprint == kernel::history_fingerprint(operations, i + 1);
        if (!exact) calculated = calculate_part(kernel, next, &calculated, policy);
    }
    part.session.commit(std::move(next), std::move(calculated));
}
bool commit_construction(Workspace& live, const std::string& document_id,
    document::ConstructionObject value, ConstructionEditMode mode) {
    if (value.kind == document::ConstructionKind::Axis)
        value.direction = document::construction_direction_from_local_axis(value.direction_axis, value.rotation);
    const auto id = value.id;
    if (auto* part = live.open_part(document_id)) {
        const auto& before = part->session.document();
        const auto* existing = before.find_construction(id);
        check_construction(existing, value, mode);
        auto next = before;
        replace_construction(next, std::move(value), mode);
        auto calculated = part->session.calculated_boundaries();
        next.resolve_constructions(construction_reference_source_geometry(calculated));
        require_resolved(next, id);
        static_cast<void>(refresh_sketch_external_references(next, calculated));
        part->session.commit(std::move(next), std::move(calculated));
        return true;
    }
    auto* assembly = live.open_assembly(document_id);
    if (!assembly) throw std::runtime_error("Assembly is no longer open");
    const auto* existing = assembly->session.document().find_construction(id);
    check_construction(existing, value, mode);
    auto next = assembly->session.document();
    replace_construction(next, std::move(value), mode);
    next.resolve_constructions(); require_resolved(next, id);
    assembly->session.commit(std::move(next)); return true;
}
namespace {
const document::ConstructionObject* standalone_construction(
    const std::vector<document::ConstructionObject>& roots, const std::string& id) {
    for (const auto& root : roots) {
        if (root.id == id) return &root;
        for (const auto& point : root.curve_points) if (point.id == id) return &point;
    }
    return nullptr;
}
document::Placement construction_placement(const document::ConstructionObject& object) {
    document::Placement value;
    value.x = object.origin.x; value.y = object.origin.y; value.z = object.origin.z;
    value.rotation_x = object.rotation.x; value.rotation_y = object.rotation.y; value.rotation_z = object.rotation.z;
    value.absolute_rotation_x = object.absolute_rotation.x;
    value.absolute_rotation_y = object.absolute_rotation.y;
    value.absolute_rotation_z = object.absolute_rotation.z;
    value.rotation_offset_x = object.rotation_offset_x;
    value.rotation_offset_y = object.rotation_offset_y;
    value.rotation_offset_z = object.rotation_offset_z;
    value.orientation_back = object.orientation_back;
    value.orientation_quarter_turns = object.orientation_quarter_turns;
    value.references = object.references; value.reference_valid = object.reference_valid;
    for (const auto& key : object.value_locks)
        if (key.starts_with("placement:")) value.value_locks.insert(key.substr(10));
    return value;
}
PlacementInfo construction_info(const document::ConstructionObject& object,
    const std::string& document, const std::string& body) {
    return {"construction", !object.parent_construction_id.empty() ? "parent_construction"
        : body.empty() ? "document" : "body",
        !object.parent_construction_id.empty() ? object.parent_construction_id
        : body.empty() ? document : body, body, construction_placement(object)};
}
template<class Value> void apply_values(Value& value, const kernel::ViewerReferenceGeometry& geometry,
    const PlacementValuePatch& patch) {
    for (const auto& [key, number] : patch)
        if (!assign_placement_dimension(value, geometry, key, number))
            throw PlacementEditError("parameter_not_editable", "The placement parameter is unknown, constrained or locked.");
}
}
PlacementInfo read_placement(const Workspace& live, const std::string& id, const std::string& object) {
    if (const auto* part = live.open_part(id)) {
        const auto& doc = part->session.document();
        if (const auto* body = doc.body_history.find(object))
            return {"body", "document", id, object, body->scope.placement};
        const auto* body = doc.body_owner_for_object(object);
        const auto body_id = body ? body->scope.id : std::string{};
        if (const auto* feature = doc.find_container(object))
            return {"feature", body ? "body" : "document", body ? body_id : id, body_id, feature->placement};
        if (const auto* value = standalone_construction(doc.constructions, object))
            return construction_info(*value, id, body_id);
    } else if (const auto* assembly = live.open_assembly(id)) {
        if (const auto* value = standalone_construction(assembly->session.document().constructions, object))
            return construction_info(*value, id, {});
    } else throw PlacementEditError("unsupported_document", "Placement operations require an open Part or Assembly.");
    throw PlacementEditError("placement_not_found", "The object has no supported placement in this document.");
}
kernel::ViewerReferenceGeometry placement_edit_geometry(const Workspace& live,
    const std::string& id, const std::string& object) {
    if (const auto* state = live.open_part(id)) {
        const auto& before = state->session.document();
        auto geometry = part_construction_dimension_geometry(before, state->session.calculated_boundaries());
        if (before.body_history.find(object)) return geometry;
        return before.construction_reference_geometry_for(object, std::move(geometry));
    }
    if (const auto* state = live.open_assembly(id)) {
        const auto& before = state->session.document();
        document::PartDocument carrier;
        carrier.document_id = before.document_id; carrier.constructions = before.constructions;
        auto geometry = before.build_scene().original_references;
        append_reference_geometry(geometry, before.origin_viewer_mesh().original_references);
        return carrier.construction_reference_geometry_for(object, std::move(geometry));
    }
    throw PlacementEditError("unsupported_document", "Placement operations require an open Part or Assembly.");
}
bool set_placement_values(Workspace& live, const kernel::OcctKernel& kernel,
    const std::string& id, const std::string& object, const PlacementValuePatch& patch) {
    if (patch.empty()) throw PlacementEditError("invalid_arguments", "Specify at least one placement parameter.");
    for (const auto& [key, number] : patch)
        if (!std::isfinite(number)) throw PlacementEditError("invalid_arguments", "Placement value must be finite.");
    const auto info = read_placement(live, id, object);
    if (auto* state = live.open_part(id)) {
        const auto& before = state->session.document();
        if (const auto* body = before.body_history.find(info.body)) {
            if (body->derived_copy) throw PlacementEditError("read_only_body", "A derived Body cannot be edited directly.");
            if (info.kind != "body" && before.body_history.active_body_id() != info.body)
                throw PlacementEditError("inactive_body", "Activate the owning Body before editing its placement.");
        }
        const auto geometry = placement_edit_geometry(live, id, object);
        if (info.kind == "construction") {
            auto value = *standalone_construction(before.constructions, object);
            apply_values(value, geometry, patch);
            if (construction_placement(value) == info.placement) return false;
            return commit_construction(live, id, std::move(value), ConstructionEditMode::Replace);
        }
        auto next = before;
        if (info.kind == "body") {
            auto body = *next.body_history.find(object);
            apply_values(body.scope.placement, geometry, patch);
            if (body.scope.placement == info.placement) return false;
            next.body_history.update_body(std::move(body));
        } else {
            auto* value = next.find_container(object);
            apply_values(value->placement, geometry, patch);
            if (value->placement == info.placement) return false;
        }
        PartCalculationPolicy policy; policy.reject_errors = true;
        if (info.kind == "feature") {
            policy.edited_document_id = id; policy.edited_history_limit = before.history_index(object);
        }
        commit_part_parameter_edit(*state, kernel, std::move(next), object, true, policy);
        return true;
    }
    auto* state = live.open_assembly(id);
    const auto& before = state->session.document();
    auto value = *standalone_construction(before.constructions, object);
    apply_values(value, placement_edit_geometry(live, id, object), patch);
    if (construction_placement(value) == info.placement) return false;
    return commit_construction(live, id, std::move(value), ConstructionEditMode::Replace);
}
} // namespace zima::workspace
