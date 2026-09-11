#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


bool AssemblyWorkspaceWindow::finish_active_reference_selection() {
    if(feature_reference_end_&&properties_dialog_){feature_reference_end_();return true;}
    if (shaft_thread_dialog_ != nullptr) { shaft_thread_dialog_->end_reference_entry();return true; }
    if (finish_drill_point_face_selection()) return true;
    if (finish_shell_face_selection()) return true;
    const bool handled = extrusion_target_dialog_ != nullptr ||
        primitive_reference_dialog_ != nullptr ||
        construction_reference_dialog_ != nullptr ||
        component_placement_dialog_ != nullptr || curve_axis_dialog_ != nullptr;
    if (!handled) return false;

    if (extrusion_target_dialog_ != nullptr)
        extrusion_target_dialog_->finish_extrusion_target_entry();
    extrusion_target_dialog_ = nullptr;
    extrusion_target_assembly_cut_ = false;

    pending_primitive_reference_index_.reset();
    primitive_reference_auto_advance_ = false;
    if (primitive_reference_dialog_ != nullptr) {
        primitive_reference_dialog_->set_active_reference_index(std::nullopt);
        primitive_reference_dialog_->clear_reference_highlights();
    }

    pending_construction_reference_index_.reset();
    construction_reference_auto_advance_ = false;
    if (construction_reference_dialog_ != nullptr) {
        construction_reference_dialog_->set_active_reference_index(std::nullopt);
        construction_reference_dialog_->clear_reference_highlights();
    }
    if (curve_axis_dialog_ != nullptr) {
        curve_axis_dialog_->set_curve_axis_active(std::nullopt);
    }
    pending_curve_axis_index_.reset();
    curve_axis_dialog_ = nullptr;

    pending_component_placement_index_.reset();
    component_placement_auto_advance_ = false;
    if (component_placement_dialog_ != nullptr) {
        component_placement_dialog_->set_active_reference_cell(std::nullopt);
        component_placement_dialog_->clear_reference_highlights();
    }

    if (primitive_reference_dialog_ != nullptr) {
        set_primitive_properties_dimension_selection();
    } else if (construction_reference_dialog_ != nullptr) {
        set_construction_properties_dimension_selection();
    } else {
        tree_->setProperty("commandSelectionActive", false);
        viewer_->clear_selection();
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
    }
    viewer_->set_constraint_reference_highlights({}, {});
    state_->setText(tr("Zadávání referencí ukončeno."));
    return true;
}

void AssemblyWorkspaceWindow::set_construction_properties_dimension_selection() {
    tree_->setProperty("commandSelectionActive", false);
    viewer_->clear_selection();
    // All construction parameter dimensions edit the corresponding live field.
    // This contract offers annotations only, never placement references.
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Dimension});
    viewer_->set_candidate_filter([this](const auto& candidate) {
        return construction_reference_dialog_ != nullptr &&
            candidate.kind == zima::viewer::CandidateKind::Dimension &&
            ((candidate.semantic_key.starts_with("parameter:") &&
              construction_reference_dialog_->owns_reference_owner(candidate.owner_id)) ||
             ((candidate.semantic_key.starts_with("dimension:")||candidate.semantic_key.starts_with("corner_dimension:")) &&
              std::ranges::any_of(viewer_->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==candidate.owner_id&&dimension.reference.semantic_key==candidate.semantic_key;})));
    });
}

void AssemblyWorkspaceWindow::set_primitive_properties_dimension_selection() {
    tree_->setProperty("commandSelectionActive", false);
    viewer_->clear_selection();
    if (primitive_reference_dialog_ == nullptr ||
        primitive_parameter_owner_id_.empty()) {
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter([](const auto&) { return false; });
        return;
    }
    const auto owner_id = primitive_parameter_owner_id_;
    std::set<std::pair<std::string,std::string>> sketches;
    for(const auto& dimension:viewer_->mesh().dimensions)if(dimension.reference.semantic_key.starts_with("dimension:")||dimension.reference.semantic_key.starts_with("corner_dimension:"))
        sketches.emplace(dimension.reference.owner_id,dimension.reference.semantic_key);
    viewer_->set_selection_contract(
        {zima::viewer::CandidateKind::Dimension});
    viewer_->set_candidate_filter(
        [owner_id,sketches](const zima::viewer::ViewerCandidate& candidate) {
            return candidate.kind ==
                    zima::viewer::CandidateKind::Dimension &&
                ((candidate.owner_id == owner_id &&
                 (candidate.semantic_key.starts_with("parameter:") ||
                  candidate.semantic_key.starts_with("measurement:"))) ||
                 sketches.contains({candidate.owner_id,candidate.semantic_key}));
        });
}

bool AssemblyWorkspaceWindow::placement_origin_allowed(const std::string& owner_id) const {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!part || section_dialog_ || dynamic_cast<BodyPropertiesDialog*>(primitive_reference_dialog_)) return true;
    const auto& document = part->session.document();
    const bool body_feature = !document.body_history.active_body_id().empty() ||
        document.body_owner_for_object(primitive_parameter_owner_id_) ||
        document.body_owner_for_object(construction_dimension_object_id_);
    return !body_feature || owner_id != document.document_id + ":origin";
}

zima::kernel::ViewerMesh AssemblyWorkspaceWindow::active_part_origins(
        const zima::document::PartDocument& document) const {
    zima::kernel::ViewerMesh result;
    const auto& active = document.body_history.active_body_id();
    const auto* sketch = active_sketch();
    const bool owned_sketch = sketch && !sketch->owner_container_id.empty();
    if (owned_sketch) {
        const auto& owner_id = sketch->owner_container_id;
        auto origins = document.history_origin_reference_geometry_before({});
        if (!document.find_container(owner_id) && pending_profile_feature_ &&
            pending_profile_feature_->id == owner_id) {
            zima::document::PartDocument carrier;
            carrier.history.push_back(*pending_profile_feature_);
            auto pending = local_origin_display_mesh(
                carrier.history_origin_reference_geometry_before({}), {owner_id + ":origin"});
            if (const auto* body = sketch_body(*sketch))
                pending = document.place_body_mesh(std::move(pending), body->scope.id);
            append_mesh(result, std::move(pending));
        } else {
            append_mesh(result, local_origin_display_mesh(origins, {owner_id + ":origin"}));
        }
    }
    if (!owned_sketch && (active.empty() || section_dialog_ || dynamic_cast<BodyPropertiesDialog*>(primitive_reference_dialog_))) {
        auto origin = document.origin_viewer_mesh();
        // Presentation extents only. Frame coordinates and persisted identities
        // remain unchanged; Body and container origins keep their existing size.
        const auto scale = [](auto& point) { point.x *= 1.25; point.y *= 1.25; point.z *= 1.25; };
        for (auto& edge : origin.edges) for (auto& point : edge.points) scale(point);
        for (auto& axis : origin.axes) axis.display_length *= 1.25;
        for (auto& point : origin.original_references.vertices) scale(point);
        for (auto& axis : origin.original_references.axes) axis.display_length *= 1.25;
        append_mesh(result, std::move(origin));
    }
    const auto& source = body_dialog_preview_ ? *body_dialog_preview_ : document;
    for (const auto& body : source.body_history.bodies()) {
        const auto& id = body.scope.id;
        if ((owned_sketch || (id != active && id != body_dialog_step_id_)) &&
            !visible_local_origin_ids_.contains(id + ":origin")) continue;
        append_mesh(result, source.place_body_mesh(
            source.body_document(id).origin_viewer_mesh(), id));
    }
    return result;
}

zima::kernel::ViewerMesh AssemblyWorkspaceWindow::selected_container_origins(
    const zima::document::PartDocument& document) const {
    return local_origin_display_mesh(
        document.history_origin_reference_geometry_before({}), visible_local_origin_ids_);
}

void AssemblyWorkspaceWindow::bind_local_origin_selection(QDialog* dialog) {
    for (auto* object : dialog->findChildren<QObject*>()) {
        if (auto* section = dynamic_cast<zima::ui::ContainerPlacementSection*>(object)) {
            section->set_reference_label_resolver([this, dialog](const auto& reference) {
                return reference_display_label(reference,
                    dialog == dynamic_cast<QDialog*>(construction_reference_dialog_)
                        ? construction_reference_geometry_ : primitive_reference_geometry_);
            });
        }
    }
    auto* properties = dynamic_cast<zima::ui::PropertiesSubWindow*>(dialog);
    if (!properties || dialog->property("originSelectionBound").toBool()) return;
    dialog->setProperty("originSelectionBound", true);
    auto* button = properties->ensure_origin_selection_button();
    connect(button, &QPushButton::toggled, this, [this, dialog, button](bool active) {
        local_origin_selection_button_ = button;
        local_origin_selection_owner_ = dialog;
        local_origin_selection_dialog_ = dynamic_cast<PlacementReferenceDialog*>(dialog);
        set_local_origin_selection_mode(active);
    });
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (local_origin_selection_owner_ != dialog) return;
        set_local_origin_selection_mode(false);
        local_origin_selection_button_.clear();
        local_origin_selection_owner_ = nullptr;
        local_origin_selection_dialog_ = nullptr;
        visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();
        selectable_local_origin_container_ids_.clear();
        origin_suspended_candidate_filter_ = {};
        origin_suspended_selection_contract_.clear();
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (local_origin_selection_owner_ != dialog) return;
        // Direct parent-window destruction need not emit QDialog::finished.
        // Retire the owner without calling its now-destroyed interface.
        local_origin_selection_active_ = false;
        local_origin_selection_owner_ = nullptr;
        local_origin_selection_dialog_ = nullptr;
        local_origin_selection_button_.clear();
        visible_local_origin_ids_.clear();visible_occurrence_origin_paths_.clear();
        selectable_local_origin_container_ids_.clear();
        suspended_primitive_reference_index_.reset();
        suspended_construction_reference_index_.reset();
        origin_suspended_candidate_filter_ = {};
        origin_suspended_selection_contract_.clear();
    });
}

void AssemblyWorkspaceWindow::set_local_origin_selection_mode(bool active) {
    if (local_origin_selection_active_ == active) return;
    local_origin_selection_active_ = active;
    if (local_origin_selection_button_) {
        const QSignalBlocker blocked(local_origin_selection_button_);
        local_origin_selection_button_->setChecked(active);
    }
    if (local_origin_selection_dialog_)
        local_origin_selection_dialog_->set_origin_selection_mode_active(active);
    if (active) {
        origin_suspended_selection_contract_ = viewer_->selection_contract();
        origin_suspended_candidate_filter_ = viewer_->candidate_filter();
        origin_suspended_advance_on_hover_ = viewer_->advances_selection_on_hover();
        origin_suspended_tree_command_ = tree_->property("commandSelectionActive").toBool();
        suspended_primitive_reference_index_ = pending_primitive_reference_index_;
        suspended_primitive_reference_auto_advance_ = primitive_reference_auto_advance_;
        suspended_construction_reference_index_ = pending_construction_reference_index_;
        suspended_construction_reference_auto_advance_ = construction_reference_auto_advance_;
        pending_primitive_reference_index_.reset();
        pending_construction_reference_index_.reset();
        if (primitive_reference_dialog_)
            primitive_reference_dialog_->set_active_reference_index(std::nullopt);
        if (construction_reference_dialog_)
            construction_reference_dialog_->set_active_reference_index(std::nullopt);
        selectable_local_origin_container_ids_.clear();
        if (const auto* part = workspace_.open_part(workspace_.active_document_id())) {
            for (const auto& container : part->session.document().history)
                selectable_local_origin_container_ids_.insert(container.id);
            for (const auto& body : part->session.document().body_history.bodies())
                selectable_local_origin_container_ids_.insert(body.scope.id);
        }
        tree_->setProperty("commandSelectionActive", true);
        std::vector kinds{zima::viewer::CandidateKind::Container};
        const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
        if (assembly && !assembly->session.document().components.empty())
            kinds.push_back(zima::viewer::CandidateKind::Occurrence);
        viewer_->set_selection_contract(std::move(kinds));
        viewer_->set_candidate_filter([this](const auto& candidate) {
            return (candidate.kind == zima::viewer::CandidateKind::Occurrence &&
                    !candidate.instance_path.empty()) ||
                (candidate.instance_path == active_occurrence_path_ &&
                 selectable_local_origin_container_ids_.contains(candidate.owner_id));
        });
        state_->setText(tr("POČÁTEK: kliknutím zobrazte nebo skryjte počátek dílu, podsestavy či kontejneru."));
        return;
    }
    tree_->setProperty("commandSelectionActive", false);
    viewer_->clear_selection();
    if (suspended_primitive_reference_index_ && primitive_reference_dialog_) {
        const auto index = *suspended_primitive_reference_index_;
        const bool automatic = suspended_primitive_reference_auto_advance_;
        suspended_primitive_reference_index_.reset();
        start_primitive_reference_selection(index, automatic);
    } else if (suspended_construction_reference_index_ && construction_reference_dialog_) {
        const auto index = *suspended_construction_reference_index_;
        const bool automatic = suspended_construction_reference_auto_advance_;
        suspended_construction_reference_index_.reset();
        start_construction_reference_selection(index, automatic);
    } else {
        viewer_->set_selection_contract(origin_suspended_selection_contract_);
        viewer_->set_candidate_filter(origin_suspended_candidate_filter_, origin_suspended_advance_on_hover_);
        tree_->setProperty("commandSelectionActive", origin_suspended_tree_command_);
    }
}

void AssemblyWorkspaceWindow::toggle_local_origin_visibility(
        const zima::viewer::ViewerCandidate& candidate) {
    if (!local_origin_selection_active_) return;
    if (candidate.kind == zima::viewer::CandidateKind::Occurrence &&
        !candidate.instance_path.empty()) {
        const auto address = workspace_.resolve_occurrence(workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(candidate.instance_path));
        if (!address) return;
        if (!visible_occurrence_origin_paths_.erase(candidate.instance_path))
            visible_occurrence_origin_paths_.insert(candidate.instance_path);
        preserve_view_on_refresh_ = true;
        refresh_scene();
        return;
    }
    if (!selectable_local_origin_container_ids_.contains(candidate.owner_id)) return;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    const auto& document = part->session.document();
    const auto* container = document.find_container(candidate.owner_id);
    const auto* body = document.body_history.find(candidate.owner_id);
    if (!container && !body) return;
    const auto id = body ? body->origin().id : container->container_origin.id;
    if (!visible_local_origin_ids_.erase(id)) visible_local_origin_ids_.insert(id);
    preserve_view_on_refresh_ = true;
    refresh_scene();
}


void AssemblyWorkspaceWindow::start_construction_reference_selection(
    std::size_t index, bool auto_advance) {
    if (construction_reference_dialog_ == nullptr) return;
    // A construction placement field and a Curve Point direction field share
    // one Viewer candidate stream. Switching to a placement/reference field
    // must retire the old axis mode first; otherwise hover follows the new
    // contract while LMB is intercepted by accept_curve_axis_reference().
    if (curve_axis_dialog_ != nullptr) {
        curve_axis_dialog_->set_curve_axis_active(std::nullopt);
        curve_axis_dialog_ = nullptr;
        pending_curve_axis_index_.reset();
    }
    construction_reference_auto_advance_ = auto_advance;
    construction_reference_dialog_->set_active_reference_index(index);
    const bool orientation_reference = index >= 3;
    const auto baseline_references =
        construction_reference_dialog_->references_without(index);
    const zima::kernel::Vec3 orientation_origin =
        construction_parameter_preview_
            ? construction_parameter_preview_->origin : zima::kernel::Vec3{};
    const int baseline_translation_dof =
        zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_);
    const int baseline_rotation_dof =
        zima::document::orientation_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_, true,
            orientation_origin);
    const bool direction_reference = !orientation_reference && index < 3 &&
        baseline_translation_dof == 0 && baseline_rotation_dof > 0;
    // A position row (index < 3) is only truly "full" once both translation
    // AND rotation are resolved: a lone point already zeroes translation,
    // but the 2nd/3rd point still carries the direction/normal information
    // needed by the "2 points define an axis"/"3 points define a plane"
    // shortcut, so it must stay enterable until rotation is resolved too.
    // An orientation row (index >= 3) must stay armable even when rotation
    // is ALREADY fully resolved by an automatically-derived FRONT role on
    // position row 0/1 (Plane's "1st reference decides orientation"
    // contract) -- otherwise clicking an empty, still-clickable FRONT/TOP
    // row in "Orientace kontejneru" would silently do nothing, matching
    // Python's `_container_orientation_references` table, which is always
    // independently pickable regardless of what row 0 already resolved.
    // A manually clicked FRONT/TOP row remains replaceable even when another
    // reference already resolved the frame.  A position row that follows a
    // fully resolving Point is different: its real remaining rotation rank
    // must be used (3 -> 1 after the first plane, 1 -> 0 after the second).
    // Treating that rank as the constant 1 made auto-advance reject the first
    // valid plane because it appeared not to improve the placement.
    const int baseline_dof = orientation_reference
        ? std::max(1, baseline_rotation_dof)
        : direction_reference ? baseline_rotation_dof
        : baseline_translation_dof + baseline_rotation_dof;
    // A completed placement has no next input. Existing rows can still be
    // armed because replacing one reference is evaluated with that row removed.
    const bool shortcut_satisfied = !orientation_reference &&
        construction_shortcut_satisfied(
            construction_reference_dialog_->construction_kind(),
            baseline_references, construction_reference_geometry_);
    const bool axis_extent_row = !orientation_reference && index < 3 &&
        construction_reference_dialog_->construction_kind() ==
            zima::document::ConstructionKind::Axis &&
        baseline_references.size() == 2;
    if (auto_advance && (baseline_dof == 0 || shortcut_satisfied) &&
        !axis_extent_row) {
        pending_construction_reference_index_.reset();
        construction_reference_auto_advance_ = false;
        construction_reference_dialog_->set_active_reference_index(std::nullopt);
        tree_->setProperty("commandSelectionActive", false);
        set_construction_properties_dimension_selection();
        viewer_->clear_selection();
        state_->setText(tr("Konstrukce je již plně určená."));
        return;
    }
    pending_construction_reference_index_ = index;
    tree_->setProperty("commandSelectionActive", true);
    // Every container consumes the same complete persisted candidate universe.
    // The active container contract interprets a candidate after hover/RMB
    // cycling instead of prematurely hiding valid placement references here.
    viewer_->set_selection_contract(placement_reference_candidate_kinds());
    const auto prefix = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    const bool active_part =
        workspace_.open_part(workspace_.active_document_id()) != nullptr;
    std::set<std::string> unavailable_construction_owners;
    const auto collect_unavailable = [&](const auto& document) {
        bool at_or_after_edited = false;
        for (const auto& object : document.constructions) {
            if (object.id == construction_reference_dialog_->construction_id())
                at_or_after_edited = true;
            if (!at_or_after_edited) continue;
            unavailable_construction_owners.insert(object.id);
            unavailable_construction_owners.insert(object.entity_id);
            unavailable_construction_owners.insert(object.container_origin.id);
        }
    };
    if (const auto* part = workspace_.open_part(workspace_.active_document_id()))
        collect_unavailable(part->session.document());
    else if (const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id()))
        collect_unavailable(assembly->session.document());
    viewer_->set_candidate_filter([this, prefix, active_part, index,
            orientation_reference, direction_reference, orientation_origin,
            baseline_references, baseline_dof, axis_extent_row,
            unavailable_construction_owners, auto_advance](const auto& candidate) {
        if (construction_reference_dialog_ == nullptr ||
            !placement_origin_allowed(candidate.owner_id)) return false;
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            (candidate.owner_id == construction_reference_dialog_->construction_id() ||
             (candidate.semantic_key == "parameter:radius" &&
              construction_reference_dialog_->construction_kind() ==
                  zima::document::ConstructionKind::Curve3D &&
              construction_reference_dialog_->owns_reference_owner(candidate.owner_id))) &&
            candidate.semantic_key.starts_with("parameter:")) return true;
        if (!construction_reference_candidate_passes_static_filters(candidate,
                orientation_reference || direction_reference,
                construction_reference_dialog_->owns_reference_owner(
                    candidate.owner_id),
                unavailable_construction_owners.contains(candidate.owner_id))) {
            return false;
        }
        auto local_path = candidate.instance_path;
        try {
            auto path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            const bool allowed_path = active_part ? path == prefix : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
            if (!allowed_path) return false;
            if (!prefix.occurrence_ids.empty()) {
                path.occurrence_ids.erase(path.occurrence_ids.begin(),
                    path.occurrence_ids.begin() + static_cast<std::ptrdiff_t>(
                        prefix.occurrence_ids.size()));
                local_path = path.encoded();
            }
        } catch (const std::invalid_argument&) {
            return false;
        }
        auto candidate_reference = zima::document::ConstructionReference{
            std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
            candidate_supports_offset(candidate)};
        if (orientation_reference || direction_reference) {
            candidate_reference.orientation_drives_rotation = true;
            candidate_reference.orientation_role = direction_reference
                ? "direction" : index == 3 ? "front" : "top";
            candidate_reference.orientation_only = direction_reference;
            if (direction_reference) candidate_reference.supports_offset = false;
        }
        auto proposed = baseline_references;
        proposed.push_back(std::move(candidate_reference));
        const int proposed_dof = orientation_reference || direction_reference
            ? zima::document::orientation_constraint_remaining_dof(
                proposed, construction_reference_geometry_, true,
                orientation_origin)
            : zima::document::point_constraint_remaining_dof(
                proposed, construction_reference_geometry_);
        // The generic point DOF rank sees the first point as fully fixing
        // translation, but classic construction shortcuts deliberately use
        // subsequent points for orientation: 2 points define an Axis and 3
        // points define a Plane. Keep those persisted vertices hoverable even
        // when the generic rank does not decrease; accept_construction_reference()
        // applies the identical quota before committing the row.
        const auto construction_kind =
            construction_reference_dialog_->construction_kind();
        const bool shortcut_point = !orientation_reference &&
            candidate.kind == zima::viewer::CandidateKind::Vertex &&
            ((construction_kind == zima::document::ConstructionKind::Axis &&
                 proposed.size() <= 2) ||
             (construction_kind == zima::document::ConstructionKind::Plane &&
                 proposed.size() <= 3));
        if (shortcut_point) return true;
        if (axis_extent_row) {
            return candidate.kind == zima::viewer::CandidateKind::Plane ||
                candidate.kind == zima::viewer::CandidateKind::Face;
        }
        return proposed_dof < baseline_dof ||
            (!auto_advance && proposed_dof == baseline_dof);
    });
    state_->setText(tr("Vyberte stabilní geometrii pro definici konstrukčního objektu."));
}

void AssemblyWorkspaceWindow::accept_construction_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!placement_origin_allowed(candidate.owner_id)) return;
    if (construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_) return;
    const std::size_t selected_index = *pending_construction_reference_index_;
    const bool orientation_reference = selected_index >= 3;
    const auto selection_baseline =
        construction_reference_dialog_->references_without(selected_index);
    const zima::kernel::Vec3 orientation_origin =
        construction_parameter_preview_
            ? construction_parameter_preview_->origin : zima::kernel::Vec3{};
    const int selection_translation_dof =
        zima::document::point_constraint_remaining_dof(
            selection_baseline, construction_reference_geometry_);
    const int selection_rotation_dof =
        zima::document::orientation_constraint_remaining_dof(
            selection_baseline, construction_reference_geometry_, true,
            orientation_origin);
    const bool direction_reference = !orientation_reference &&
        selected_index < 3 && selection_translation_dof == 0 &&
        selection_rotation_dof > 0;
    auto local_path = candidate.instance_path;
    if (!active_occurrence_path_.empty()) {
        auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix =
            zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() < prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                path.occurrence_ids.begin())) return;
        path.occurrence_ids.erase(path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        local_path = path.encoded();
    }
    const auto references_current_or_later_construction = [&](const auto& document) {
        bool at_or_after_edited = false;
        for (const auto& object : document.constructions) {
            if (object.id == construction_reference_dialog_->construction_id())
                at_or_after_edited = true;
            if (at_or_after_edited &&
                (candidate.owner_id == object.id ||
                 candidate.owner_id == object.entity_id ||
                 candidate.owner_id == object.container_origin.id)) return true;
        }
        return false;
    };
    const auto active_document_id = workspace_.active_document_id();
    if (const auto* part = workspace_.open_part(active_document_id)) {
        if (references_current_or_later_construction(part->session.document())) return;
    } else if (const auto* assembly = workspace_.open_assembly(active_document_id)) {
        if (references_current_or_later_construction(
                assembly->session.document())) return;
    }
    if (!construction_reference_candidate_passes_static_filters(candidate,
            orientation_reference || direction_reference,
            construction_reference_dialog_->owns_reference_owner(candidate.owner_id),
            false)) {
        return;
    }
    // Every container kind (Point, Axis, Plane) now shares one placement
    // model, matching Placement/resolve_placement() used by primitives and
    // Extrusion/Revolution: placement references are solved generically
    // (any combination/count of point/axis/edge/plane position rows), and
    // any reference marked orientation_drives_rotation (front/top role)
    // additionally composes the object's orientation. There is no more
    // per-kind "named" definition (TwoPointAxis/AxisReference/
    // ThreePointPlane/PlaneReference) that requires an exact reference
    // count/type -- resolve_construction() detects the classic "2 points
    // define an axis"/"3 points define a plane" shortcuts on its own when
    // no orientation-driving reference is present, and otherwise falls back
    // to the generic position-only solve for any other combination.
    const auto kind = construction_reference_dialog_->construction_kind();
    auto baseline_references =
        construction_reference_dialog_->references_without(selected_index);
    const auto definition = zima::document::ConstructionDefinition::PointReference;
    const int baseline_dof = orientation_reference || direction_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_, true,
            orientation_origin)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_);
    auto proposed_reference = zima::document::ConstructionReference{
        local_path, candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    if (orientation_reference || direction_reference) {
        proposed_reference.orientation_drives_rotation = true;
        proposed_reference.orientation_role = direction_reference
            ? "direction" : selected_index == 3 ? "front" : "top";
        proposed_reference.orientation_only = direction_reference;
        if (direction_reference) proposed_reference.supports_offset = false;
    }
    baseline_references.push_back(proposed_reference);
    const int proposed_dof = orientation_reference || direction_reference
        ? zima::document::orientation_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_, true,
            orientation_origin)
        : zima::document::point_constraint_remaining_dof(
            baseline_references, construction_reference_geometry_);
    // A 2nd (Axis) or 2nd/3rd (Plane) plain point reference carries real
    // direction/normal information via the classic history-order shortcut
    // (1st point = origin, 2nd = direction, 3rd = plane-completing point) --
    // point_constraint_remaining_dof() cannot see that, since a single point
    // already zeroes translation and a bare vertex never drives rotation.
    // Such a reference must therefore be accepted even when it does not
    // shrink the generic DOF count, as long as the container still has room
    // for it (2 points for an Axis, 3 for a Plane).
    // baseline_references already includes the just-pushed proposed_reference
    // at this point, so the total-count checks below compare against the
    // post-push size (i.e. <= the shortcut's point quota, not < it).
    const bool is_shortcut_point = !orientation_reference &&
        candidate.kind == zima::viewer::CandidateKind::Vertex &&
        ((kind == zima::document::ConstructionKind::Axis &&
             baseline_references.size() <= 2) ||
         (kind == zima::document::ConstructionKind::Plane &&
             baseline_references.size() <= 3));
    const bool is_axis_extent = !orientation_reference &&
        kind == zima::document::ConstructionKind::Axis &&
        baseline_references.size() == 3 &&
        (candidate.kind == zima::viewer::CandidateKind::Plane ||
         candidate.kind == zima::viewer::CandidateKind::Face);
    // An orientation row (index >= 3) is always accepted as an explicit
    // user override, even when FRONT/TOP is already fully resolved by an
    // automatically-derived role on a position row -- exactly like the
    // `baseline_dof` override in start_construction_reference_selection()
    // above that arms picking for such a row in the first place. Rejecting
    // the pick here via the generic "no independent constraint" DOF check
    // would silently ignore every manual FRONT/TOP selection the user makes
    // once row 0 already supplied a default, matching Python's always-
    // overridable `_container_orientation_references` contract.
    const bool worsens_or_fails_to_improve = proposed_dof > baseline_dof ||
        (construction_reference_auto_advance_ &&
         proposed_dof == baseline_dof);
    if (!orientation_reference && !is_shortcut_point && !is_axis_extent &&
        worsens_or_fails_to_improve) {
        state_->setText(tr("Tato reference nepřidává žádnou nezávislou vazbu."));
        viewer_->clear_selection();
        return;
    }
    QString reference_label;
    const auto label_from_constructions = [&](const auto& document) {
        const auto found = std::find_if(document.constructions.begin(),
            document.constructions.end(), [&](const auto& object) {
                return candidate.owner_id == object.id ||
                    candidate.owner_id == object.entity_id ||
                    candidate.owner_id == object.container_origin.id;
            });
        if (found != document.constructions.end()) {
            reference_label = QString::fromStdString(found->name);
        }
    };
    if (const auto* part = workspace_.open_part(active_document_id)) {
        label_from_constructions(part->session.document());
        if (candidate.owner_id == part->session.document().document_id + ":origin") {
            reference_label = tr("Počátek dílu");
        }
    } else if (const auto* assembly = workspace_.open_assembly(active_document_id)) {
        label_from_constructions(assembly->session.document());
        if (candidate.owner_id == assembly->session.document().document_id + ":origin") {
            reference_label = tr("Počátek sestavy");
        }
    }
    const auto semantic_label = candidate.semantic_key.starts_with("origin:axis:")
        ? tr("Osa %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(12)).toUpper())
        : candidate.semantic_key.starts_with("origin:plane:")
            ? tr("Rovina %1").arg(QString::fromStdString(
                candidate.semantic_key.substr(13)).toUpper())
        : candidate.kind == zima::viewer::CandidateKind::Vertex ? tr("Bod")
        : candidate.kind == zima::viewer::CandidateKind::Axis ? tr("Osa")
        : candidate.kind == zima::viewer::CandidateKind::Edge ? tr("Hrana")
        : candidate.kind == zima::viewer::CandidateKind::Plane ? tr("Rovina")
        : tr("Plocha");
    reference_label = reference_label.isEmpty()
        ? semantic_label : reference_label + QStringLiteral(" — ") + semantic_label;
    if (const auto* part=workspace_.open_part(workspace_.active_document_id())) {
        if (const auto* owner=part->session.document().find_container(candidate.owner_id)) {
            const auto cap_label=zima::document::sweep3d_cap_label(*owner,candidate.semantic_key);
            if (!cap_label.empty()) reference_label=QString::fromStdString(owner->name+" — "+cap_label);
        }
    }
    auto committed_reference = zima::document::ConstructionReference{
        std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    if (direction_reference) {
        committed_reference.orientation_drives_rotation = true;
        committed_reference.orientation_role = "direction";
        committed_reference.orientation_only = true;
        committed_reference.supports_offset = false;
    }
    // Every construction dialog now owns the same independent orientation
    // table. Planar position references are mirrored into its first two
    // slots by ContainerPlacementSection; position rows themselves never
    // acquire a second rotational meaning.
    committed_reference.measured_offset=zima::document::measure_placement_reference_offset(committed_reference,construction_reference_geometry_,orientation_origin);
    const bool auto_advance = construction_reference_auto_advance_;
    if (!construction_reference_dialog_->set_reference(
        selected_index, committed_reference, reference_label, definition)) {
        state_->setText(tr("Stejná reference už je pro tento objekt zadaná."));
        viewer_->clear_selection();
        return;
    }
    if (auto_advance)
        construction_reference_dialog_->set_reference_inspected(
            selected_index, true);
    pending_construction_reference_index_.reset();
    viewer_->clear_selection();
    // set_reference() above already triggered the dialog's preview callback
    // synchronously (via notify_changed()), which itself already called
    // refresh_scene() with preserve_view_on_refresh_ temporarily true --
    // but that call resets the flag back to false once done. Without
    // re-arming it here, this second, redundant refresh_scene() call runs
    // with fit_view enabled and re-fits/zooms the camera to the newly
    // resolved construction geometry (e.g. an Axis/Plane's real display-size
    // extent, which is typically much larger than the small Origin preview
    // shown before the reference was picked). That camera re-fit is what
    // makes the Origin appear to shrink the instant the first reference is
    // entered, even though the Origin's own world-space size never changes.
    preserve_view_on_refresh_ = true;
    refresh_scene();
    if (auto_advance) {
        const auto next =
            construction_reference_dialog_->first_empty_position_index();
        if (next < 3) {
            start_construction_reference_selection(next, true);
            return;
        }
    }
    construction_reference_auto_advance_ = false;
    construction_reference_dialog_->set_active_reference_index(std::nullopt);
    tree_->setProperty("commandSelectionActive", false);
    set_construction_properties_dimension_selection();
    viewer_->clear_selection();
}

void AssemblyWorkspaceWindow::start_primitive_reference_selection(
    std::size_t index, bool auto_advance) {
    if (primitive_reference_dialog_ == nullptr) return;
    if (extrusion_target_dialog_ != nullptr)
        finish_extrusion_target_selection();
    primitive_reference_auto_advance_ = auto_advance;
    primitive_reference_dialog_->set_active_reference_index(index);
    const bool orientation_reference = index >= 3;
    const auto baseline_references =
        primitive_reference_dialog_->references_without(index);
    zima::document::Placement baseline_placement;
    baseline_placement.references = baseline_references;
    static_cast<void>(zima::document::resolve_placement(
        baseline_placement, primitive_reference_geometry_));
    const zima::kernel::Vec3 orientation_origin{baseline_placement.x,
        baseline_placement.y, baseline_placement.z};
    const int baseline_translation_dof =
        zima::document::point_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_);
    const int baseline_rotation_dof =
        zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, true,
            orientation_origin);
    const bool direction_reference = !orientation_reference && index < 3 &&
        baseline_translation_dof == 0 && baseline_rotation_dof > 0;
    const int baseline_dof = orientation_reference
        ? std::max(1, baseline_rotation_dof)
        : direction_reference ? baseline_rotation_dof
        : baseline_translation_dof + baseline_rotation_dof;
    if (auto_advance && baseline_dof == 0) {
        pending_primitive_reference_index_.reset();
        primitive_reference_auto_advance_ = false;
        primitive_reference_dialog_->set_active_reference_index(std::nullopt);
        set_primitive_properties_dimension_selection();
        state_->setText(tr("Umístění kontejneru je již plně určené."));
        return;
    }
    pending_primitive_reference_index_ = index;
    tree_->setProperty("commandSelectionActive", true);
    viewer_->set_selection_contract(placement_reference_candidate_kinds());
    const auto prefix = active_occurrence_path_.empty()
        ? zima::assembly::InstancePath{}
        : zima::assembly::InstancePath::decode(active_occurrence_path_);
    const bool active_part =
        workspace_.open_part(workspace_.active_document_id()) != nullptr;
    viewer_->set_candidate_filter([this, prefix, active_part, index,
            orientation_reference, direction_reference, orientation_origin,
            baseline_references, baseline_dof, auto_advance](
                const auto& candidate) {
        if (candidate.kind == zima::viewer::CandidateKind::Dimension &&
            candidate.owner_id == construction_dimension_object_id_ &&
            candidate.semantic_key.starts_with("parameter:")) return true;
        if (!placement_origin_allowed(candidate.owner_id)) return false;
        if (!placement_reference_candidate_has_stable_geometry(candidate)) return false;
        if (primitive_reference_dialog_ == nullptr ||
            primitive_reference_dialog_->owns_reference_owner(candidate.owner_id))
            return false;
        auto local_path = candidate.instance_path;
        try {
            auto path = zima::assembly::InstancePath::decode(
                candidate.instance_path);
            const bool allowed_path = active_part ? path == prefix : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
            if (!allowed_path) return false;
            if (!prefix.occurrence_ids.empty()) {
                path.occurrence_ids.erase(path.occurrence_ids.begin(),
                    path.occurrence_ids.begin() + static_cast<std::ptrdiff_t>(
                        prefix.occurrence_ids.size()));
                local_path = path.encoded();
            }
        } catch (const std::invalid_argument&) {
            return false;
        }
        auto candidate_reference = zima::document::ConstructionReference{
            std::move(local_path), candidate.owner_id, candidate.semantic_key, 0.0,
            candidate_supports_offset(candidate)};
        if (orientation_reference || direction_reference) {
            if (direction_reference &&
                !placement_reference_candidate_can_define_direction(candidate))
                return false;
            candidate_reference.orientation_drives_rotation = true;
            candidate_reference.orientation_role = direction_reference
                ? "direction" : index == 3 ? "front" : "top";
            candidate_reference.orientation_only = direction_reference;
            if (direction_reference) candidate_reference.supports_offset = false;
        } else if (candidate_drives_rotation(candidate)) {
            assign_automatic_orientation_role(
                candidate_reference, baseline_references);
        }
        auto proposed = baseline_references;
        proposed.push_back(std::move(candidate_reference));
        const int proposed_rotation =
            zima::document::orientation_constraint_remaining_dof(
                proposed, primitive_reference_geometry_, true,
                orientation_origin);
        const int proposed_dof = orientation_reference || direction_reference
            ? proposed_rotation
            : proposed_rotation +
                zima::document::point_constraint_remaining_dof(
                    proposed, primitive_reference_geometry_);
        return proposed_dof < baseline_dof ||
            (!auto_advance && proposed_dof == baseline_dof);
    });
    state_->setText(tr("Vyberte stabilní geometrii pro umístění kontejneru."));
}

bool AssemblyWorkspaceWindow::component_placement_reference_candidate_allowed(
    const zima::viewer::ViewerCandidate& candidate, bool match_other_side) const {
    using zima::viewer::CandidateKind;
    if (!component_placement_dialog_ || !pending_component_placement_index_ ||
        !placement_reference_candidate_has_stable_geometry(candidate) ||
        candidate.owner_id.empty() || candidate.semantic_key.empty()) return false;
    if (candidate.kind != CandidateKind::Vertex && candidate.kind != CandidateKind::Axis &&
        candidate.kind != CandidateKind::Face && candidate.kind != CandidateKind::Plane) return false;
    const auto kind = candidate.kind == CandidateKind::Vertex ? zima::assembly::MateReferenceKind::Point :
        candidate.kind == CandidateKind::Axis ? zima::assembly::MateReferenceKind::Axis : zima::assembly::MateReferenceKind::Face;
    const auto& rows = component_placement_dialog_->placement_references();
    if (match_other_side && !pending_component_placement_component_side_ && *pending_component_placement_index_ < rows.size()) {
        const auto& source = rows[*pending_component_placement_index_].component_reference;
        if (!source.owner_id.empty() && source.kind != kind) return false;
    }
    try {
        const auto prefix = zima::assembly::InstancePath::decode(properties_dialog_instance_path_)
            .parent().value_or(zima::assembly::InstancePath{});
        const auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        if (path == prefix) return !pending_component_placement_component_side_;
        if (path.occurrence_ids.size() != prefix.occurrence_ids.size() + 1 ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(), path.occurrence_ids.begin())) return false;
        const bool own = path.occurrence_ids.back() == component_placement_occurrence_id_;
        return pending_component_placement_component_side_ ? own : !own;
    } catch (const std::invalid_argument&) { return false; }
}

void AssemblyWorkspaceWindow::start_component_placement_reference_selection(
    std::size_t index, bool component_side, bool auto_advance) {
    if (component_placement_dialog_ == nullptr) return;
    component_placement_auto_advance_ = auto_advance;
    pending_component_placement_index_ = index;
    pending_component_placement_component_side_ = component_side;
    component_placement_dialog_->set_active_reference_cell(index, component_side);
    tree_->setProperty("commandSelectionActive", true);
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Vertex,
        zima::viewer::CandidateKind::Axis, zima::viewer::CandidateKind::Plane,
        zima::viewer::CandidateKind::Face});
    viewer_->set_candidate_filter([this](const auto& candidate) {
        return component_placement_reference_candidate_allowed(candidate);
    });
    state_->setText(component_side
        ? tr("Vyberte referenci na tomto dílu.")
        : tr("Vyberte cílovou referenci."));
}

void AssemblyWorkspaceWindow::accept_component_placement_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!component_placement_reference_candidate_allowed(candidate)) return;
    const auto kind = candidate.kind == zima::viewer::CandidateKind::Axis
        ? zima::assembly::MateReferenceKind::Axis
        : candidate.kind == zima::viewer::CandidateKind::Vertex
            ? zima::assembly::MateReferenceKind::Point : zima::assembly::MateReferenceKind::Face;
    auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
    const auto prefix = zima::assembly::InstancePath::decode(properties_dialog_instance_path_)
        .parent().value_or(zima::assembly::InstancePath{});
    path.occurrence_ids.erase(path.occurrence_ids.begin(), path.occurrence_ids.begin() +
        static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
    const auto local_path = path.encoded();
    const std::size_t selected_index = *pending_component_placement_index_;
    const bool component_side = pending_component_placement_component_side_;
    zima::assembly::MateReference reference{
        kind, zima::assembly::InstancePath::decode(local_path),
        candidate.owner_id, candidate.semantic_key};
    const auto semantic_label = candidate.kind == zima::viewer::CandidateKind::Vertex
        ? tr("Bod") : candidate.kind == zima::viewer::CandidateKind::Axis
            ? tr("Osa") : tr("Plocha");
    // Preserve the nearest orientation when a new pair is picked, but persist
    // that choice in Flip. Subsequent toggles/recalculations use its absolute
    // value and can therefore return from the opposite orientation reliably.
    std::optional<bool> initial_flip;
    const auto& existing=component_placement_dialog_->placement_references();
    if (selected_index<existing.size() && kind!=zima::assembly::MateReferenceKind::Point) {
        const auto other=component_side?existing[selected_index].target_reference:existing[selected_index].component_reference;
        const auto* assembly=workspace_.open_assembly(component_placement_assembly_document_id_);
        if (assembly && !other.owner_id.empty() && other.kind==kind) {
            auto geometry=assembly->session.document();
            *geometry.find_occurrence(component_placement_occurrence_id_)=component_placement_dialog_->pending_value();
            const auto source=component_side?reference:other,target=component_side?other:reference;
            if (kind==zima::assembly::MateReferenceKind::Axis) {
                const auto a=geometry.resolve_axis(source),b=geometry.resolve_axis(target);
                if(a.status==zima::assembly::MateStatus::Valid && b.status==zima::assembly::MateStatus::Valid)
                    initial_flip=a.axis.direction.x*b.axis.direction.x+a.axis.direction.y*b.axis.direction.y+a.axis.direction.z*b.axis.direction.z<0;
            } else {
                const auto a=geometry.resolve_plane(source),b=geometry.resolve_plane(target);
                if(a.status==zima::assembly::MateStatus::Valid && b.status==zima::assembly::MateStatus::Valid)
                    initial_flip=a.plane.normal.x*b.plane.normal.x+a.plane.normal.y*b.plane.normal.y+a.plane.normal.z*b.plane.normal.z<0;
            }
        }
    }
    component_placement_dialog_->set_placement_reference(
        selected_index, component_side, std::move(reference), semantic_label, initial_flip);
    const bool auto_advance = component_placement_auto_advance_;
    if (auto_advance)
        component_placement_dialog_->set_reference_inspected(
            selected_index, component_side, true);
    pending_component_placement_index_.reset();
    viewer_->clear_selection();
    // Auto-advance the picking flow, matching Python's _advance_pick /
    // _activate_pick chain: after the component-side cell of a row is
    // filled, arm the row's target-side cell next; once both sides of a row
    // are filled, arm the next row's component-side cell (up to 3 rows).
    // The user is never made to hunt for the next cell to click.
    const auto& rows = component_placement_dialog_->placement_references();
    const bool row_target_filled = selected_index < rows.size() &&
        !rows[selected_index].target_reference.owner_id.empty();
    const bool row_component_filled = selected_index < rows.size() &&
        !rows[selected_index].component_reference.owner_id.empty();
    if (auto_advance) {
        if (component_side && !row_target_filled) {
            start_component_placement_reference_selection(
                selected_index, false, true);
            return;
        }
        if (!component_side && !row_component_filled) {
            start_component_placement_reference_selection(
                selected_index, true, true);
            return;
        }
        if (selected_index + 1 < 3) {
            start_component_placement_reference_selection(
                selected_index + 1, true, true);
            return;
        }
    }
    component_placement_auto_advance_ = false;
    component_placement_dialog_->set_active_reference_cell(std::nullopt);
    tree_->setProperty("commandSelectionActive", false);
    viewer_->set_selection_contract({});
    viewer_->set_candidate_filter([](const auto&) { return false; });
    state_->setText(tr("Reference zadána."));
}

void AssemblyWorkspaceWindow::accept_primitive_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (!placement_origin_allowed(candidate.owner_id)) return;
    if (primitive_reference_dialog_ == nullptr ||
        !pending_primitive_reference_index_ || candidate.owner_id.empty() ||
        candidate.semantic_key.empty() ||
        primitive_reference_dialog_->owns_reference_owner(candidate.owner_id) ||
        !placement_reference_candidate_has_stable_geometry(candidate)) return;
    auto local_path = candidate.instance_path;
    if (!active_occurrence_path_.empty()) {
        auto path = zima::assembly::InstancePath::decode(candidate.instance_path);
        const auto prefix =
            zima::assembly::InstancePath::decode(active_occurrence_path_);
        if (path.occurrence_ids.size() < prefix.occurrence_ids.size() ||
            !std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                path.occurrence_ids.begin())) return;
        path.occurrence_ids.erase(path.occurrence_ids.begin(),
            path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
        local_path = path.encoded();
    }
    const std::size_t selected_index = *pending_primitive_reference_index_;
    const bool orientation_reference = selected_index >= 3;
    auto baseline_references =
        primitive_reference_dialog_->references_without(selected_index);
    zima::document::Placement baseline_placement;
    baseline_placement.references = baseline_references;
    static_cast<void>(zima::document::resolve_placement(
        baseline_placement, primitive_reference_geometry_));
    const zima::kernel::Vec3 orientation_origin{baseline_placement.x,
        baseline_placement.y, baseline_placement.z};
    const int baseline_translation_dof =
        zima::document::point_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_);
    const int baseline_rotation_dof =
        zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, true,
            orientation_origin);
    const bool direction_reference = !orientation_reference &&
        selected_index < 3 && baseline_translation_dof == 0 &&
        baseline_rotation_dof > 0;
    const int baseline_dof = orientation_reference
        ? std::max(1, baseline_rotation_dof)
        : direction_reference ? baseline_rotation_dof
        : baseline_translation_dof + baseline_rotation_dof;
    if (direction_reference &&
        !placement_reference_candidate_can_define_direction(candidate)) {
        state_->setText(tr(
            "Po bodu vyberte další bod, rovinu, plochu, osu nebo hranu pro orientaci."));
        viewer_->clear_selection();
        return;
    }
    auto proposed_reference = zima::document::ConstructionReference{
        local_path, candidate.owner_id, candidate.semantic_key, 0.0,
        candidate_supports_offset(candidate)};
    if (orientation_reference || direction_reference) {
        proposed_reference.orientation_drives_rotation = true;
        proposed_reference.orientation_role = direction_reference
            ? "direction" : selected_index == 3 ? "front" : "top";
        proposed_reference.orientation_only = direction_reference;
        if (direction_reference) proposed_reference.supports_offset = false;
    } else if (candidate_drives_rotation(candidate)) {
        assign_automatic_orientation_role(
            proposed_reference, baseline_references);
    }
    auto committed_reference = proposed_reference;
    baseline_references.push_back(proposed_reference);
    const int proposed_rotation_dof =
        zima::document::orientation_constraint_remaining_dof(
            baseline_references, primitive_reference_geometry_, true,
            orientation_origin);
    const int proposed_dof = orientation_reference || direction_reference
        ? proposed_rotation_dof
        : proposed_rotation_dof +
            zima::document::point_constraint_remaining_dof(
                baseline_references, primitive_reference_geometry_);
    if (proposed_dof > baseline_dof ||
        (primitive_reference_auto_advance_ && proposed_dof == baseline_dof)) {
        state_->setText(tr("Tato reference nepřidává žádnou nezávislou vazbu."));
        viewer_->clear_selection();
        return;
    }
    QString reference_label;
    const auto label_from_constructions = [&](const auto& document) {
        const auto found = std::find_if(document.constructions.begin(),
            document.constructions.end(), [&](const auto& object) {
                return candidate.owner_id == object.id ||
                    candidate.owner_id == object.entity_id ||
                    candidate.owner_id == object.container_origin.id;
            });
        if (found != document.constructions.end()) {
            reference_label = QString::fromStdString(found->name);
        }
    };
    const auto active_document_id = workspace_.active_document_id();
    if (const auto* part = workspace_.open_part(active_document_id)) {
        label_from_constructions(part->session.document());
        if (candidate.owner_id == part->session.document().document_id + ":origin") {
            reference_label = tr("Počátek dílu");
        }
    }
    const auto semantic_label = candidate.semantic_key.starts_with("origin:axis:")
        ? tr("Osa %1").arg(QString::fromStdString(
            candidate.semantic_key.substr(12)).toUpper())
        : candidate.semantic_key.starts_with("origin:plane:")
            ? tr("Rovina %1").arg(QString::fromStdString(
                candidate.semantic_key.substr(13)).toUpper())
        : candidate.kind == zima::viewer::CandidateKind::Vertex ? tr("Bod")
        : candidate.kind == zima::viewer::CandidateKind::Axis ? tr("Osa")
        : candidate.kind == zima::viewer::CandidateKind::Plane ? tr("Rovina")
        : candidate.kind == zima::viewer::CandidateKind::Edge ? tr("Hrana")
        : tr("Plocha");
    reference_label = reference_label.isEmpty()
        ? semantic_label : reference_label + QStringLiteral(" — ") + semantic_label;
    if (const auto* part=workspace_.open_part(workspace_.active_document_id())) {
        if (const auto* owner=part->session.document().find_container(candidate.owner_id)) {
            const auto cap_label=zima::document::sweep3d_cap_label(*owner,candidate.semantic_key);
            if (!cap_label.empty()) reference_label=QString::fromStdString(owner->name+" — "+cap_label);
        }
    }
    committed_reference.measured_offset=zima::document::measure_placement_reference_offset(committed_reference,primitive_reference_geometry_,orientation_origin);
    const bool auto_advance = primitive_reference_auto_advance_;
    if (!primitive_reference_dialog_->set_reference(
        selected_index, std::move(committed_reference), reference_label)) {
        state_->setText(tr("Stejná reference už je pro toto umístění zadaná."));
        viewer_->clear_selection();
        return;
    }
    if (auto_advance)
        primitive_reference_dialog_->set_reference_inspected(selected_index, true);
    pending_primitive_reference_index_.reset();
    viewer_->clear_selection();
    // set_reference() synchronously publishes the live placement preview and
    // refreshes the scene while preserving the camera. This follow-up refresh
    // only updates the post-pick selection contract; it must not perform an
    // implicit Fit All and make the model jump/shrink after every reference.
    preserve_view_on_refresh_ = true;
    refresh_scene();
    if (auto_advance) {
        const auto next = primitive_reference_dialog_->first_empty_position_index();
        if (next < 3) {
            start_primitive_reference_selection(next, true);
            return;
        }
    }
    primitive_reference_auto_advance_ = false;
    primitive_reference_dialog_->set_active_reference_index(std::nullopt);
    set_primitive_properties_dimension_selection();
}

bool AssemblyWorkspaceWindow::accept_primitive_tree_reference(
    const QTreeWidgetItem* item) {
    if (item == nullptr || primitive_reference_dialog_ == nullptr ||
        !pending_primitive_reference_index_) return false;
    const auto item_kind = item->data(0, Qt::UserRole + 3).toString();
    if (item_kind == QStringLiteral("document-origin") ||
        item_kind == QStringLiteral("construction-origin")) {
        const auto origin_id = item->data(0, Qt::UserRole).toString().toStdString();
        if (!placement_origin_allowed(origin_id)) return false;
        if (primitive_reference_dialog_->owns_reference_owner(origin_id)) return false;
        if (*pending_primitive_reference_index_ >= 3) return false;
        struct CapturedPlaneReference {
            std::string owner_id;
            std::string instance_path;
            std::string semantic_key;
        };
        std::vector<CapturedPlaneReference> planes;
        for (int index = 0; index < item->childCount(); ++index) {
            const auto* child = item->child(index);
            const auto semantic = child->data(0, Qt::UserRole + 5)
                .toString().toStdString();
            if (!semantic.starts_with("origin:plane:") &&
                !semantic.starts_with("plane:")) continue;
            planes.push_back({
                child->data(0, Qt::UserRole + 6).isValid()
                    ? child->data(0, Qt::UserRole + 6).toString().toStdString()
                    : child->data(0, Qt::UserRole).toString().toStdString(),
                child->data(0, Qt::UserRole + 1).toString().toStdString(),
                semantic});
        }
        const auto origin_plane_rank = [](const auto& value) {
            return value.semantic_key.ends_with("plane:xz") ? 0
                : value.semantic_key.ends_with("plane:xy") ? 1 : 2;
        };
        std::ranges::sort(planes, {}, origin_plane_rank);
        bool accepted_any = false;
        for (const auto& plane : planes) {
            const auto row = primitive_reference_dialog_->first_empty_position_index();
            if (row >= 3) break;
            start_primitive_reference_selection(row);
            if (!pending_primitive_reference_index_) break;
            const auto before = primitive_reference_dialog_->first_empty_position_index();
            zima::viewer::ViewerCandidate candidate;
            candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
            candidate.kind = zima::viewer::CandidateKind::Plane;
            candidate.owner_id = plane.owner_id;
            candidate.instance_path = plane.instance_path;
            candidate.semantic_key = plane.semantic_key;
            accept_primitive_reference(candidate);
            accepted_any = accepted_any ||
                primitive_reference_dialog_->first_empty_position_index() != before;
        }
        return accepted_any;
    }
    if (item_kind != QStringLiteral("origin-reference")) return false;
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    candidate.owner_id = item->data(0, Qt::UserRole + 6).isValid()
        ? item->data(0, Qt::UserRole + 6).toString().toStdString()
        : item->data(0, Qt::UserRole).toString().toStdString();
    candidate.semantic_key =
        item->data(0, Qt::UserRole + 5).toString().toStdString();
    candidate.kind = candidate.semantic_key == "origin:point" ||
            candidate.semantic_key == "point"
        ? zima::viewer::CandidateKind::Vertex
        : candidate.semantic_key.starts_with("origin:axis:")
            ? zima::viewer::CandidateKind::Axis
            : zima::viewer::CandidateKind::Plane;
    const auto before = primitive_reference_dialog_->first_empty_position_index();
    accept_primitive_reference(candidate);
    return primitive_reference_dialog_->first_empty_position_index() != before;
}

bool AssemblyWorkspaceWindow::accept_component_placement_tree_reference(
    const QTreeWidgetItem* item) {
    if (item == nullptr || component_placement_dialog_ == nullptr) return false;

    const auto item_kind = item->data(0, Qt::UserRole + 3).toString();
    const auto prefix = zima::assembly::InstancePath::decode(properties_dialog_instance_path_)
        .parent().value_or(zima::assembly::InstancePath{});
    // The owning Assembly origin is a complete placement shortcut even when
    // insertion has armed the source cell, or reference entry has ended.
    if (item_kind == "document-origin" &&
        item->data(0, Qt::UserRole).toString().toStdString() ==
            component_placement_assembly_document_id_ + ":origin" &&
        item->data(0, Qt::UserRole + 1).toString().toStdString() == prefix.encoded()) {
        const auto pending = component_placement_dialog_->pending_value();
        std::vector<zima::assembly::ComponentPlacementReference> rows;
        for (const auto key : {"origin:plane:xy", "origin:plane:yz", "origin:plane:xz"}) {
            rows.push_back({zima::assembly::MateKind::PlaneCoincident,
                {zima::assembly::MateReferenceKind::Face,
                    zima::assembly::InstancePath{}.child(pending.occurrence_id),
                    pending.source_document_id + ":origin", key},
                {zima::assembly::MateReferenceKind::Face, {},
                    component_placement_assembly_document_id_ + ":origin", key}});
        }
        pending_component_placement_index_.reset();
        component_placement_dialog_->set_placement_references(std::move(rows));
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        state_->setText(tr("Roviny počátků XY, YZ a XZ jsou spárovány."));
        return true;
    }
    if (!pending_component_placement_index_) return false;
    const auto selected_row = *pending_component_placement_index_;
    const bool component_side = pending_component_placement_component_side_;

    // A complete origin fills the selected side with its point and X/Y axes.
    // Preserve that exact origin and occurrence; never substitute the source
    // document's origin or silently bind the component to itself.
    if (item_kind == "document-origin" || item_kind == "construction-origin") {
        std::vector<zima::viewer::ViewerCandidate> captured;
        for (const auto wanted : {"origin:point", "origin:axis:x", "origin:axis:y"}) {
            for (int index = 0; index < item->childCount(); ++index) {
                const auto* child = item->child(index);
                const auto semantic = child->data(0, Qt::UserRole + 5).toString().toStdString();
                if (semantic != wanted && !(std::string_view(wanted) == "origin:point" && semantic == "point")) continue;
                zima::viewer::ViewerCandidate candidate;
                candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
                candidate.kind = captured.empty() ? zima::viewer::CandidateKind::Vertex : zima::viewer::CandidateKind::Axis;
                candidate.owner_id = child->data(0, Qt::UserRole + 6).isValid()
                    ? child->data(0, Qt::UserRole + 6).toString().toStdString()
                    : child->data(0, Qt::UserRole).toString().toStdString();
                candidate.semantic_key = semantic;
                candidate.instance_path = child->data(0, Qt::UserRole + 1).toString().toStdString();
                if (!component_placement_reference_candidate_allowed(candidate, false)) return false;
                captured.push_back(std::move(candidate)); break;
            }
        }
        if (captured.size() != 3) return false;
        const auto prefix = zima::assembly::InstancePath::decode(properties_dialog_instance_path_)
            .parent().value_or(zima::assembly::InstancePath{});
        for (std::size_t index = 0; index < captured.size(); ++index) {
            auto path = zima::assembly::InstancePath::decode(captured[index].instance_path);
            path.occurrence_ids.erase(path.occurrence_ids.begin(), path.occurrence_ids.begin() +
                static_cast<std::ptrdiff_t>(prefix.occurrence_ids.size()));
            component_placement_dialog_->set_placement_reference(index, component_side,
                {index == 0 ? zima::assembly::MateReferenceKind::Point : zima::assembly::MateReferenceKind::Axis,
                 path, captured[index].owner_id, captured[index].semantic_key}, index == 0 ? tr("Bod") : tr("Osa"));
        }
        const auto& rows = component_placement_dialog_->placement_references();
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (rows[index].component_reference.owner_id.empty()) {
                start_component_placement_reference_selection(index, true, true); return true;
            }
            if (rows[index].target_reference.owner_id.empty()) {
                start_component_placement_reference_selection(index, false, true); return true;
            }
        }
        pending_component_placement_index_.reset();
        component_placement_dialog_->set_active_reference_cell(std::nullopt);
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({}); viewer_->set_candidate_filter({}); viewer_->clear_selection();
        state_->setText(tr("Reference počátků jsou zadány."));
        return true;
    }

    if (item_kind != QStringLiteral("origin-reference")) return false;
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    candidate.owner_id = item->data(0, Qt::UserRole + 6).isValid()
        ? item->data(0, Qt::UserRole + 6).toString().toStdString()
        : item->data(0, Qt::UserRole).toString().toStdString();
    candidate.semantic_key =
        item->data(0, Qt::UserRole + 5).toString().toStdString();
    candidate.kind = (candidate.semantic_key == "origin:point" || candidate.semantic_key == "point")
        ? zima::viewer::CandidateKind::Vertex
        : candidate.semantic_key.starts_with("origin:axis:")
            ? zima::viewer::CandidateKind::Axis
            : zima::viewer::CandidateKind::Face;
    const auto before = component_placement_dialog_->placement_references();
    accept_component_placement_reference(candidate);
    return component_placement_dialog_->placement_references() != before ||
        !pending_component_placement_index_ ||
        *pending_component_placement_index_ != selected_row ||
        pending_component_placement_component_side_ != component_side;
}

bool AssemblyWorkspaceWindow::accept_construction_tree_reference(
    const QTreeWidgetItem* item) {
    if (item == nullptr || construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_) return false;
    const auto item_kind = item->data(0, Qt::UserRole + 3).toString();
    if ((item_kind == QStringLiteral("document-origin") ||
         item_kind == QStringLiteral("construction-origin"))) {
        const auto origin_id =
            item->data(0, Qt::UserRole).toString().toStdString();
        if (!placement_origin_allowed(origin_id)) return false;
        if (construction_reference_dialog_->owns_reference_owner(origin_id)) return false;
        const std::size_t selected_index = *pending_construction_reference_index_;
        // Matches Python's PointConstraintDialog.add_reference() Origin-kind
        // branch (shared, unoverridden, by Point/Axis/every placement
        // dialog): clicking the whole "Počátek dílu"/"Počátek sestavy" tree
        // node -- as opposed to one of its Point/X Axis/.../XZ Plane
        // children -- picking a POSITION row expands the Origin into its
        // datum planes so all three land as position references in one
        // click, fully constraining the container immediately. Only
        // FRONT/TOP no longer accept `origin:plane:*` at all: a document's
        // own datum planes looked selectable there but were misleading, and
        // a container-origin plane is only the container's already-derived
        // preview frame, not an independent orientation anchor.
        const auto find_plane_child = [&](std::string_view plane_key)
            -> const QTreeWidgetItem* {
            for (int i = 0; i < item->childCount(); ++i) {
                const auto* child = item->child(i);
                const auto key = child->data(0, Qt::UserRole + 5)
                    .toString().toStdString();
                if (key == std::string("origin:") + std::string(plane_key) ||
                    key == plane_key) return child;
            }
            return nullptr;
        };
        // Extract each plane child's identifying data into plain QStrings
        // *before* accepting any one of them: accept_origin_reference_value()
        // synchronously calls accept_construction_reference(), which
        // triggers refresh_scene() -> tree_->clear(), destroying every
        // QTreeWidgetItem including `item` and its still-unprocessed
        // siblings. Recursing back into accept_construction_tree_reference()
        // with a QTreeWidgetItem* found before that clear (as this loop used
        // to do) is therefore a use-after-free on the 2nd/3rd iteration --
        // this is the root cause of the crash when clicking the whole
        // "Počátek" origin node while entering a Point/Axis/Plane container
        // reference.
        struct CapturedPlaneReference {
            QString owner_id;
            QString instance_path;
            QString semantic_key;
        };
        const auto capture_plane_child = [&](const QTreeWidgetItem* child)
            -> CapturedPlaneReference {
            return CapturedPlaneReference{
                child->data(0, Qt::UserRole + 6).isValid()
                    ? child->data(0, Qt::UserRole + 6).toString()
                    : child->data(0, Qt::UserRole).toString(),
                child->data(0, Qt::UserRole + 1).toString(),
                child->data(0, Qt::UserRole + 5).toString()};
        };
        if (selected_index >= 3) return false;
        std::vector<CapturedPlaneReference> captured_planes;
        // XZ first becomes FRONT (+Y after its required inversion), XY
        // second becomes TOP (+Z); YZ completes the positional triad.
        for (const auto plane_key : {"plane:xz", "plane:xy", "plane:yz"}) {
            const auto* plane_child = find_plane_child(plane_key);
            if (plane_child != nullptr) {
                captured_planes.push_back(capture_plane_child(plane_child));
            }
        }
        // Always target the real first empty position row(s) (0/1/2),
        // never assume whatever row happened to be armed when "Počátek"
        // was clicked. A 2nd bulk-fill attempt (e.g. after the user deleted
        // one reference and re-triggered the bulk-fill) can leave the armed
        // row at 1 or 2 while row 0 is still empty (or vice versa) -- the
        // old code always fed xy/yz/xz starting from whatever row was
        // currently armed, so the first one or two planes collided with an
        // already-populated row (duplicate-reference rejection) or were
        // rejected as not adding an independent DOF, and silently failed to
        // show up in the 3D view.
        bool accepted_any = false;
        for (const auto& reference : captured_planes) {
            const auto target_index =
                construction_reference_dialog_->first_empty_position_index();
            if (target_index >= 3) break;
            start_construction_reference_selection(target_index);
            if (!pending_construction_reference_index_) break;
            if (accept_origin_reference_value(reference.owner_id,
                    reference.instance_path, reference.semantic_key)) {
                accepted_any = true;
            }
        }
        return accepted_any;
    }
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    try {
        const auto path = zima::assembly::InstancePath::decode(
            candidate.instance_path);
        const auto prefix = active_occurrence_path_.empty()
            ? zima::assembly::InstancePath{}
            : zima::assembly::InstancePath::decode(active_occurrence_path_);
        const bool active_part =
            workspace_.open_part(workspace_.active_document_id()) != nullptr;
        const bool in_scope = active_part ? path == prefix
            : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
        if (!in_scope) return false;
    } catch (const std::invalid_argument&) {
        return false;
    }
    if (item_kind == QStringLiteral("origin-reference")) {
        candidate.owner_id = item->data(0, Qt::UserRole + 6).isValid()
            ? item->data(0, Qt::UserRole + 6).toString().toStdString()
            : item->data(0, Qt::UserRole).toString().toStdString();
        candidate.semantic_key =
            item->data(0, Qt::UserRole + 5).toString().toStdString();
        candidate.kind =
            (candidate.semantic_key == "origin:point" ||
                candidate.semantic_key == "point")
            ? zima::viewer::CandidateKind::Vertex
            : candidate.semantic_key.starts_with("origin:axis:")
                ? zima::viewer::CandidateKind::Axis
                : zima::viewer::CandidateKind::Plane;
    } else if (item_kind == QStringLiteral("part-construction") ||
               item_kind == QStringLiteral("assembly-construction")) {
        const auto id = item->data(0, Qt::UserRole).toString().toStdString();
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* object = part != nullptr
            ? part->session.document().find_construction(id)
            : assembly != nullptr
                ? assembly->session.document().find_construction(id) : nullptr;
        if (object == nullptr) return false;
        if (object->id == construction_reference_dialog_->construction_id()) {
            return false;
        }
        candidate.owner_id = object->kind ==
                zima::document::ConstructionKind::Point
            ? object->container_origin.id : object->entity_id;
        candidate.kind = object->kind == zima::document::ConstructionKind::Point
            ? zima::viewer::CandidateKind::Vertex
            : object->kind == zima::document::ConstructionKind::Axis
                ? zima::viewer::CandidateKind::Axis
                : zima::viewer::CandidateKind::Plane;
        candidate.semantic_key = object->kind ==
                zima::document::ConstructionKind::Point ? "point"
            : object->kind == zima::document::ConstructionKind::Axis ? "axis"
            : "plane";
    } else {
        return false;
    }
    const bool accepted = candidate.kind == zima::viewer::CandidateKind::Vertex ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.kind == zima::viewer::CandidateKind::Plane;
    if (!accepted) return false;
    accept_construction_reference(candidate);
    return true;
}

bool AssemblyWorkspaceWindow::accept_origin_reference_value(
    const QString& owner_id, const QString& instance_path,
    const QString& semantic_key) {
    if (construction_reference_dialog_ == nullptr ||
        !pending_construction_reference_index_ || owner_id.isEmpty() ||
        semantic_key.isEmpty()) return false;
    zima::viewer::ViewerCandidate candidate;
    candidate.geometry = zima::viewer::CandidateGeometry::OriginalReference;
    candidate.instance_path = instance_path.toStdString();
    try {
        const auto path = zima::assembly::InstancePath::decode(
            candidate.instance_path);
        const auto prefix = active_occurrence_path_.empty()
            ? zima::assembly::InstancePath{}
            : zima::assembly::InstancePath::decode(active_occurrence_path_);
        const bool active_part =
            workspace_.open_part(workspace_.active_document_id()) != nullptr;
        const bool in_scope = active_part ? path == prefix
            : path == prefix ||
                (path.occurrence_ids.size() == prefix.occurrence_ids.size() + 1 &&
                 std::equal(prefix.occurrence_ids.begin(), prefix.occurrence_ids.end(),
                     path.occurrence_ids.begin()));
        if (!in_scope) return false;
    } catch (const std::invalid_argument&) {
        return false;
    }
    candidate.owner_id = owner_id.toStdString();
    candidate.semantic_key = semantic_key.toStdString();
    candidate.kind =
        (candidate.semantic_key == "origin:point" ||
            candidate.semantic_key == "point")
        ? zima::viewer::CandidateKind::Vertex
        : candidate.semantic_key.starts_with("origin:axis:")
            ? zima::viewer::CandidateKind::Axis
            : zima::viewer::CandidateKind::Plane;
    const bool accepted = candidate.kind == zima::viewer::CandidateKind::Vertex ||
        candidate.kind == zima::viewer::CandidateKind::Axis ||
        candidate.kind == zima::viewer::CandidateKind::Plane;
    if (!accepted) return false;
    accept_construction_reference(candidate);
    return true;
}

} // namespace zima::app
