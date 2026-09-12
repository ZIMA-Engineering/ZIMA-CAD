#include "workspace_internal.hpp"
#include <zima/workspace/component_source_operations.hpp>
#include <zima/document/file_path.hpp>

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::show_tree_item_properties(QTreeWidgetItem* item) {
    if(item&&item->data(0,Qt::UserRole+3)=="document-measurement"){show_measurement(item->data(0,Qt::UserRole).toString().toStdString());return;}
    if(item&&item->data(0,Qt::UserRole+3)=="document-section"){show_section_properties(item->data(0,Qt::UserRole).toString().toStdString());return;}
    if(item && item->data(0,Qt::UserRole+3).toString()=="template-image") {
        show_template_image_properties(item->data(0,Qt::UserRole).toString().toStdString());return;
    }
    if(item && item->data(0,Qt::UserRole+3).toString()=="template-repeat-region") {
        show_template_region_properties(item->data(0,Qt::UserRole).toString().toStdString());return;
    }
    if(item&&item->data(0,Qt::UserRole+3).toString()=="part-sweep2d-sketch"){
        const auto stage=item->data(0,Qt::UserRole+6).toUInt();
        show_sweep2d_properties(item->data(0,Qt::UserRole).toString().toStdString());
        if(auto* dialog=dynamic_cast<Sweep2DDialog*>(properties_dialog_))dialog->edit_sketch(stage);
        return;
    }
    if(item&&item->data(0,Qt::UserRole+3).toString()=="part-helical-sketch"){
        const auto stage=item->data(0,Qt::UserRole+6).toUInt();
        show_helical_sweep_properties(item->data(0,Qt::UserRole).toString().toStdString());
        if(auto* dialog=dynamic_cast<HelicalSweepDialog*>(properties_dialog_))dialog->edit_sketch(stage);
        return;
    }

    if (item == nullptr) return;
    const auto kind = item->data(0, Qt::UserRole + 3).toString();
    const auto id = item->data(0, Qt::UserRole).toString().toStdString();
    if(kind=="mirror-source"){show_derived_source_properties(id);return;}
    if(kind=="part-occurrence"||kind=="assembly-occurrence") {
        const auto* owner=workspace_.open_assembly(item->data(0,Qt::UserRole+4).toString().toStdString());
        const auto* occurrence=owner?owner->session.document().find_occurrence(id):nullptr;
        if(occurrence&&occurrence->derived_copy&&owner->session.document().document_id==workspace_.active_document_id()) {
            show_derived_copy_properties(id);return;
        }
    }
    if (kind == "part-body") {
        const auto* part=workspace_.open_part(workspace_.active_document_id());
        const auto* body=part?part->session.document().body_history.find(id):nullptr;
        if(body&&body->derived_copy)show_derived_copy_properties(id);else show_body_properties(id);return;
    }
    if (kind == "part-body-boolean") { show_body_boolean_properties(id); return; }

    const auto instance_path =
        item->data(0, Qt::UserRole + 1).toString().toStdString();
    if (kind == QStringLiteral("part-container") ||
        kind == QStringLiteral("part-hole-component") ||
        kind == QStringLiteral("part-opening-component") ||
        kind == QStringLiteral("part-treatment-component") ||
        kind == QStringLiteral("part-construction") ||
        kind == QStringLiteral("assembly-construction") ||
        kind == QStringLiteral("part-sketch") ||
        kind == QStringLiteral("assembly-sketch") ||
        kind == QStringLiteral("assembly-cut")) {
        construction_dimension_object_id_ = id;
        preserve_view_on_refresh_ = true;
        refresh_scene();
    }
    if (kind == QStringLiteral("part-container") ||
        kind == QStringLiteral("part-hole-component") ||
        kind == QStringLiteral("part-opening-component") ||
        kind == QStringLiteral("part-treatment-component")) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* container = part == nullptr
            ? nullptr : part->session.document().find_container(id);
        if (container != nullptr && container->feature_kind ==
                zima::document::FeatureKind::Sketch) {
            const auto sketch = std::find_if(
                part->session.document().sketches.begin(),
                part->session.document().sketches.end(),
                [&](const auto& value) {
                    return value.owner_container_id == container->id;
                });
            if (sketch != part->session.document().sketches.end()) {
                show_sketch_properties(sketch->id);
            }
        } else if (container != nullptr) {
            show_primitive_properties(container->feature_kind, id);
        }
    } else if (kind == QStringLiteral("assembly-cut")) {
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* cut = assembly == nullptr
            ? nullptr : assembly->session.document().find_cut(id);
        if (cut != nullptr) show_primitive_properties(cut->definition.feature_kind, id);
    } else if (kind == QStringLiteral("part-construction") ||
               kind == QStringLiteral("assembly-construction")) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto* assembly =
            workspace_.open_assembly(workspace_.active_document_id());
        const auto* object = part != nullptr
            ? part->session.document().find_construction(id)
            : assembly != nullptr
                ? assembly->session.document().find_construction(id) : nullptr;
        if (object != nullptr) show_construction_properties(object->kind, id);
    } else if (kind == QStringLiteral("curve3d-point")) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        const auto parent_id =
            item->data(0, Qt::UserRole + 4).toString().toStdString();
        const auto* curve = part == nullptr
            ? nullptr : part->session.document().find_construction(parent_id);
        if (curve != nullptr &&
            (curve->kind == zima::document::ConstructionKind::Curve3D)) {
            const auto found = std::find_if(
                curve->curve_points.begin(), curve->curve_points.end(),
                [&id](const auto& point) { return point.id == id; });
            if (found != curve->curve_points.end()) {
                const auto index = static_cast<std::size_t>(
                    std::distance(curve->curve_points.begin(), found));
                show_construction_properties(curve->kind, parent_id);
                if (auto* dialog = dynamic_cast<ConstructionPropertiesDialog*>(
                        properties_dialog_)) {
                    show_curve_point_properties(dialog, index);
                }
            }
        }
    } else if (kind == QStringLiteral("part-sketch") ||
               kind == QStringLiteral("assembly-sketch")) {
        show_sketch_properties(id);
    } else if (kind == QStringLiteral("part-sketch-dimension")) {
        show_sketch_dimension_properties(
            item->data(0, Qt::UserRole + 4).toString().toStdString(), id);
    } else if (!instance_path.empty()) {
        show_component_properties(instance_path);
    }
}

bool AssemblyWorkspaceWindow::activate_occurrence_for_test(
    const std::string& selected_path) {
    const std::string top_assembly_id = workspace_.displayed_document_id();
    std::string instance_path;
    try{instance_path=workspace_.derived_source_path(top_assembly_id,zima::assembly::InstancePath::decode(selected_path)).encoded();}
    catch(const std::exception&){return false;}
    std::optional<zima::workspace::OccurrenceAddress> address;
    try {
        address = workspace_.resolve_occurrence(
            top_assembly_id, zima::assembly::InstancePath::decode(instance_path));
    } catch (const std::invalid_argument&) {
        return false;
    }
    if (!address) return false;
    auto* assembly = workspace_.open_assembly(address->owner_assembly_document_id);
    if (assembly == nullptr) return false;
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return false;
    const bool source_is_assembly =
        address->source_kind == zima::assembly::ComponentSourceKind::Assembly;
    try {
        if (workspace_.find(address->source_document_id) == nullptr) {
            if (occurrence->source_path.empty() ||
                !open_document_path(QString::fromStdString(
                    occurrence->source_path.string()))) {
                return false;
            }
        }
        const auto activated = workspace_.activate_occurrence(
            top_assembly_id, zima::assembly::InstancePath::decode(instance_path));
        if (!activated) return false;
        active_occurrence_path_ = instance_path;
        active_sketch_id_.clear();
        selected_sketch_id_.clear();
        active_application_ = source_is_assembly
            ? ApplicationMode::Assembly : ApplicationMode::Modeling;
        refresh_tabs();
        refresh_scene();
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

void AssemblyWorkspaceWindow::deactivate_active_occurrence_for_test() {
    const std::string displayed = workspace_.displayed_document_id();
    if (workspace_.open_assembly(displayed) == nullptr) return;
    workspace_.activate(displayed);
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    active_application_ = ApplicationMode::Assembly;
    refresh_tabs();
    refresh_scene();
}

bool AssemblyWorkspaceWindow::open_component_source(const std::string& instance_path) {
    if (properties_dialog_ || !active_sketch_id_.empty()) return false;
    const auto top = workspace_.displayed_document_id();bool reading=false;
    try {
        const auto opened=zima::workspace::open_component_source(workspace_,top,zima::assembly::InstancePath::decode(instance_path),
            [](auto task){run_background_task(std::move(task));},[this,&reading](const auto& path){
                reading=true;begin_status_operation(tr("Otevírám %1…").arg(QString::fromStdString(zima::document::path_to_utf8(path.filename()))));
            });
        workspace_.activate(opened.document_id);workspace_.display_top_level(opened.document_id);
        if(!opened.path.empty())working_directory_=opened.path.parent_path();
        viewer_->clear_selection();finish_document_switch(true);
        if(reading)finish_status_operation(tr("Otevřeno: %1").arg(QString::fromStdString(zima::document::path_to_utf8(opened.path.filename()))));
        return true;
    }catch(const std::exception& error) {
        const auto message=tr(error.what());if(reading)finish_status_operation(message,false);else state_->setText(message);
        return false;
    }
}

void AssemblyWorkspaceWindow::show_component_context_menu(
    const std::string& instance_path, const QPoint& global_position) {
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
    if(properties_dialog_!=nullptr)return;
    const auto selected_path=zima::assembly::InstancePath::decode(instance_path);
    const auto select_parent_occurrence = [this, selected_path] {
        auto parent = selected_path;
        if (parent.occurrence_ids.empty()) return;
        parent.occurrence_ids.pop_back();
        select_occurrence(parent.encoded());
        if (parent.occurrence_ids.empty()) viewer_->confirm_result_body();
        else viewer_->confirm_occurrence(parent.encoded());
    };
    const auto selected_parent=selected_path.parent();
    const bool copied_context=selected_parent&&workspace_.derived_source_path(workspace_.displayed_document_id(),*selected_parent)!=*selected_parent;
    if(assembly==nullptr||copied_context) {
        const auto path=selected_path;
        const auto source=workspace_.derived_source_path(workspace_.displayed_document_id(),path);
        // A persisted nested occurrence is still openable when its owning
        // subassembly has not been opened as an editing document.
        QMenu menu(this);
        auto* open=menu.addAction(resource_icon("open"),tr("Otevřít"));
        open->setObjectName("openComponentSourceAction");
        auto* properties=source!=path?menu.addAction(tr("Vlastnosti zdroje")):nullptr;
        auto* parent=menu.addAction(tr("Vybrat rodiče"));
        parent->setObjectName("selectParentOccurrenceAction");
        const auto* selected=menu.exec(global_position);
        if(selected==open)static_cast<void>(open_component_source(instance_path));
        else if(properties&&selected==properties)show_component_properties(source.encoded());
        else if(selected==parent)select_parent_occurrence();
        return;
    }
    const auto* occurrence = assembly->session.document().find_occurrence(
        address->occurrence_id);
    if (occurrence == nullptr) return;
    const bool is_active_occurrence =
        workspace_.active_document_id() == address->source_document_id &&
        active_occurrence_path_ == instance_path;
    const bool source_is_assembly =
        address->source_kind == zima::assembly::ComponentSourceKind::Assembly;
    QMenu menu(this);
    auto* open = menu.addAction(resource_icon("open"),tr("Otevřít"));
    open->setObjectName("openComponentSourceAction");
    auto* select_parent = menu.addAction(tr("Vybrat rodiče"));
    select_parent->setObjectName("selectParentOccurrenceAction");
    auto* activate_or_deactivate = is_active_occurrence
        ? menu.addAction(tr("Zpět do sestavy"))
        : menu.addAction(tr("Aktivní"));
    auto* create_body=is_active_occurrence && !source_is_assembly && !properties_dialog_
        ? menu.addAction(resource_icon("result-body"),tr("Vytvořit těleso")) : nullptr;
    auto* properties = menu.addAction(tr("Vlastnosti"));
    auto* mirror_properties=occurrence->derived_copy&&address->owner_assembly_document_id==workspace_.active_document_id()
        ? menu.addAction(occurrence->derived_copy->pattern?tr("Vlastnosti Pole"):tr("Vlastnosti Zrcadla")) : nullptr;
    auto* visibility = menu.addAction(
        occurrence->visible ? tr("Skrýt") : tr("Zobrazit"));
    auto* suppression = menu.addAction(
        occurrence->suppressed ? tr("Obnovit") : tr("Potlačit"));
    auto* grounding = menu.addAction(
        occurrence->grounded ? tr("Uvolnit") : tr("Uzemnit"));
    grounding->setEnabled(!occurrence->derived_copy);
    auto* remove = menu.addAction(tr("Odstranit"));
    const QAction* selected = menu.exec(global_position);
    if(selected==open) {
        if(!open_component_source(instance_path)) state_->setText(tr("Zdrojový dokument komponenty nelze otevřít."));
        return;
    }
    if (selected == select_parent) {
        select_parent_occurrence();
        return;
    }
    if (selected == activate_or_deactivate) {
        if (is_active_occurrence) {
            deactivate_active_occurrence_for_test();
        } else if (!activate_occurrence_for_test(instance_path)) {
            QMessageBox::critical(this, tr("Aktivace selhala"),
                tr("Zdrojový dokument komponenty se nepodařilo otevřít nebo aktivovat."));
        }
        return;
    }
    if(create_body && selected==create_body) { activate_body({});show_body_properties();return; }
    if(mirror_properties&&selected==mirror_properties){show_derived_copy_properties(address->occurrence_id);return;}
    if (selected == properties) {
        show_component_properties(instance_path);
        return;
    }
    if (selected != visibility && selected != suppression &&
        selected != grounding && selected != remove) return;
    if (selected == remove && QMessageBox::question(
            this, tr("Odstranit komponentu"),
            tr("Opravdu chcete komponentu ze sestavy odstranit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) return;
    auto next = assembly->session.document();
    auto found = std::find_if(next.components.begin(), next.components.end(),
        [&](const auto& item) { return item.occurrence_id == address->occurrence_id; });
    if (found == next.components.end()) return;
    if (selected == remove) {
        const auto uses_occurrence = [&](const auto& path) {
            return !path.occurrence_ids.empty() &&
                path.occurrence_ids.front() == address->occurrence_id;
        };
        const bool used_by_placement_reference = std::any_of(
            next.components.begin(), next.components.end(), [&](const auto& component) {
                return std::any_of(component.placement_references.begin(),
                    component.placement_references.end(), [&](const auto& row) {
                        return uses_occurrence(row.component_reference.instance_path) ||
                            uses_occurrence(row.target_reference.instance_path);
                    });
            });
        const bool used_by_dependency = std::any_of(
            next.dependencies.begin(), next.dependencies.end(), [&](const auto& edge) {
                return edge.prerequisite_occurrence_id == address->occurrence_id && edge.dependent_occurrence_id != address->occurrence_id;
            });
        const bool used_by_sketch = std::any_of(
            next.sketches.begin(), next.sketches.end(), [&](const auto& sketch) {
                return std::any_of(sketch.external_references.begin(),
                    sketch.external_references.end(), [&](const auto& reference) {
                        if (reference.context_assembly_document_id != next.document_id ||
                            reference.source_instance_path.empty()) return false;
                        try {
                            return uses_occurrence(zima::assembly::InstancePath::decode(
                                reference.source_instance_path));
                        } catch (const std::invalid_argument&) {
                            return false;
                        }
                    });
            });
        if (used_by_placement_reference || used_by_dependency || used_by_sketch) {
            QMessageBox::warning(this, tr("Komponentu nelze odstranit"),
                tr("Komponenta je použita vazbou, závislostí nebo "
                   "externí referencí skici."));
            return;
        }
        const auto assembly_id = next.document_id;
        const auto occurrence_id = address->occurrence_id;
        if(!found->derived_copy)workspace_.regenerate_assembly_from_open_dependencies(assembly_id);
        assembly = workspace_.open_assembly(assembly_id);
        if (assembly == nullptr) return;
        next = assembly->session.document();
        found = std::find_if(next.components.begin(), next.components.end(),
            [&](const auto& item) { return item.occurrence_id == occurrence_id; });
        if (found == next.components.end()) return;
        for (auto& cut : next.cuts) {
            std::erase(cut.target_occurrence_ids, occurrence_id);
            auto& extrusion = cut.definition.extrusion;
            bool lost_extent_target = false;
            if (!extrusion.target_face.instance_path.empty()) {
                try {
                    lost_extent_target = uses_occurrence(
                        zima::assembly::InstancePath::decode(
                            extrusion.target_face.instance_path));
                } catch (const std::invalid_argument&) {
                    lost_extent_target = true;
                }
            }
            if (lost_extent_target &&
                (extrusion.extent == zima::document::ExtrusionExtent::UpToPlane ||
                 extrusion.extent == zima::document::ExtrusionExtent::UpToSurface)) {
                extrusion.extent = zima::document::ExtrusionExtent::Blind;
                extrusion.target_face = {};
                extrusion.target_surface_triangles.clear();
            }
        }
        next.components.erase(found);
        std::erase_if(next.dependencies,[&](const auto& dependency){return dependency.dependent_occurrence_id==occurrence_id;});
        next.calculate_placement_references();
        calculate_assembly_cuts(next);
        assembly->session.commit(std::move(next));
        refresh_tabs();
        refresh_scene();
        return;
    }
    if (selected == visibility) found->visible = !found->visible;
    if (selected == suppression) found->suppressed = !found->suppressed;
    if (selected == grounding) found->grounded = !found->grounded;
    next.calculate_placement_references();
    assembly->session.commit(std::move(next));
    refresh_tabs();
    refresh_scene();
}

} // namespace zima::app
