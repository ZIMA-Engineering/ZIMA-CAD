#include "workspace_internal.hpp"

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
        if (!workspace::set_part_history_suppressed(*part, kernel_, container_id,
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
            return item->parent()->data(0,Qt::UserRole+1).toString().toStdString()==active_occurrence_path_ &&
                item->data(0,Qt::UserRole+4).toString().toStdString()==workspace_.active_document_id() &&
                assembly->session.document().find_occurrence(id);
        if (kind=="assembly-cut") return assembly->session.document().find_cut(id)!=nullptr;
        if (kind=="assembly-construction") return assembly->session.document().find_construction(id)!=nullptr;
        if (kind=="assembly-sketch") return std::ranges::any_of(assembly->session.document().sketches,
            [&](const auto& sketch){return sketch.id==id && sketch.owner_container_id.empty();});
    }
    return false;
}

bool AssemblyWorkspaceWindow::reorder_part_history(const std::string& id,const std::string& before,bool commit) {
    auto* part=workspace_.open_part(workspace_.active_document_id());
    if (!part || properties_dialog_ || !active_sketch_id_.empty()) return false;
    try {
        const bool changed=workspace::move_part_history(*part,kernel_,id,before,commit);
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
    auto* assembly=workspace_.open_assembly(workspace_.active_document_id());
    if (!assembly) return false;
    const auto kind=item->data(0,Qt::UserRole+3).toString();
    const auto& original=assembly->session.document();
    const bool components=kind=="part-occurrence" || kind=="assembly-occurrence";
    std::vector<std::string> order;
    HistoryDependencies dependencies;
    if (components) {
        for (const auto& component : original.components) order.push_back(component.occurrence_id);
        dependencies=assembly_component_dependencies(original);
    } else {
        document::PartDocument carrier;
        carrier.constructions=original.constructions;carrier.sketches=original.sketches;
        for (const auto& cut : original.cuts) carrier.history.push_back(cut.definition);
        dependencies=part_history_dependencies(carrier);
        if (kind=="assembly-cut") for (const auto& cut : original.cuts) order.push_back(cut.definition.id);
        else if (kind=="assembly-construction") for (const auto& object : original.constructions) order.push_back(object.id);
        else for (const auto& sketch : original.sketches) if (sketch.owner_container_id.empty()) order.push_back(sketch.id);
    }
    const auto reordered=reordered_history(order,id,before.toStdString());
    if (!history_order_preserves_dependencies(order,reordered,dependencies)) return false;
    if (!commit || reordered==order) return true;
    try {
        auto next=original;
        if (components) sort_history_records(next.components,reordered,[](const auto& c){return c.occurrence_id;});
        else if (kind=="assembly-cut") {
            if (!original.cuts.empty()) {
                const auto& inputs=original.cuts.front().input_component_bodies;
                for (auto& component : next.components) {
                    const auto input=inputs.find(component.occurrence_id);
                    if (input==inputs.end()) throw std::runtime_error("Chybí uložené vstupní těleso operace.");
                    component.calculated_source=input->second;
                }
            }
            sort_history_records(next.cuts,reordered,[](const auto& c){return c.definition.id;});
            calculate_assembly_cuts(next);
            document::PartDocument old_cut_references,new_cut_references;
            old_cut_references.sketches=original.sketches;
            new_cut_references.sketches=next.sketches;
            for (const auto& cut : original.cuts) {
                old_cut_references.history.push_back(cut.definition);
                const auto* changed=next.find_cut(cut.definition.id);
                if (!changed || (cut.definition.placement.reference_valid && !changed->definition.placement.reference_valid))
                    throw std::runtime_error("Přesun by poškodil referenci operace sestavy.");
            }
            for (const auto& cut : next.cuts) new_cut_references.history.push_back(cut.definition);
            const auto old_graph=part_history_dependency_graph(old_cut_references);
            const auto new_graph=part_history_dependency_graph(new_cut_references);
            if (!std::includes(new_graph.references.begin(),new_graph.references.end(),
                    old_graph.references.begin(),old_graph.references.end()))
                throw std::runtime_error("Přesun by odstranil uloženou referenci operace sestavy.");
            const auto old_refs=assembly_reference_index(original),new_refs=assembly_reference_index(next);
            if (!std::includes(new_refs.keys.begin(),new_refs.keys.end(),old_refs.keys.begin(),old_refs.keys.end()))
                throw std::runtime_error("Přesun by odstranil referenční geometrii sestavy.");
        } else if (kind=="assembly-construction") sort_history_records(next.constructions,reordered,[](const auto& c){return c.id;});
        else sort_history_records(next.sketches,reordered,[](const auto& c){return c.id;});
        assembly->session.commit(std::move(next));
        preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
        state_->setText(tr("Pořadí změněno. Operaci lze vrátit přes Zpět."));
        return true;
    } catch (const std::exception& error) {
        state_->setText(tr("Pořadí nebylo změněno: %1").arg(tr(error.what())));
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
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            const auto assembly_id = assembly->session.document().document_id;
            workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
            assembly = workspace_.open_assembly(assembly_id);
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            std::erase_if(next.cuts, [&](const auto& cut) {
                return cut.definition.id == object_id;
            });
            calculate_assembly_cuts(next);
            assembly->session.commit(std::move(next));
        } else if (kind == QStringLiteral("assembly-sketch")) {
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            if (std::any_of(next.cuts.begin(), next.cuts.end(), [&](const auto& cut) {
                    const auto& definition = cut.definition;
                    return (definition.feature_kind ==
                                zima::document::FeatureKind::Extrusion &&
                            definition.extrusion.sketch_id == object_id) ||
                        (definition.feature_kind ==
                                zima::document::FeatureKind::Revolution &&
                            definition.revolution.sketch_id == object_id);
                })) {
                throw std::runtime_error("Assembly sketch is still used by a cut");
            }
            std::erase_if(next.sketches,
                [&](const auto& sketch) { return sketch.id == object_id; });
            assembly->session.commit(std::move(next));
        } else if (kind == QStringLiteral("assembly-construction")) {
            auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
            if (assembly == nullptr) return;
            auto next = assembly->session.document();
            const auto document_states = workspace_.documents();
            const bool used_by_external_sketch = std::any_of(
                document_states.begin(), document_states.end(),
                [&](const auto& state) {
                    const auto* part = std::get_if<zima::workspace::PartState>(&state);
                    return part != nullptr && std::any_of(
                        part->session.document().sketches.begin(),
                        part->session.document().sketches.end(), [&](const auto& sketch) {
                            return std::any_of(sketch.external_references.begin(),
                                sketch.external_references.end(), [&](const auto& reference) {
                                    return reference.source_document_id == next.document_id &&
                                        reference.source_owner_id == object_id;
                                });
                        });
                });
            if (std::any_of(next.components.begin(), next.components.end(), [&](const auto& component) {
                    return std::any_of(component.placement_references.begin(),
                        component.placement_references.end(), [&](const auto& row) {
                            return row.component_reference.owner_id == object_id ||
                                row.target_reference.owner_id == object_id;
                        });
                }) || std::any_of(next.constructions.begin(), next.constructions.end(),
                    [&](const auto& construction) {
                        return construction.id != object_id &&
                            std::any_of(construction.references.begin(),
                                construction.references.end(), [&](const auto& reference) {
                                    return reference.owner_id == object_id;
                                });
                    }) || used_by_external_sketch) {
                throw std::runtime_error(
                    "Assembly datum is still used by a placement reference or another datum");
            }
            const auto old_size = next.constructions.size();
            std::erase_if(next.constructions,
                [&](const auto& object) { return object.id == object_id; });
            if (next.constructions.size() == old_size) return;
            next.resolve_constructions();
            next.calculate_placement_references();
            assembly->session.commit(std::move(next));
        } else {
            auto* part = workspace_.open_part(workspace_.active_document_id());
            if (part == nullptr) return;
            workspace::delete_part_history(*part,kernel_,object_id);
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
