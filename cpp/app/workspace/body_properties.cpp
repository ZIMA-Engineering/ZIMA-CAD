#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::finish_body_dialog(QDialog* dialog) {
    properties_dialog_ = dialog;
    if (dynamic_cast<PlacementReferenceDialog*>(dialog)) bind_local_origin_selection(dialog);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
        if (construction_dimension_object_id_ == body_dialog_step_id_)
            construction_dimension_object_id_.clear();
        body_dialog_preview_.reset(); body_dialog_context_.reset(); body_dialog_step_id_.clear();
        refresh_tabs(); preserve_view_on_refresh_ = true; refresh_scene();
    });
    preserve_view_on_refresh_ = true; refresh_scene();
    dialog->show();
}

void AssemblyWorkspaceWindow::activate_first_part_body() {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!part) return;
    const auto& graph = part->session.document().body_history;
    for (const auto& id : graph.order()) {
        const auto* body = graph.find(id);
        if (body && !body->derived_copy) {
            part->session.activate_body(id);
            return;
        }
    }
}

void AssemblyWorkspaceWindow::activate_body(const std::string& id) {
    if (properties_dialog_) return;
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!part) return;
    auto next = part->session.document();
    next.body_history.activate(id);
    part->session.commit(std::move(next), part->session.calculated_boundaries());
    viewer_->clear_selection();
    preserve_view_on_refresh_ = true; refresh_tabs(); refresh_scene();
}

void AssemblyWorkspaceWindow::show_body_properties(const std::string& id) {
    if (properties_dialog_) return;
    const auto document_id = workspace_.active_document_id();
    auto* part = workspace_.open_part(document_id);
    if (!part || document_id != workspace_.displayed_document_id()) return;
    auto pending = part->session.document();
    auto graph = pending.body_history;
    std::string edited = id;
    if (edited.empty()) {
        const bool first = graph.bodies().empty();
        edited = zima::document::create_origin_bound_body(graph, pending.document_id, tr("Těleso %1").arg(graph.bodies().size()+1).toStdString());
        if (first) for (const auto& entry : pending.history_order) graph.insert(entry);
    }
    const auto* source = graph.find(edited);
    if (!source) return;
    pending.set_body_history(graph);
    body_dialog_preview_ = pending;
    body_dialog_step_id_ = edited;
    const auto position = static_cast<std::size_t>(std::distance(graph.order().begin(), std::ranges::find(graph.order(), edited)));
    body_dialog_context_ = graph.available_before(position);
    auto* dialog = new BodyPropertiesDialog(*source, graph.active_body_id() == edited,
        [this, document_id, graph, edited](zima::document::BodyHistory value, bool active) mutable {
            auto* current = workspace_.open_part(document_id);
            if (!current) throw std::runtime_error("Dokument již není otevřený.");
            auto next = current->session.document();
            auto updated = graph;
            updated.update_body(std::move(value));
            if (active) updated.activate(edited);
            else if (updated.active_body_id() == edited) updated.activate({});
            next.set_body_history(std::move(updated));
            auto calculated = calculate_part_with_resolved_references(next, &current->session.calculated_boundaries());
            // Reference resolution may normalize signed zero after the last
            // geometry pass. OK must retain the exact final inputs so a later
            // metadata-only edit and Save can reuse this calculation safely.
            calculated = calculate_part(next, &calculated);
            current->session.commit(std::move(next), std::move(calculated));
        }, this, document_decimal_places(part->session.document()));
    primitive_reference_geometry_ = part->session.calculated_boundaries().empty()
        ? zima::kernel::ViewerReferenceGeometry{} : part->session.calculated_boundaries().back().mesh.original_references;
    append_reference_geometry(primitive_reference_geometry_, pending.origin_viewer_mesh().original_references);
    append_reference_geometry(primitive_reference_geometry_, pending.body_origin_reference_geometry());
    append_reference_geometry(primitive_reference_geometry_, pending.history_origin_reference_geometry_before({}));
    append_reference_geometry(primitive_reference_geometry_, pending.construction_viewer_mesh().original_references);
    dialog->set_forbidden_owner([pending,position](const std::string& id) {
        if (id==pending.document_id+":origin") return false;
        const auto* owner=pending.body_owner_for_object(id);
        if (!owner) return true;
        const auto& order=pending.body_history.order();
        return static_cast<std::size_t>(std::distance(order.begin(),std::ranges::find(order,owner->scope.id)))>=position;
    });
    primitive_reference_dialog_=dialog;
    construction_dimension_object_id_=edited;
    dialog->set_reference_request_callback([this](std::size_t index) { start_primitive_reference_selection(index); });
    dialog->set_reference_highlights_changed_callback([this,dialog] {
        viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));
    });
    const auto preview=[this,dialog,edited](zima::document::BodyHistory value) {
        if (!body_dialog_preview_) return;
        auto& placement=value.scope.placement;
        zima::kernel::Vec3 base;
        bool oriented=false;
        const bool valid=zima::document::resolve_placement(placement,primitive_reference_geometry_,&base,&oriented);
        const auto constraint=zima::document::point_constraint_state(placement.references,primitive_reference_geometry_);
        primitive_translation_dof_=constraint.remaining_dof;
        dialog->set_translation_constraint_state(constraint,{placement.x,placement.y,placement.z});
        dialog->set_rotation_constraint_state(zima::document::orientation_constraint_state(
            placement.references,primitive_reference_geometry_,true,{placement.x,placement.y,placement.z}));
        dialog->set_orientation_base_rotation(base,oriented);
        dialog->set_resolved_rotation({placement.rotation_x,placement.rotation_y,placement.rotation_z},valid);
        auto graph=body_dialog_preview_->body_history;
        if (value.name.empty()) value.name=graph.find(edited)->name;
        graph.update_body(std::move(value));graph.activate(edited);
        body_dialog_preview_->set_body_history(std::move(graph));
        const auto active_reference=pending_primitive_reference_index_;
        preserve_view_on_refresh_=true;refresh_scene();
        if (active_reference) start_primitive_reference_selection(*active_reference);
        viewer_->set_constraint_reference_highlights({},highlighted_reference_edge_keys(*dialog));
    };
    dialog->set_preview_callback(preview);
    connect(dialog,&QDialog::finished,this,[this,dialog] {
        if (primitive_reference_dialog_!=dialog) return;
        primitive_reference_dialog_=nullptr;primitive_reference_geometry_={};
        pending_primitive_reference_index_.reset();primitive_reference_auto_advance_=false;
        tree_->setProperty("commandSelectionActive",false);
        viewer_->set_constraint_reference_highlights({},{});
    });
    finish_body_dialog(dialog);
    preview(dialog->pending_value());
    const auto first = dialog->first_empty_position_index();
    if (first < 3) start_primitive_reference_selection(first, true);
    else set_primitive_properties_dimension_selection();
}

void AssemblyWorkspaceWindow::show_body_boolean_properties(const std::string& id) {
    if (properties_dialog_) return;
    const auto document_id = workspace_.active_document_id();
    auto* part = workspace_.open_part(document_id);
    if (!part || document_id != workspace_.displayed_document_id()) return;
    auto pending = part->session.document();
    auto graph = pending.body_history;
    const auto boundary = id.empty() ? graph.insertion_cursor() : static_cast<std::size_t>(
        std::distance(graph.order().begin(), std::ranges::find(graph.order(), id)));
    auto available = graph.available_before(boundary);
    if (available.size() < 2) { state_->setText(tr("Boolean potřebuje dva dostupné výsledky těles.")); return; }
    auto edited = id;
    if (edited.empty()) edited = graph.create_boolean("Boolean", zima::kernel::BodyCombination::Subtract,
        available[0], available[1]);
    const auto* initial = graph.find_boolean(edited);
    if (!initial) return;
    std::vector<std::pair<std::string,std::string>> inputs;
    for (const auto& input : available) inputs.emplace_back(input,
        graph.find(input) ? graph.find(input)->name : graph.find_boolean(input)->name);
    pending.set_body_history(graph);
    body_dialog_preview_ = pending; body_dialog_step_id_ = edited; body_dialog_context_ = available;
    auto* dialog = new BodyBooleanPropertiesDialog(*initial, inputs,
        [this, document_id, graph](zima::document::BodyBoolean value) mutable {
            auto* current = workspace_.open_part(document_id);
            if (!current) throw std::runtime_error("Dokument již není otevřený.");
            auto next = current->session.document();
            auto updated = graph; updated.update_boolean(std::move(value)); updated.activate({});
            next.set_body_history(std::move(updated));
            auto calculated = calculate_part_with_resolved_references(next, &current->session.calculated_boundaries());
            current->session.commit(std::move(next), std::move(calculated));
        }, this);
    finish_body_dialog(dialog);
}

} // namespace zima::app
