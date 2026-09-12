#include <zima/workspace/placement_edit.hpp>
#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::show_construction_properties(
    zima::document::ConstructionKind kind, const std::string& object_id) {
    if (properties_dialog_ != nullptr) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;
    const auto* edited = object_id.empty() ? nullptr : part != nullptr
        ? part->session.document().find_construction(object_id)
        : assembly->session.document().find_construction(object_id);
    if (!object_id.empty() && (edited == nullptr || edited->kind != kind)) return;
    const bool edit_mode = edited != nullptr;
    const auto initial = edit_mode ? *edited
        : zima::document::PartDocument::create_construction(kind);
    const std::string document_id = workspace_.active_document_id();
    const int decimal_places = part != nullptr
        ? document_decimal_places(part->session.document())
        : document_decimal_places(assembly->session.document());
    auto* dialog = new ConstructionPropertiesDialog(
        initial, edit_mode,
        [this, document_id, edit_mode](zima::document::ConstructionObject committed) {
            static_cast<void>(workspace::commit_construction(workspace_, document_id,
                std::move(committed), edit_mode ? workspace::ConstructionEditMode::Replace
                                                : workspace::ConstructionEditMode::Create));
        }, this, decimal_places);
    dialog->set_reference_request_callback(
        [this](std::size_t index) { start_construction_reference_selection(index); });
    if (kind == zima::document::ConstructionKind::Curve3D) {
        dialog->set_curve_point_edit_request_callback(
            [this, dialog](std::optional<std::size_t> index) {
                show_curve_point_properties(dialog, index);
            });
    }
    if (kind == zima::document::ConstructionKind::Curve3D) {
        dialog->set_curve_axis_request_callback(
            [this, dialog](std::size_t index) {
                start_curve_axis_selection(dialog, index);
            });
        dialog->set_curve_axis_cycle_callback([this, dialog] {
            if (curve_axis_dialog_ != dialog) return;
            pending_curve_axis_index_.reset();
            curve_axis_dialog_ = nullptr;
            viewer_->clear_selection();
            set_construction_properties_dimension_selection();
            state_->setText(tr(
                "Nastavení směru bodu 3D křivky bylo změněno."));
        });
    }

    dialog->set_reference_highlights_changed_callback([this, dialog] {
        viewer_->set_constraint_reference_highlights(
            {}, highlighted_reference_edge_keys(*dialog));
    });
    construction_reference_dialog_ = dialog;
    auto plane_drag_baseline = std::make_shared<double>(initial.offset);
    auto plane_drag_back = std::make_shared<bool>(initial.orientation_back);
    if (kind == zima::document::ConstructionKind::Plane) {
        viewer_->set_extent_manipulator_callbacks(
            [dialog, plane_drag_baseline, plane_drag_back](const std::string&) {
                *plane_drag_baseline = dialog->plane_offset();
                *plane_drag_back = dialog->orientation_back();
            },
            [dialog, plane_drag_baseline, plane_drag_back](const std::string& key,
                    double coordinate) {
                if (key == "plane_offset") {
                    const double signed_offset =
                        *plane_drag_baseline + coordinate;
                    dialog->set_plane_offset_and_orientation(
                        std::abs(signed_offset),
                        signed_offset < 0.0
                            ? !*plane_drag_back : *plane_drag_back);
                }
            }, [] {});
    }
    dialog->set_preview_callback(
        [this, document_id, edit_mode](zima::document::ConstructionObject preview) {
            const std::set<std::string> preview_owners{
                preview.entity_id, preview.container_origin.id};
            zima::kernel::ViewerMesh mesh;
            zima::kernel::ViewerReferenceGeometry reference_geometry;
            zima::kernel::Vec3 resolved_origin = preview.origin;
            zima::kernel::Vec3 resolved_entity_origin = preview.entity_origin;
            zima::kernel::Vec3 resolved_direction = preview.direction;
            zima::kernel::Vec3 resolved_rotation = preview.rotation_base;
            zima::kernel::Vec3 resolved_final_rotation = preview.rotation;
            bool resolved_reference_valid = preview.reference_valid;
            bool resolved_orientation_inherited =
                preview.orientation_inherited_from_reference;
            if (const auto* source = workspace_.open_part(document_id)) {
                auto next = source->session.document();
                if (edit_mode) {
                    if (auto* target = next.find_construction(preview.id)) {
                        *target = preview;
                    }
                } else {
                    next.insert_history_entry(zima::document::PartHistoryKind::Construction, preview.id);
                    next.constructions.push_back(preview);
                }
                const auto& calculated = source->session.calculated_boundaries();
                reference_geometry =
                    construction_reference_source_geometry(calculated);
                next.resolve_constructions(reference_geometry);
                if (const auto* resolved = next.find_construction(preview.id)) {
                    resolved_origin = resolved->origin;
                    resolved_entity_origin = resolved->entity_origin;
                    resolved_direction = resolved->direction;
                    resolved_rotation = resolved->rotation_base;
                    resolved_final_rotation = resolved->rotation;
                    resolved_reference_valid = resolved->reference_valid;
                    resolved_orientation_inherited =
                        resolved->orientation_inherited_from_reference;
                    construction_parameter_preview_ = *resolved;
                }
                {
                    // Origin's own on-screen size (document and container
                    // alike) is now a fixed constant independent of the
                    // scene/model size -- see kDocumentOriginPlaneSize's
                    // comment in part_document.cpp. `construction_viewer_mesh`
                    // still accepts a scene_size parameter for source
                    // compatibility, but it is unused; pass 0.0.
                    mesh = next.construction_viewer_mesh(preview.id);
                }
                append_reference_geometry(reference_geometry,
                    next.origin_viewer_mesh().original_references);
                append_reference_geometry(reference_geometry,
                    next.construction_viewer_mesh().original_references);
                append_reference_geometry(reference_geometry,
                    next.history_origin_reference_geometry_before(preview.id));
                reference_geometry = next.construction_reference_geometry_for(
                    preview.id, std::move(reference_geometry));
            } else if (const auto* source = workspace_.open_assembly(document_id)) {
                auto next = source->session.document();
                if (edit_mode) {
                    if (auto* target = next.find_construction(preview.id)) {
                        *target = preview;
                    }
                } else {
                    next.constructions.push_back(preview);
                }
                next.resolve_constructions();
                if (const auto* resolved = next.find_construction(preview.id)) {
                    resolved_origin = resolved->origin;
                    resolved_entity_origin = resolved->entity_origin;
                    resolved_direction = resolved->direction;
                    resolved_rotation = resolved->rotation_base;
                    resolved_final_rotation = resolved->rotation;
                    resolved_reference_valid = resolved->reference_valid;
                    resolved_orientation_inherited =
                        resolved->orientation_inherited_from_reference;
                    construction_parameter_preview_ = *resolved;
                }
                mesh = next.construction_viewer_mesh(preview.id);
                reference_geometry = next.build_scene().original_references;
            }
            construction_reference_geometry_ = reference_geometry;
            if (construction_reference_dialog_ != nullptr) {
                const auto constraint_state =
                    zima::document::point_constraint_state(
                        preview.references, reference_geometry);
                construction_translation_dof_ = constraint_state.remaining_dof;
                construction_reference_dialog_->set_translation_constraint_state(
                    constraint_state, resolved_origin);
                auto rotation_state =
                    zima::document::orientation_constraint_state(
                        preview.references, reference_geometry, true,
                        resolved_origin);
                if (construction_shortcut_satisfied(
                        preview.kind, preview.references, reference_geometry)) {
                    rotation_state.remaining_dof = 0;
                    rotation_state.constrained_axes = {true, true, true};
                }
                if (resolved_orientation_inherited) {
                    rotation_state.remaining_dof = 0;
                    rotation_state.constrained_axes = {true, true, true};
                }
                construction_rotation_dof_ = rotation_state.remaining_dof;
                construction_reference_dialog_->set_rotation_constraint_state(
                    rotation_state);
                const bool has_orientation_references = std::any_of(
                    preview.references.begin(), preview.references.end(),
                    [](const auto& reference) {
                        return reference.orientation_drives_rotation;
                    }) || resolved_orientation_inherited;
                construction_reference_dialog_->set_orientation_base_rotation(
                    has_orientation_references
                        ? resolved_rotation : preview.absolute_rotation,
                    has_orientation_references);
                construction_reference_dialog_->set_resolved_rotation(
                    resolved_final_rotation, resolved_reference_valid);
                construction_reference_dialog_->set_orientation_inherited_from_reference(
                    resolved_orientation_inherited);
            }
            construction_preview_mesh_ = std::move(mesh);
            construction_dimension_object_id_ = preview.id;
            viewer_->set_feature_preview_owners(preview_owners);
            viewer_->set_transient_edges({});
            preserve_view_on_refresh_ = true;
            refresh_scene();
            if (!pending_construction_reference_index_) {
                tree_->setProperty("commandSelectionActive", false);
                set_construction_properties_dimension_selection();
            }
            if (preview.kind == zima::document::ConstructionKind::Plane) {
                viewer_->set_extent_manipulator(zima::viewer::ExtentManipulator{
                    "plane_offset", {}, resolved_entity_origin,
                    resolved_direction, 0.0, true});
            }
        });
    viewer_->set_editing_origin_visible(true);
    properties_dialog_ = dialog;
    track_tree_edit(dialog);

    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        construction_reference_dialog_ = nullptr;
        curve_axis_dialog_ = nullptr;
        pending_curve_axis_index_.reset();
        construction_dimension_object_id_.clear();
        construction_preview_mesh_.reset();
        construction_parameter_preview_.reset();
        construction_reference_geometry_ = {};
        pending_construction_reference_index_.reset();
        construction_reference_auto_advance_ = false;
        tree_->setProperty("commandSelectionActive", false);
        construction_translation_dof_ = 3;
        construction_rotation_dof_ = 3;
        viewer_->set_transient_edges({});
        viewer_->set_extent_manipulator(std::nullopt);
        viewer_->set_extent_manipulator_callbacks({}, {}, {});
        viewer_->set_feature_preview_owners({});
        viewer_->set_editing_origin_visible(false);
        // A construction command installs its own persisted-reference filter.
        // Returning to ordinary Part selection must also remove that filter;
        // restoring only CandidateKind::Container leaves every basic history
        // container unpickable in the View.
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        viewer_->set_constraint_reference_highlights({}, {});
        refresh_tabs();
        // Closing the dialog (OK or Cancel) must not re-fit/zoom the camera
        // to the just-committed (or reverted) construction geometry -- an
        // Axis/Plane's real display-size extent is typically much larger
        // than the small Origin preview shown while the dialog was open,
        // and an un-preserved refresh_scene() here would re-fit the camera
        // to it, making the Origin (and everything else) appear to shrink
        // the instant the dialog closes. See the identical guard right
        // before every other refresh_scene() call in this dialog's
        // callbacks above.
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    dialog->show();
    const auto first = dialog->first_empty_position_index();
    if (first < 3) {
        start_construction_reference_selection(first, true);
    } else {
        tree_->setProperty("commandSelectionActive", false);
        viewer_->clear_selection();
        set_construction_properties_dimension_selection();
    }
}

void AssemblyWorkspaceWindow::start_curve_axis_selection(
    ConstructionPropertiesDialog* curve_dialog, std::size_t point_index) {
    if (curve_dialog == nullptr || properties_dialog_ != curve_dialog) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    const auto curve = curve_dialog->pending_value();
    if ((part == nullptr && assembly == nullptr) ||
        point_index >= curve.curve_points.size()) return;
    const auto& point = curve.curve_points[point_index];

    // One green reference field owns the common Viewer picker at a time.
    // The Curve dialog may still have its automatic placement row armed when
    // the user clicks a Point's direction-axis field. Leaving that pending
    // row alive makes the next nested Point Properties dialog compete with
    // this axis picker for the same LMB confirmation.
    pending_construction_reference_index_.reset();
    construction_reference_auto_advance_ = false;
    curve_dialog->set_active_reference_index(std::nullopt);

    zima::document::PartDocument next;
    zima::kernel::ViewerReferenceGeometry reference_geometry;
    if (part != nullptr) {
        next = part->session.document();
        reference_geometry = construction_reference_source_geometry(
            part->session.calculated_boundaries());
    } else {
        auto source = assembly->session.document();
        next.document_id = source.document_id;
        next.name = source.name;
        next.constructions = source.constructions;
        source.constructions.clear();
        reference_geometry = source.build_scene().original_references;
    }
    if (auto* target = next.find_construction(curve.id)) {
        *target = curve;
    } else {
        next.constructions.push_back(curve);
    }
    next.resolve_constructions(std::move(reference_geometry));
    construction_preview_mesh_ = next.construction_viewer_mesh(point.id);
    construction_dimension_object_id_ = point.id;
    curve_axis_dialog_ = curve_dialog;
    pending_curve_axis_index_ = point_index;
    curve_dialog->set_curve_axis_active(point_index);
    viewer_->set_feature_preview_owners(
        {point.entity_id, point.container_origin.id, curve.entity_id});
    preserve_view_on_refresh_ = true;
    refresh_scene();
    viewer_->clear_selection();
    viewer_->set_selection_contract({zima::viewer::CandidateKind::Axis});
    const std::string origin_id = point.container_origin.id;
    viewer_->set_candidate_filter([origin_id](const auto& candidate) {
        return candidate.kind == zima::viewer::CandidateKind::Axis &&
            candidate.geometry ==
                zima::viewer::CandidateGeometry::OriginalReference &&
            candidate.owner_id == origin_id &&
            candidate.semantic_key.starts_with("origin:axis:");
    });
    state_->setText(tr(
        "3D křivka: vyberte osu lokálního počátku bodu. "
        "Pravým tlačítkem v políčku lze směr obrátit."));
}

void AssemblyWorkspaceWindow::accept_curve_axis_reference(
    const zima::viewer::ViewerCandidate& candidate) {
    if (curve_axis_dialog_ == nullptr || !pending_curve_axis_index_) return;
    if (candidate.kind != zima::viewer::CandidateKind::Axis ||
        !candidate.semantic_key.starts_with("origin:axis:")) return;
    using zima::document::Curve3DTangentMode;
    const auto axis = candidate.semantic_key.substr(
        std::string("origin:axis:").size());
    const auto tangent = axis == "x" ? Curve3DTangentMode::PositiveX
        : axis == "y" ? Curve3DTangentMode::PositiveY
        : axis == "z" ? Curve3DTangentMode::PositiveZ
        : Curve3DTangentMode::Automatic;
    if (tangent == Curve3DTangentMode::Automatic) return;
    auto* dialog = curve_axis_dialog_;
    const auto index = *pending_curve_axis_index_;
    pending_curve_axis_index_.reset();
    curve_axis_dialog_ = nullptr;
    dialog->set_curve_axis_active(std::nullopt);
    dialog->set_curve_point_tangent(index, tangent);
    viewer_->clear_selection();
    set_construction_properties_dimension_selection();
    state_->setText(tr("Osa směru bodu 3D křivky byla nastavena."));
}


void AssemblyWorkspaceWindow::show_sweep_profile_sketch(
    ConstructionPropertiesDialog* sweep_dialog,
    std::size_t profile_index) {
    if (sweep_dialog == nullptr || properties_dialog_ != sweep_dialog ||
        sweep_profile_sketch_draft_) return;
    auto pending = sweep_dialog->pending_sweep_value();
    if (pending.feature_kind != zima::document::FeatureKind::Sweep3D ||
        profile_index >= pending.sweep3d.profiles.size()) return;
    try {
        auto sketch = zima::sketcher::Sketch::from_serialized(
            pending.sweep3d.profiles[profile_index].sketch_serialized);
        sketch.owner_container_id = pending.id;
        sketch.validate();
        sweep_profile_sketch_draft_ = std::move(sketch);
    } catch (const std::exception& exception) {
        state_->setText(tr("Profilovou skicu Sweep/Loftu nelze otevřít: %1")
            .arg(QString::fromUtf8(exception.what())));
        return;
    }
    sweep_profile_parent_dialog_ = sweep_dialog;
    sweep_profile_sketch_index_ = profile_index;
    sweep_dialog->hide();
    properties_dialog_ = nullptr;
    construction_reference_dialog_ = nullptr;
    active_sketch_id_ = sweep_profile_sketch_draft_->id;
    selected_sketch_id_ = active_sketch_id_;
    clear_selected_sketch_geometry();
    viewer_->clear_selection();
    tree_->clearSelection();
    preserve_view_on_refresh_ = true;
    refresh_scene();
    align_active_sketch_view();
    state_->setText(tr(
        "Profil Sweep/Loftu: nakreslete uzavřený obrys a zvolte Dokončit skicu."));
}

void AssemblyWorkspaceWindow::show_curve_point_properties(
    ConstructionPropertiesDialog* curve_dialog,
    std::optional<std::size_t> point_index) {
    if (curve_dialog == nullptr || properties_dialog_ != curve_dialog) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (part == nullptr && assembly == nullptr) return;

    const auto curve_value = curve_dialog->pending_value();
    const auto* existing = point_index
        ? curve_dialog->curve_point(*point_index) : nullptr;
    if (point_index && existing == nullptr) return;
    auto initial = existing != nullptr ? *existing
        : zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Point);
    initial.parent_construction_id = curve_value.id;
    const std::size_t slot = point_index.value_or(curve_value.curve_points.size());
    if (existing == nullptr) {
        initial.curve_tangent =
            zima::document::Curve3DTangentMode::PositiveX;
        initial.curve_tangent_enabled = false;
        initial.name = tr("Bod %1").arg(slot + 1, 3, 10,
            QLatin1Char('0')).toStdString();
    }

    const std::string document_id = workspace_.active_document_id();
    const int decimal_places = part != nullptr
        ? document_decimal_places(part->session.document())
        : document_decimal_places(assembly->session.document());
    auto committed = std::make_shared<bool>(false);
    QPointer<ConstructionPropertiesDialog> parent_guard(curve_dialog);
    auto* point_dialog = new ConstructionPropertiesDialog(
        initial, existing != nullptr,
        [parent_guard, slot, committed](
                zima::document::ConstructionObject point) {
            if (parent_guard == nullptr) return;
            parent_guard->set_curve_point(slot, std::move(point), false);
            *committed = true;
        }, this, decimal_places);

    set_local_origin_selection_mode(false);
    bind_local_origin_selection(point_dialog);
    curve_dialog->hide();
    properties_dialog_ = point_dialog;
    construction_reference_dialog_ = point_dialog;
    point_dialog->set_reference_request_callback(
        [this](std::size_t index) {
            start_construction_reference_selection(index);
        });
    point_dialog->set_reference_highlights_changed_callback(
        [this, point_dialog] {
            viewer_->set_constraint_reference_highlights(
                {}, highlighted_reference_edge_keys(*point_dialog));
        });
    point_dialog->set_preview_callback(
        [this, document_id, parent_guard, slot](
                zima::document::ConstructionObject preview) {
            if (parent_guard == nullptr) return;
            auto* source_part = workspace_.open_part(document_id);
            auto* source_assembly = workspace_.open_assembly(document_id);
            if (source_part == nullptr && source_assembly == nullptr) return;

            auto curve_preview = parent_guard->pending_value();
            preview.parent_construction_id = curve_preview.id;
            if (slot < curve_preview.curve_points.size()) {
                curve_preview.curve_points[slot] = preview;
            } else if (slot == curve_preview.curve_points.size()) {
                curve_preview.curve_points.push_back(preview);
            } else {
                return;
            }

            zima::document::PartDocument next;
            zima::kernel::ViewerReferenceGeometry reference_geometry;
            const bool part_document = source_part != nullptr;
            if (part_document) {
                next = source_part->session.document();
                reference_geometry = construction_reference_source_geometry(
                    source_part->session.calculated_boundaries());
            } else {
                auto source = source_assembly->session.document();
                next.document_id = source.document_id;
                next.name = source.name;
                next.constructions = source.constructions;
                source.constructions.clear();
                reference_geometry =
                    source.build_scene().original_references;
            }
            if (parent_guard->is_sweep()) {
                auto sweep = parent_guard->pending_sweep_value();
                sweep.sweep3d.path.curve_points = curve_preview.curve_points;
                if (auto* target = next.find_container(sweep.id)) *target = sweep;
                else {
                    next.insert_history_entry(zima::document::PartHistoryKind::Feature, sweep.id);
                    next.history.push_back(sweep);
                }
            } else if (auto* target = next.find_construction(curve_preview.id)) {
                *target = curve_preview;
            } else {
                if (part_document) next.insert_history_entry(zima::document::PartHistoryKind::Construction, curve_preview.id);
                next.constructions.push_back(curve_preview);
            }
            next.resolve_constructions(reference_geometry);
            // The path carrier is presentation data, not a second container
            // inserted into the persisted Body ownership graph.
            std::optional<zima::document::PartDocument> sweep_carrier;
            std::string sweep_body;
            if (parent_guard->is_sweep()) {
                const auto* sweep = next.find_container(parent_guard->pending_sweep_value().id);
                if (!sweep) return;
                if (const auto* owner=next.body_owner_for_object(sweep->id)) sweep_body=owner->scope.id;
                sweep_carrier=sweep_body.empty() ? next : next.body_document(sweep_body);
                sweep_carrier->constructions.push_back(sweep_display_path(*sweep));
            }
            const auto& point_document=sweep_carrier ? *sweep_carrier : next;
            const auto* resolved = point_document.find_construction(preview.id);
            if (resolved == nullptr) return;

            construction_parameter_preview_ = *resolved;
            construction_preview_mesh_ = point_document.construction_viewer_mesh(preview.id);
            if (!sweep_body.empty())
                construction_preview_mesh_=next.place_body_mesh(std::move(*construction_preview_mesh_),sweep_body);
            if (part_document) {
                append_reference_geometry(reference_geometry,
                    next.origin_viewer_mesh().original_references);
            }
            append_reference_geometry(reference_geometry,
                next.history_origin_reference_geometry_before(""));
            append_reference_geometry(reference_geometry,
                next.construction_viewer_mesh().original_references);
            if (sweep_carrier) {
                if (!sweep_body.empty()) reference_geometry=next.construction_reference_geometry_for(
                    sweep_body,std::move(reference_geometry));
                construction_reference_geometry_=sweep_carrier->construction_reference_geometry_for(
                    preview.id,std::move(reference_geometry));
            } else {
                construction_reference_geometry_=next.construction_reference_geometry_for(
                    preview.id,std::move(reference_geometry));
            }

            const auto constraint_state =
                zima::document::point_constraint_state(
                    preview.references, construction_reference_geometry_);
            construction_translation_dof_ = constraint_state.remaining_dof;
            if (construction_reference_dialog_ != nullptr) {
                construction_reference_dialog_->set_translation_constraint_state(
                    constraint_state, resolved->origin);
                const bool inherited =
                    resolved->orientation_inherited_from_reference;
                auto rotation_state =
                    zima::document::orientation_constraint_state(
                        preview.references, construction_reference_geometry_, true,
                        resolved->origin);
                if (inherited) {
                    rotation_state.remaining_dof = 0;
                    rotation_state.constrained_axes = {true, true, true};
                }
                construction_rotation_dof_ = rotation_state.remaining_dof;
                construction_reference_dialog_->set_rotation_constraint_state(
                    rotation_state);
                const bool has_orientation_references = std::any_of(
                    preview.references.begin(), preview.references.end(),
                    [](const auto& reference) {
                        return reference.orientation_drives_rotation;
                    }) || inherited;
                construction_reference_dialog_->set_orientation_base_rotation(
                    has_orientation_references
                        ? resolved->rotation_base
                        : resolved->absolute_rotation,
                    has_orientation_references);
                construction_reference_dialog_->set_resolved_rotation(
                    resolved->rotation, resolved->reference_valid);
                construction_reference_dialog_->
                    set_orientation_inherited_from_reference(inherited);
            }
            construction_dimension_object_id_ = preview.id;
            viewer_->set_feature_preview_owners(
                {preview.entity_id, preview.container_origin.id,
                 curve_preview.entity_id});
            viewer_->set_transient_edges({});
            preserve_view_on_refresh_ = true;
            refresh_scene();
            if (!pending_construction_reference_index_) {
                tree_->setProperty("commandSelectionActive", false);
                set_construction_properties_dimension_selection();
            }
        });

    connect(point_dialog, &QObject::destroyed, this,
        [this, parent_guard, committed] {
            pending_construction_reference_index_.reset();
            construction_reference_auto_advance_ = false;
            viewer_->set_constraint_reference_highlights({}, {});
            viewer_->set_feature_preview_owners({});
            viewer_->set_candidate_filter({});
            viewer_->clear_selection();
            tree_->setProperty("commandSelectionActive", false);
            if (parent_guard == nullptr) {
                properties_dialog_ = nullptr;
                construction_reference_dialog_ = nullptr;
                construction_preview_mesh_.reset();
                construction_parameter_preview_.reset();
                construction_reference_geometry_ = {};
                preserve_view_on_refresh_ = true;
                refresh_scene();
                return;
            }
            properties_dialog_ = parent_guard;
            construction_reference_dialog_ = parent_guard;
            parent_guard->show();
            parent_guard->raise();
            parent_guard->activateWindow();
            parent_guard->refresh_preview();
            static_cast<void>(committed);
        });

    point_dialog->show();
    const auto first = point_dialog->first_empty_position_index();
    if (first < 3) {
        start_construction_reference_selection(first, true);
    } else {
        set_construction_properties_dimension_selection();
    }
}

} // namespace zima::app
