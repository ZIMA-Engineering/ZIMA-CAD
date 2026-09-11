#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::import_file() {
    if (properties_dialog_) { properties_dialog_->raise(); return; }
    const QString path = open_file(this,
        application_settings_.text("menu.file.import", tr("Importovat")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.text("file.filter.import",
            tr("Podporované importní formáty (*.step *.stp *.igs *.iges *.dxf);;"
               "STEP (*.step *.stp);;IGES (*.igs *.iges);;DXF (*.dxf)")),
        application_settings_.translations);
    if (path.isEmpty()) return;
    import_selected_file(path);
}

void AssemblyWorkspaceWindow::import_selected_file(const QString& path, std::optional<double> mesh_deflection) {
    const auto context = !active_sketch_id_.empty()
        ? zima::interchange::Context::Sketch
        : workspace_.open_part(workspace_.active_document_id()) != nullptr
            ? zima::interchange::Context::Part : zima::interchange::Context::Assembly;
    const auto format = zima::interchange::format_from_path(path.toStdString());
    if (!zima::interchange::supports(
            format, zima::interchange::Direction::Import, context)) {
        QMessageBox::warning(this, tr("Import nelze provést"), QString::fromStdString(
            zima::interchange::unsupported_reason(
                format, zima::interchange::Direction::Import, context)));
        return;
    }
    if (!mesh_deflection && (format == zima::interchange::Format::Step || format == zima::interchange::Format::Iges)) {
        try {
            const auto precision=context==zima::interchange::Context::Part
                ? new_part_from_template(application_settings_).document_precision
                : new_assembly_from_template(application_settings_).document_precision;
            auto* dialog=new ImportOptionsDialog(path,
                zima::document::precision_value(precision,"mesh_deflection",0.1),this);
            properties_dialog_=dialog;
            const auto target_id=workspace_.active_document_id();
            connect(dialog,&QDialog::accepted,this,[this,dialog,path,target_id] {
                const double selected=dialog->mesh_deflection();
                QTimer::singleShot(0,this,[this,path,target_id,selected] {
                    if(workspace_.active_document_id()!=target_id || !active_sketch_id_.empty()) {
                        QMessageBox::warning(this,tr("Import nelze provést"),tr("Cílový dokument se během nastavení importu změnil."));
                        return;
                    }
                    import_selected_file(path,selected);
                });
            });
            connect(dialog,&QObject::destroyed,this,[this,dialog] {
                if(properties_dialog_==dialog)properties_dialog_=nullptr;
            });
            dialog->show();
        } catch(const std::exception& error) {
            QMessageBox::warning(this,tr("Nastavení importu"),QString::fromUtf8(error.what()));
        }
        return;
    }
    if (format == zima::interchange::Format::Dxf || format == zima::interchange::Format::Iges) {
        const auto target_id = workspace_.active_document_id();
        const auto displayed_id = workspace_.displayed_document_id();
        auto* part = workspace_.open_part(target_id);
        const auto* assembly = workspace_.open_assembly(target_id);
        if (!part && !assembly) return;
        const bool dxf = format == zima::interchange::Format::Dxf;
        begin_status_operation(tr("Importuji %1…").arg(QFileInfo(path).fileName()));
        try {
            auto document = part ? part->session.document() : new_part_from_template(application_settings_);
            if (!part) {
                document.name = QFileInfo(path).completeBaseName().toStdString();
                document.document_precision = assembly->session.document().document_precision;
                document.document_units = assembly->session.document().document_units;
            }
            auto previous = part ? part->session.calculated_boundaries() : std::vector<zima::kernel::BodyResult>{};
            zima::interchange::DxfImportResult report;
            auto imported = run_background_task([document=std::move(document),previous=std::move(previous),
                    source=std::filesystem::path(path.toStdString()),sketch_id=active_sketch_id_,dxf,mesh_deflection,&report]() mutable {
                if (!dxf) return zima::interchange::import_iges_part(std::move(document),previous,source,mesh_deflection);
                auto result = zima::interchange::import_dxf_part(std::move(document),previous,source,sketch_id);
                report = std::move(result.report); return std::move(result.part);
            });
            if (part) part->session.commit(std::move(imported.document),std::move(imported.calculated));
            else {
                const auto base = assembly->path.empty() ? working_directory_ : assembly->path.parent_path();
                const auto name = imported.document.name;
                const auto id = imported.document.document_id;
                std::filesystem::path destination;
                for (std::size_t suffix=0;;++suffix) {
                    destination=std::filesystem::absolute(base/(name+(suffix?"_"+std::to_string(suffix):"")+".prtz"));
                    if (!std::filesystem::exists(destination) && !workspace_.document_id_for_path(destination)) break;
                }
                imported.document.save(destination,imported.calculated);
                workspace_.add_part(std::move(imported.document),std::move(imported.calculated),destination);
                static_cast<void>(workspace_.insert_open_part(target_id,id,name));
                workspace_.activate(target_id); workspace_.display_top_level(displayed_id);
            }
            refresh_tabs(); refresh_scene();
            finish_status_operation(dxf ? tr("DXF importováno: %1 entit").arg(report.imported_entities)
                : tr("IGES importován"));
            if (!report.warnings.empty()) {
                QStringList warnings;
                for (const auto& warning : report.warnings)
                    if (!warnings.contains(QString::fromStdString(warning))) warnings << QString::fromStdString(warning);
                QMessageBox::information(this,tr("Upozornění importu DXF"),warnings.join("\n"));
            }
        } catch (const std::exception& error) {
            finish_status_operation(tr("Import selhal"),false);
            QMessageBox::warning(this,tr("Import selhal"),QString::fromUtf8(error.what()));
        }
        return;
    }
    if (format == zima::interchange::Format::Step) {
        auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr) {
            if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
                begin_status_operation(tr("Importuji STEP sestavu %1…").arg(
                    QFileInfo(path).fileName()));
                try { import_step_into_assembly(path.toStdString(),mesh_deflection); }
                catch (const std::exception& error) {
                    finish_status_operation(tr("Import STEP sestavy selhal"), false);
                    QMessageBox::warning(this, tr("Import STEP selhal"), error.what());
                }
            }
            return;
        }
        begin_status_operation(tr("Importuji STEP %1…").arg(
            QFileInfo(path).fileName()));
        try {
            const auto source=std::filesystem::absolute(path.toStdString());
            update_status_operation(tr("OCCT čte STEP, převádí topologii a vytváří síť…"),-1,0);
            auto imported=run_background_task([document=part->session.document(),
                    previous=part->session.calculated_boundaries(),source,mesh_deflection] {
                return zima::interchange::import_step_part(document,previous,source,mesh_deflection);
            });
            const auto count=imported.document.body_history.bodies().size()-part->session.document().body_history.bodies().size();
            part->session.commit(std::move(imported.document),std::move(imported.calculated));
            refresh_tabs();refresh_scene();
            finish_status_operation(tr("STEP importován: %1 těles").arg(count));
        } catch (const std::exception& error) {
            finish_status_operation(tr("Import STEP selhal"), false);
            QMessageBox::warning(this, tr("Import STEP selhal"), error.what());
        }
        return;
    }
    state_->setText(tr("Importní soubor připraven: %1").arg(path));
}

void AssemblyWorkspaceWindow::import_step_into_assembly(const std::filesystem::path& source, std::optional<double> mesh_deflection) {
    const auto target_id=workspace_.active_document_id();
    const auto displayed_id=workspace_.displayed_document_id();
    const auto* target=workspace_.open_assembly(target_id);
    if(!target)throw std::runtime_error("Aktivní dokument není sestava");
    const auto base=target->path.empty()?working_directory_:target->path.parent_path();
    const auto stem=source.stem().string()+"_zima";
    std::filesystem::path directory;
    for(std::size_t suffix=0;;++suffix) {
        directory=std::filesystem::absolute(base/(stem+(suffix?"_"+std::to_string(suffix):"")));
        if(std::filesystem::create_directory(directory))break;
    }
    update_status_operation(tr("Čtu produktovou strukturu STEP sestavy…"),-1,0);
    auto imported=run_background_task([source,directory,precision=target->session.document().document_precision,mesh_deflection] {
        return zima::interchange::import_step_assembly(source,directory,precision,mesh_deflection);
    });
    update_status_operation(tr("Ukládám STEP díly a podsestavy…"),-1,0);
    run_background_task([&imported] {
        for(const auto& part:imported.parts)part.document.save(part.path,part.calculated);
        for(const auto& assembly:imported.assemblies)assembly.document.save(assembly.path);
    });
    auto next=workspace_.open_assembly(target_id)->session.document();
    next.components.push_back(std::move(imported.root_occurrence));
    for(auto& part:imported.parts)workspace_.add_part(std::move(part.document),std::move(part.calculated),part.path);
    for(auto& assembly:imported.assemblies)workspace_.add_assembly(std::move(assembly.document),assembly.path);
    workspace_.open_assembly(target_id)->session.commit(std::move(next));
    workspace_.activate(target_id);workspace_.display_top_level(displayed_id);
    refresh_tabs();refresh_scene();
    finish_status_operation(tr("STEP sestava importována: %1 unikátních Partů, %2 podsestav")
        .arg(imported.parts.size()).arg(imported.assemblies.size()));
}

void AssemblyWorkspaceWindow::export_file() {
    if(workspace_.open_drawing(workspace_.displayed_document_id())){drawing_workspace_->save_pdf();return;}
    const QString path = save_file(this, tr("Exportovat"),
        QString::fromStdString(working_directory_.string()),
        tr("DXF (*.dxf);;STEP (*.step);;STL (*.stl);;PNG (*.png);;JPEG (*.jpg *.jpeg)"));
    if (path.isEmpty()) return;
    const auto context = !active_sketch_id_.empty()
        ? zima::interchange::Context::Sketch
        : workspace_.open_part(workspace_.active_document_id()) != nullptr
            ? zima::interchange::Context::Part : zima::interchange::Context::Assembly;
    const auto format = zima::interchange::format_from_path(path.toStdString());
    if (!zima::interchange::supports(
            format, zima::interchange::Direction::Export, context)) {
        QMessageBox::warning(this, tr("Export nelze provést"), QString::fromStdString(
            zima::interchange::unsupported_reason(
                format, zima::interchange::Direction::Export, context)));
        return;
    }
    if (format == zima::interchange::Format::Dxf) {
        const auto* part = workspace_.open_part(workspace_.active_document_id());
        if (part == nullptr) return;
        const auto sketch = std::find_if(
            part->session.document().sketches.begin(),
            part->session.document().sketches.end(),
            [&](const auto& value) { return value.id == active_sketch_id_; });
        if (sketch == part->session.document().sketches.end()) return;
        begin_status_operation(tr("Exportuji aktivní skicu do DXF…"));
        try {
            update_status_operation(
                tr("Převádím entity skici a zapisuji DXF…"), -1, 0);
            auto sketch_snapshot = *sketch;
            run_background_task([
                    sketch = std::move(sketch_snapshot),
                    target = path.toStdString()] {
                zima::interchange::export_dxf(target, sketch);
            });
            finish_status_operation(
                tr("Aktivní skica exportována do DXF: %1").arg(path));
        } catch (const std::exception& error) {
            finish_status_operation(tr("Export DXF selhal"), false);
            QMessageBox::warning(this, tr("Export DXF selhal"), error.what());
        }
        return;
    }
    if (format == zima::interchange::Format::Png ||
        format == zima::interchange::Format::Jpeg) {
        begin_status_operation(tr("Exportuji aktuální 3D pohled…"));
        update_status_operation(tr("Snímám framebuffer View…"));
        const auto image = viewer_->grabFramebuffer();
        update_status_operation(tr("Zapisuji obrazový soubor…"), -1, 0);
        const bool saved = !image.isNull() && run_background_task(
            [image, target = path] { return image.save(target); });
        if (!saved) {
            finish_status_operation(tr("Export obrázku selhal"), false);
            QMessageBox::warning(this, tr("Export obrázku selhal"),
                tr("Aktuální 3D pohled se nepodařilo uložit."));
            return;
        }
        finish_status_operation(
            tr("Aktuální 3D pohled exportován: %1").arg(path));
        return;
    }
    if (format == zima::interchange::Format::Step) {
        begin_status_operation(tr("Připravuji model pro export…"));
        try {
            zima::kernel::StepProduct product;
            const auto id=workspace_.active_document_id();
            if(const auto* part=workspace_.open_part(id))
                product=zima::interchange::step_product(part->session.document(),part->session.calculated_boundaries());
            else if(const auto* assembly=workspace_.open_assembly(id))
                product=zima::interchange::step_product(assembly->session.document());
            else throw std::runtime_error("STEP export vyžaduje Part nebo sestavu");
            update_status_operation(tr("OCCT převádí a zapisuje STEP…"),-1,0);
            run_background_task([product=std::move(product),target=path.toStdString()] {
                zima::kernel::OcctKernel kernel;kernel.export_step(product,target);
            });
            finish_status_operation(tr("Model exportován: %1").arg(path));
        } catch(const std::exception& error) {
            finish_status_operation(tr("Export modelu selhal"),false);
            QMessageBox::warning(this,tr("Export modelu selhal"),error.what());
        }
        return;
    }
    if (format == zima::interchange::Format::Stl) {
        begin_status_operation(tr("Připravuji model pro export…"));
        try {
            std::vector<zima::kernel::PlacedBody> bodies;
            if (const auto* part = workspace_.open_part(
                    workspace_.active_document_id())) {
                if (part->session.calculated_boundaries().empty()) {
                    throw std::runtime_error(
                        "Part nemá explicitně vypočtené těleso");
                }
                bodies.push_back({part->session.calculated_boundaries().back(), {}, {}});
            } else if (const auto* assembly = workspace_.open_assembly(
                           workspace_.active_document_id())) {
                const auto suppressed =
                    assembly->session.document().effectively_suppressed_occurrences();
                for (const auto& component : assembly->session.document().components) {
                    if (!component.visible || suppressed.contains(component.occurrence_id)) {
                        continue;
                    }
                    if (component.source_kind !=
                            zima::assembly::ComponentSourceKind::Part) {
                        throw std::runtime_error(
                            "STEP/STL export vnořené podsestavy zatím vyžaduje její "
                            "rozbalení na Part výskyty");
                    }
                    bodies.push_back({component.calculated_source,
                        {component.placement.x, component.placement.y,
                         component.placement.z},
                        {component.placement.rotation_x,
                         component.placement.rotation_y,
                         component.placement.rotation_z}});
                }
            }
            update_status_operation(tr("OCCT vytváří síť a zapisuje STL…"),-1,0);
            run_background_task([
                    bodies = std::move(bodies), target = path.toStdString(),
                    format] {
                zima::kernel::OcctKernel worker_kernel;
                worker_kernel.export_stl(bodies, target);
            });
            finish_status_operation(tr("Model exportován: %1").arg(path));
        } catch (const std::exception& error) {
            finish_status_operation(tr("Export modelu selhal"), false);
            QMessageBox::warning(this, tr("Export modelu selhal"), error.what());
        }
        return;
    }
}

} // namespace zima::app
