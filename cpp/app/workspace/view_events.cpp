#include "workspace_internal.hpp"
#include <zima/workspace/document_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && placement_reference_drag_document_) {
        placement_reference_drag_document_.reset();
        placement_reference_drag_document_id_.clear();
        placement_reference_drag_occurrence_id_.clear();
        placement_reference_drag_index_ = 0;
        placement_reference_drag_changed_ = false;
        placement_reference_drag_angular_ = false;
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Tažení reference umístění bylo zrušeno beze změny."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && component_drag_document_) {
        const auto placement = component_drag_document_->find_occurrence(component_drag_occurrence_id_)->placement;
        const auto path=component_drag_instance_path_;
        component_drag_preview_.reset();
        end_component_drag();
        if (component_placement_dialog_) component_placement_dialog_->set_pending_placement(placement);
        else { preserve_view_on_refresh_=true;refresh_scene();select_occurrence(path);set_selected_component_origin(path); }
        state_->setText(tr("Tažení komponenty bylo zrušeno beze změny."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && edge_treatment_selection_) {
        if (edge_treatment_dialog_ != nullptr) {
            edge_treatment_dialog_->reject();
            event->accept();
            return;
        }
        edge_treatment_selection_.reset();
        edge_treatment_hover_seed_.reset();
        pending_edge_treatment_edges_.clear();
        pending_edge_treatment_groups_.clear();
        pending_edge_treatment_seeds_.clear();
        edge_treatment_preview_owner_id_.clear();
        viewer_->set_feature_hover_edges({});
        preserve_view_on_refresh_ = true;
        refresh_scene();
        state_->setText(tr("Zaoblení nebo sražení zrušeno."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && shell_dialog_ != nullptr) {
        shell_dialog_->reject();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && extrusion_target_dialog_ != nullptr) {
        finish_extrusion_target_selection();
        state_->setText(tr("Výběr cílové plochy vytažení byl zrušen."));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && delete_selected_sketch_geometry()) {
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        viewer_->hasFocus() && viewer_->confirm_current_pointer()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape &&
        (sketch_external_reference_active_ || sketch_point_active_ ||
         sketch_segment_active_ ||
         sketch_rectangle_active_ || sketch_polygon_active_ || sketch_trim_active_ ||
         sketch_circle_active_ ||
         sketch_offset_dialog_ || sketch_mirror_active_ || sketch_arc_active_ || sketch_ellipse_active_ ||
         sketch_elliptical_arc_active_ ||
         sketch_bspline_active_ ||
         sketch_coincident_active_ || sketch_midpoint_active_ ||
         sketch_symmetric_active_ || sketch_concentric_active_ ||
         sketch_tangent_active_ || sketch_common_tangent_active_ ||
         sketch_segment_pair_active_ || sketch_point_dimension_active_ ||
         sketch_universal_dimension_active_ ||
         sketch_line_pair_dimension_active_ || pending_sketch_dimension_ ||
         sketch_corner_fillet_active_)) {
        if (cancel_current_sketch_step()) {
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        // Idle mode (no active command consumed Escape above): mirror the
        // ordinary empty-View-space click contract and clear the confirmed
        // View+Tree selection together.
        viewer_->clear_selection();
        tree_->clearSelection();
        tree_->setCurrentItem(nullptr);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

std::optional<std::string> AssemblyWorkspaceWindow::resolve_active_occurrence(
    const std::string& part_document_id) const {
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (assembly == nullptr) return std::string{};
    if (!active_occurrence_path_.empty()) {
        try {
            const auto address = workspace_.resolve_occurrence(
                workspace_.displayed_document_id(),
                zima::assembly::InstancePath::decode(active_occurrence_path_));
            if (address && address->source_document_id == part_document_id &&
                address->source_kind == zima::assembly::ComponentSourceKind::Part) {
                return active_occurrence_path_;
            }
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }
    std::optional<std::string> only;
    std::set<std::string> paths;
    for (const auto& reference : assembly->session.document()
             .build_scene().original_references.triangle_references) {
        paths.insert(reference.instance_path);
    }
    for (const auto& path : paths) {
        const auto address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(path));
        if (!address || address->source_document_id != part_document_id) continue;
        if (only) return std::nullopt;
        only = path;
    }
    return only;
}

std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
AssemblyWorkspaceWindow::active_part_local_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    auto local_origin = origin;
    auto local_direction = direction;
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    const auto* assembly = workspace_.open_assembly(workspace_.displayed_document_id());
    if (!part) return {origin, direction};
    if (assembly) {
        const auto occurrence = resolve_active_occurrence(part->session.document().document_id);
        if (!occurrence || occurrence->empty()) return {origin, direction};
        const auto path = zima::assembly::InstancePath::decode(*occurrence);
        local_origin = workspace_.occurrence_point_from_scene(
            assembly->session.document().document_id, path, origin);
        local_direction = workspace_.occurrence_direction_from_scene(
            assembly->session.document().document_id, path, direction);
    }
    if (const auto* sketch = active_sketch()) {
        if (const auto* body = sketch_body(*sketch)) {
            const auto frame = container_dimension_frame(body->scope.placement);
            local_origin = frame.inverse_point(local_origin);
            local_direction = frame.inverse_vector(local_direction);
        }
    }
    return {local_origin, local_direction};
}

std::pair<zima::kernel::Vec3, zima::kernel::Vec3>
AssemblyWorkspaceWindow::active_assembly_local_ray(
    const zima::kernel::Vec3& origin,
    const zima::kernel::Vec3& direction) const {
    const auto* active = workspace_.open_assembly(workspace_.active_document_id());
    const auto* displayed =
        workspace_.open_assembly(workspace_.displayed_document_id());
    if (active == nullptr || displayed == nullptr ||
        active->session.document().document_id ==
            displayed->session.document().document_id ||
        active_occurrence_path_.empty()) return {origin, direction};
    const auto path = zima::assembly::InstancePath::decode(active_occurrence_path_);
    return {
        workspace_.occurrence_point_from_scene(
            displayed->session.document().document_id, path, origin),
        workspace_.occurrence_direction_from_scene(
            displayed->session.document().document_id, path, direction),
    };
}

void AssemblyWorkspaceWindow::regenerate_active_part() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        const auto& previous = part->session.document();
        auto next = previous;
        // Explicit Regenerate recalculates geometry even when parameters match
        // the persisted cache (for example after a kernel calculation fix).
        auto calculated = calculate_part_with_resolved_references(next);
        next.resolve_constructions(calculated.empty()
            ? zima::kernel::ViewerReferenceGeometry{}
            : calculated.back().mesh.original_references);
        const bool references_changed =
            refresh_sketch_external_references(next, calculated) |
            workspace_.refresh_context_external_references(next) |
            prune_missing_drill_point_references(next, calculated);
        if (references_changed) calculated = calculate_part(next, &calculated);
        const bool sketches_changed = next.sketches.size() != previous.sketches.size() ||
            !std::equal(next.sketches.begin(), next.sketches.end(), previous.sketches.begin(),
                [](const auto& left, const auto& right) { return left.serialized() == right.serialized(); });
        if (references_changed || sketches_changed ||
            zima::document::serialize_sections(next.sections)!=zima::document::serialize_sections(previous.sections) || next.history != previous.history ||
            next.constructions != previous.constructions ||
            next.body_history.bodies() != previous.body_history.bodies()) {
            part->session.commit(std::move(next), std::move(calculated));
        } else {
            part->session.update_calculated_boundaries(std::move(calculated));
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        const auto& results = part->session.calculated_boundaries();
        state_->setText(!results.empty() && !results.back().calculation_errors.empty()
            ? tr("Regenerace zachovala platnou geometrii. Nevypočítané prvky jsou označeny ve stromu.")
            : references_changed
                ? tr("Part byl regenerován a externí reference skic byly obnoveny.")
                : tr("Part byl regenerován."));
    } catch (const std::exception& error) {
        report_operation_error(tr("Regenerace Partu selhala"), error.what());
    }
}

void AssemblyWorkspaceWindow::undo() {
    if (properties_dialog_ != nullptr) return;
    if(section_sketch_history(false))return;
    cancel_sketch_segment();
    if (workspace::step_document_history(workspace_, workspace_.active_document_id(),
            workspace::HistoryDirection::Undo)) refresh_scene();
    refresh_tabs();
}

void AssemblyWorkspaceWindow::redo() {
    if (properties_dialog_ != nullptr) return;
    if(section_sketch_history(true))return;
    cancel_sketch_segment();
    if (workspace::step_document_history(workspace_, workspace_.active_document_id(),
            workspace::HistoryDirection::Redo)) refresh_scene();
    refresh_tabs();
}

} // namespace zima::app
