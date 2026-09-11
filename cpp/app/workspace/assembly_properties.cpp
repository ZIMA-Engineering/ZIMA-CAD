#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::select_container(const std::string& container_id) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    QTreeWidgetItem* fallback{};
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole).toString().toStdString() == container_id) {
            const auto role = item->data(0, Qt::UserRole + 3).toString();
            // A Sketch and its owned Plane intentionally share the Sketch
            // ID in the Tree. View selection of the complete Sketch must
            // synchronize to the Sketch row, not stop on its Plane sibling.
            if (role == QStringLiteral("part-sketch") ||
                role == QStringLiteral("assembly-sketch")) {
                selected_sketch_id_ = container_id;
                tree_->setCurrentItem(item);
                return;
            }
            if (fallback == nullptr) fallback = item;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
    if (fallback != nullptr) tree_->setCurrentItem(fallback);
}

void AssemblyWorkspaceWindow::select_occurrence(const std::string& instance_path) {
    auto* root = tree_->topLevelItem(0);
    if (root == nullptr) return;
    std::vector<QTreeWidgetItem*> pending{root};
    while (!pending.empty()) {
        auto* item = pending.back();
        pending.pop_back();
        if (item->data(0, Qt::UserRole + 1).toString().toStdString() == instance_path) {
            tree_->setCurrentItem(item);
            return;
        }
        for (int index = 0; index < item->childCount(); ++index) {
            pending.push_back(item->child(index));
        }
    }
}

void AssemblyWorkspaceWindow::show_component_properties(
    const std::string& selected_path, bool start_reference_entry) {
    if (properties_dialog_ != nullptr) return;
    std::string instance_path;
    try{instance_path=workspace_.derived_source_path(workspace_.displayed_document_id(),zima::assembly::InstancePath::decode(selected_path)).encoded();}
    catch(const std::exception&){return;}
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            workspace_.displayed_document_id(),
            zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return;
    }
    if (!address) return;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    auto* dialog = new ComponentPropertiesDialog(
        *occurrence,
        [this, assembly_id = address->owner_assembly_document_id]
        (zima::assembly::PartOccurrence committed) {
            auto* assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) {
                throw std::runtime_error("Assembly is no longer open");
            }
            auto next = assembly->session.document();
            auto found = std::find_if(next.components.begin(), next.components.end(),
                [&](const auto& item) { return item.occurrence_id == committed.occurrence_id; });
            if (found == next.components.end()) {
                throw std::runtime_error("Assembly occurrence no longer exists");
            }
            *found = std::move(committed);
            // Solve all embedded rows together before committing the occurrence.
            next.calculate_placement_references();
            assembly->session.commit(std::move(next));
        }, this);
    dialog->set_reference_request_callback(
        [this](std::size_t index, bool component_side) {
            start_component_placement_reference_selection(index, component_side);
        });
    const auto reference_scene_prefix =
        zima::assembly::InstancePath::decode(instance_path)
            .parent().value_or(zima::assembly::InstancePath{});
    dialog->set_reference_highlights_changed_callback(
        [this, dialog, reference_scene_prefix] {
            std::set<zima::viewer::EdgeKey> keys;
            for (const auto& reference : dialog->highlighted_references()) {
                auto path = reference_scene_prefix;
                for (const auto& occurrence_id :
                     reference.instance_path.occurrence_ids) {
                    path = path.child(occurrence_id);
                }
                keys.insert({reference.owner_id, reference.semantic_key,
                    path.encoded()});
            }
            viewer_->set_constraint_reference_highlights({}, std::move(keys));
        });
    dialog->set_reference_measure_callback([this,assembly_id=address->owner_assembly_document_id](const auto& component,const auto& row)->std::optional<double> {
        const auto* assembly=workspace_.open_assembly(assembly_id);if(!assembly)return {};
        auto geometry=assembly->session.document();auto* occurrence=geometry.find_occurrence(component.occurrence_id);if(!occurrence)return {};
        *occurrence=component;
        return geometry.measure_placement_reference(row);
    });
    dialog->set_reference_label_resolver(
        [geometry=assembly->session.document().build_scene().original_references](const auto& reference) {
            return reference_display_label(reference,geometry);
        });
    component_placement_dialog_ = dialog;
    component_placement_assembly_document_id_ = address->owner_assembly_document_id;
    component_placement_occurrence_id_ = address->occurrence_id;
    viewer_->set_component_origin_handle(zima::viewer::EdgeKey{
        occurrence->source_document_id + ":origin", "origin:point", instance_path});
    properties_dialog_ = dialog;

    properties_dialog_instance_path_ = instance_path;
    viewer_->set_editing_origin_visible(true);
    preserve_view_on_refresh_ = true;
    refresh_scene();
    dialog->set_preview_callback(
        [this, dialog, reference_scene_prefix, assembly_id = address->owner_assembly_document_id,
         occurrence_id = address->occurrence_id](const auto& preview) {
            const auto* assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            auto found = std::find_if(next.components.begin(), next.components.end(),
                [&](const auto& item) {
                    return item.occurrence_id == occurrence_id;
                });
            if (found == next.components.end()) return;
            *found = preview;
            try {
                next.calculate_placement_references();
                dialog->set_solved_placement(next.find_occurrence(occurrence_id)->placement,
                    next.component_constraint_state(occurrence_id));
                viewer_->set_mesh(reference_scene_prefix.occurrence_ids.empty() ? next.build_scene() :
                    workspace_.build_scene_with_assembly_override(workspace_.displayed_document_id(),
                        reference_scene_prefix, next), false);
            } catch (const std::exception& error) {
                dialog->set_placement_error(QString::fromUtf8(error.what()));
            }
        });
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
        properties_dialog_instance_path_.clear();
        component_placement_dialog_ = nullptr;
        component_placement_assembly_document_id_.clear();
        component_placement_occurrence_id_.clear();
        pending_component_placement_index_.reset();
        component_placement_auto_advance_ = false;
        tree_->setProperty("commandSelectionActive", false);
        viewer_->set_selection_contract({});
        viewer_->set_candidate_filter({});
        viewer_->clear_selection();
        viewer_->set_constraint_reference_highlights({}, {});
        viewer_->set_editing_origin_visible(false);
        viewer_->set_component_origin_handle(std::nullopt);
        component_drag_document_.reset();component_drag_preview_.reset();
        component_drag_occurrence_id_.clear();component_drag_document_id_.clear();component_drag_instance_path_.clear();
        refresh_scene();
    });
    dialog->show();
    if (start_reference_entry)
        start_component_placement_reference_selection(0, true, true);
}

} // namespace zima::app
