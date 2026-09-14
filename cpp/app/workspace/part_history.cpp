#include <zima/workspace/assembly_history_operations.hpp>
#include <zima/workspace/assembly_cut_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include "workspace_internal.hpp"
#include <zima/workspace/construction_removal.hpp>

namespace zima::app {
using namespace workspace_detail;
using workspace::reordered_history;
using workspace::history_order_preserves_dependencies;
using workspace::assembly_component_dependencies;
using workspace::sort_history_records;
using workspace::HistoryDependencies;
using workspace::part_history_dependencies;
using workspace::part_history_dependency_graph;


void AssemblyWorkspaceWindow::toggle_part_container_suppressed(
    const std::string& container_id) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        if (!workspace::set_part_history_suppressed(workspace_,workspace_.active_document_id(), kernel_, container_id,
                !workspace::part_history_suppressed(part->session.document(), container_id))) return;
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Potlačení nelze změnit"), tr(error.what()));
    }
}

bool AssemblyWorkspaceWindow::part_element_context_menu_enabled(const std::string& owner_id) const {
    const auto* part = workspace_.open_part(workspace_.active_document_id());
    if (!part) return true;
    const auto& document = part->session.document();
    if (document.body_history.find(owner_id)) return true;
    const auto* body = document.body_owner_for_object(owner_id);
    return !body || body->scope.id == document.body_history.active_body_id();
}

bool AssemblyWorkspaceWindow::tree_item_context_menu_enabled(QTreeWidgetItem* item) const {
    if (!item) return false;
    // Dimensions, constraints and feature subcomponents inherit the history
    // owner's editing scope. Body-level activation remains available.
    for (auto* entry = item; entry; entry = entry->parent()) {
        if (!part_element_context_menu_enabled(entry->data(0, Qt::UserRole).toString().toStdString()))
            return false;
    }
    return true;
}

bool AssemblyWorkspaceWindow::tree_item_reorder_enabled(QTreeWidgetItem* item) const {
    if (!item || !item->parent() || properties_dialog_ || !active_sketch_id_.empty() ||
        part_rollback_ || assembly_cut_rollback_ || tree_->property("commandSelectionActive").toBool()) return false;
    const auto kind=item->data(0,Qt::UserRole+3).toString();
    const auto id=item->data(0,Qt::UserRole).toString().toStdString();
    if (const auto* part=workspace_.open_part(workspace_.active_document_id())) {
        const auto& graph = part->session.document().body_history;
        if (kind=="part-body" || kind=="part-body-boolean")
            return workspace_.displayed_document_id()==workspace_.active_document_id() &&
                (graph.find(id) || graph.find_boolean(id));
        if (kind!="part-container" && kind!="part-sketch" && kind!="part-construction") return false;
        if (!graph.bodies().empty()) {
            const auto* owner=graph.owner(id);
            if (!owner || owner->scope.id!=graph.active_body_id()) return false;
        }
        return std::ranges::any_of(part->session.document().history_order,[&](const auto& entry){return entry.id==id;});
    }
    if (const auto* assembly=workspace_.open_assembly(workspace_.active_document_id())) {
        if (kind=="part-occurrence" || kind=="assembly-occurrence")
            return item->parent()->data(0,Qt::UserRole+1).toString().toStdString()==workspace_.active_occurrence_path() &&
                item->data(0,Qt::UserRole+4).toString().toStdString()==workspace_.active_document_id() &&
                assembly->session.document().find_occurrence(id);
        if (kind=="assembly-cut") return assembly->session.document().find_cut(id)!=nullptr;
        if (kind=="assembly-construction") return assembly->session.document().find_construction(id)!=nullptr;
        if (kind=="assembly-sketch-container") return assembly->session.document().find_sketch_container(id)!=nullptr;
    }
    return false;
}

bool AssemblyWorkspaceWindow::reorder_part_history(const std::string& id,const std::string& before,bool commit) {
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if (!part || properties_dialog_ || !active_sketch_id_.empty()) return false;
    try {
        const bool changed=workspace::move_part_history(workspace_,workspace_.active_document_id(),kernel_,id,before,commit);
        if (!commit || !changed) return true;
        preserve_view_on_refresh_=true;
        refresh_tabs();refresh_scene();
        state_->setText(tr("Pořadí historie změněno. Operaci lze vrátit přes Zpět."));
        return true;
    } catch (const std::exception& error) {
        if (commit) state_->setText(tr("Pořadí nebylo změněno: %1").arg(tr(error.what())));
        return false;
    }
}

bool AssemblyWorkspaceWindow::reorder_tree_item(QTreeWidgetItem* item,const QString& before,bool commit) {
    if (!tree_item_reorder_enabled(item)) return false;
    const auto id=item->data(0,Qt::UserRole).toString().toStdString();
    if (workspace_.open_part(workspace_.active_document_id())) return reorder_part_history(id,before.toStdString(),commit);
    try {
        const bool changed=workspace::move_assembly_history(workspace_,workspace_.active_document_id(),
            kernel_,id,before.toStdString(),commit);
        if (commit && changed) {
            preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
            state_->setText(tr("Pořadí změněno. Operaci lze vrátit přes Zpět."));
        }
        return true;
    } catch (const std::exception& error) {
        if (commit) state_->setText(tr("Pořadí nebylo změněno: %1").arg(tr(error.what())));
        return false;
    }
}

void AssemblyWorkspaceWindow::move_part_container(const std::string& id,int direction) {
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if (!part || (direction!=-1 && direction!=1)) return;
    const auto& order=part->session.document().history_order;
    const auto source=std::ranges::find_if(order,[&](const auto& entry){return entry.id==id;});
    if (source==order.end()) return;
    const auto index=static_cast<std::size_t>(std::distance(order.begin(),source));
    if (direction<0 && index>0) reorder_part_history(id,order[index-1].id,true);
    if (direction>0 && index+1<order.size()) reorder_part_history(id,index+2<order.size() ? order[index+2].id : std::string{},true);
}

void AssemblyWorkspaceWindow::delete_part_object(
    const std::string& object_id, const QString& kind,
    bool ask_confirmation) {
    if (properties_dialog_ != nullptr || object_id.empty()) return;
    if (ask_confirmation && QMessageBox::question(this, tr("Odstranit objekt"),
            tr("Opravdu chcete vybraný objekt odstranit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) return;
    QString calculation_issue;
    try {
        if (kind == QStringLiteral("assembly-cut")) {
            workspace::remove_assembly_cut(workspace_,kernel_,workspace_.active_document_id(),object_id);
        } else if (kind == QStringLiteral("assembly-sketch") || kind == QStringLiteral("assembly-sketch-container")) {
            const auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (!assembly) return;
            const auto& sketches = assembly->session.document().sketches;
            const auto sketch = std::ranges::find_if(sketches, [&](const auto& value) {
                return kind == QStringLiteral("assembly-sketch") ? value.id == object_id : value.owner_container_id == object_id;
            });
            if (sketch == sketches.end()) return;
            workspace::delete_document_sketch(workspace_, kernel_, workspace_.active_document_id(), sketch->id);
        } else if (kind == QStringLiteral("assembly-construction")) {
            workspace::delete_construction(workspace_,kernel_,workspace_.active_document_id(),object_id);
        } else {
            auto* part = workspace_.open_part(workspace_.active_document_id());
            if (part == nullptr) return;
            if(kind==QStringLiteral("part-construction"))workspace::delete_construction(workspace_,kernel_,workspace_.active_document_id(),object_id);
            else workspace::delete_part_history(workspace_,workspace_.active_document_id(),kernel_,object_id);
            part=workspace_.open_part(workspace_.active_document_id());
            const auto& calculated=part->session.calculated_boundaries();
            if (!calculated.empty() && !calculated.back().calculation_errors.empty())
                calculation_issue = tr("Platná předcházející geometrie zůstala zachována. Chyby jsou označeny ve stromu.");
            if (active_sketch_id_ == object_id) active_sketch_id_.clear();
            if (selected_sketch_id_ == object_id) selected_sketch_id_.clear();
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (!calculation_issue.isEmpty())
            state_->setText(tr("Objekt odstraněn. Navazující geometrii nelze vypočítat: %1").arg(calculation_issue));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Objekt nelze odstranit"), tr(error.what()));
    }
}

} // namespace zima::app
