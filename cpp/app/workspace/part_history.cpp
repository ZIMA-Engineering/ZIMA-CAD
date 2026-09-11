#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::toggle_part_container_suppressed(
    const std::string& container_id) {
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr || properties_dialog_ != nullptr) return;
    try {
        auto next = part->session.document();
        auto* container = next.find_container(container_id);
        if (container != nullptr) {
            container->suppressed = !container->suppressed;
        } else if (auto* construction = next.find_construction(container_id)) {
            construction->suppressed = !construction->suppressed;
        } else {
            const auto sketch = std::find_if(next.sketches.begin(), next.sketches.end(),
                [&](const auto& value) { return value.id == container_id; });
            if (sketch == next.sketches.end()) return;
            sketch->suppressed = !sketch->suppressed;
        }
        const auto& previous = part->session.calculated_boundaries();
        auto calculated = calculate_part(next, &previous);
        next.resolve_constructions(calculated.empty()
            ? zima::kernel::ViewerReferenceGeometry{}
            : calculated.back().mesh.original_references);
        static_cast<void>(refresh_sketch_external_references(next, calculated));
        part->session.commit(std::move(next), std::move(calculated));
        refresh_tabs();
        refresh_scene();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Potlačení nelze změnit"), error.what());
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
    const auto& original=part->session.document();
    const bool body_step = original.body_history.find(id) || original.body_history.find_boolean(id);
    const auto* owner = original.body_history.owner(id);
    std::vector<std::string> order;
    if (body_step) order=original.body_history.order();
    else if (owner) {
        if (!before.empty() && original.body_history.owner(before)!=owner) return false;
        for (const auto& entry : owner->entries) order.push_back(entry.id);
    } else for (const auto& entry : original.history_order) order.push_back(entry.id);
    if (std::ranges::find(order,id)==order.end() || (!before.empty() && std::ranges::find(order,before)==order.end())) return false;
    const auto reordered=reordered_history(order,id,before);
    try {
    if (!history_order_preserves_dependencies(order,reordered,
            body_step ? part_body_dependencies(original) : part_history_dependencies(original))) {
        if (commit) state_->setText(tr("Přesun není možný: porušil by závislost nebo referenci skici."));
        return false;
    }
        std::optional<zima::document::BodyHistoryGraph> changed_graph;
        if (body_step) {
            auto graph=original.body_history;
            graph.move_step(id,static_cast<std::size_t>(std::distance(reordered.begin(),std::ranges::find(reordered,id))));
            changed_graph=std::move(graph);
        } else if (owner) {
            auto graph=original.body_history;
            auto body=*owner;
            sort_history_records(body.entries,reordered,[](const auto& entry){return entry.id;});
            for (const auto& entry : body.entries) {
                const auto* feature=entry.kind==zima::document::PartHistoryKind::Feature ? original.find_container(entry.id) : nullptr;
                if (!feature || feature->suppressed || feature->feature_kind==zima::document::FeatureKind::Sketch) continue;
                if (feature->combine_mode==zima::document::CombineMode::Subtract)
                    throw std::invalid_argument("První prvek tělesa nemůže být odečet.");
                break;
            }
            graph.update_body(std::move(body));changed_graph=std::move(graph);
        }
        if (!commit || order==reordered) return true;
        auto next=original;
        if (changed_graph) next.set_body_history(std::move(*changed_graph));
        std::vector<std::string> storage_order;
        if (body_step || owner) for (const auto& entry : next.history_order) storage_order.push_back(entry.id);
        else storage_order=reordered;
        sort_history_records(next.history_order,storage_order,[](const auto& entry){return entry.id;});
        sort_history_records(next.history,storage_order,[](const auto& entry){return entry.id;});
        sort_history_records(next.constructions,storage_order,[](const auto& entry){return entry.id;});
        sort_history_records(next.sketches,storage_order,[](const auto& entry){return entry.owner_container_id.empty() ? entry.id : entry.owner_container_id;});
        auto calculated=calculate_part_with_resolved_references(next,&part->session.calculated_boundaries());
        // Persist the calculation for the exact resolved parameters. Placement
        // equality treats signed zero as equal, whereas persisted fingerprints
        // retain the floating-point bits. Do not change placement solving here.
        const auto resolved_operations=next.kernel_operations();
        bool exact_calculation=calculated.size()==resolved_operations.size();
        for (std::size_t i=0;i<calculated.size() && exact_calculation;++i)
            exact_calculation=calculated[i].source_fingerprint==kernel::history_fingerprint(resolved_operations,i+1);
        if (!exact_calculation) calculated=calculate_part(next);

        const auto old_graph=part_history_dependency_graph(original);
        const auto new_graph=part_history_dependency_graph(next);
        if (!std::includes(new_graph.references.begin(),new_graph.references.end(),
                old_graph.references.begin(),old_graph.references.end()))
            throw std::runtime_error("Přesun by odstranil uloženou referenci.");
        TreeReferenceIndex old_refs,new_refs;
        const auto& previous=part->session.calculated_boundaries();
        if (!previous.empty()) old_refs.add_geometry(previous.back().mesh.original_references);
        if (!calculated.empty()) new_refs.add_geometry(calculated.back().mesh.original_references);
        if (!std::includes(new_refs.keys.begin(),new_refs.keys.end(),old_refs.keys.begin(),old_refs.keys.end()))
            throw std::runtime_error("Přesun by odstranil původní geometrii používanou pro reference.");
        for (const auto& object : original.constructions) {
            const auto* changed=next.find_construction(object.id);
            if (changed && object.reference_valid && !changed->reference_valid)
                throw std::runtime_error("Přesun by poškodil referenci konstrukčního prvku.");
        }
        for (const auto& feature : original.history) {
            const auto* changed=next.find_container(feature.id);
            if (changed && feature.placement.reference_valid && !changed->placement.reference_valid)
                throw std::runtime_error("Přesun by poškodil referenci kontejneru.");
        }
        for (const auto& sketch : next.sketches) {
            const auto old=std::ranges::find_if(original.sketches,[&](const auto& s){return s.id==sketch.id;});
            if (old==original.sketches.end()) continue;
            for (const auto& ref : sketch.external_references) {
                const auto old_ref=std::ranges::find_if(old->external_references,[&](const auto& r){return r.id==ref.id;});
                if (ref.broken && old_ref!=old->external_references.end() && !old_ref->broken)
                    throw std::runtime_error("Přesun by poškodil externí referenci skici.");
            }
        }
        part->session.commit(std::move(next),std::move(calculated));
        preserve_view_on_refresh_=true;
        refresh_tabs();refresh_scene();
        state_->setText(tr("Pořadí historie změněno. Operaci lze vrátit přes Zpět."));
        return true;
    } catch (const std::exception& error) {
        if (commit) state_->setText(tr("Pořadí nebylo změněno: %1").arg(QString::fromUtf8(error.what())));
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
        state_->setText(tr("Pořadí nebylo změněno: %1").arg(QString::fromUtf8(error.what())));
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
            auto next = part->session.document();
            const auto rollback = part->session.rollback_boundary(object_id);
            next.erase_history_object(object_id);
            if (rollback && rollback->input_body) {
                static_cast<void>(restore_surviving_edge_references_after_history_delete(
                    next, object_id, *rollback->input_body,
                    part->session.calculated_boundaries()));
            }
            auto calculated = calculate_part_with_resolved_references(next);
            if (!calculated.empty() && !calculated.back().calculation_errors.empty())
                calculation_issue = tr("Platná předcházející geometrie zůstala zachována. Chyby jsou označeny ve stromu.");
            part->session.commit(std::move(next), std::move(calculated));
            if (active_sketch_id_ == object_id) active_sketch_id_.clear();
            if (selected_sketch_id_ == object_id) selected_sketch_id_.clear();
        }
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
        if (!calculation_issue.isEmpty())
            state_->setText(tr("Objekt odstraněn. Navazující geometrii nelze vypočítat: %1").arg(calculation_issue));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Objekt nelze odstranit"), error.what());
    }
}

} // namespace zima::app
