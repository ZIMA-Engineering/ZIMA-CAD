#include "workspace_internal.hpp"
#include <zima/workspace/import_operations.hpp>
#include <zima/workspace/assembly_import_operations.hpp>
#include <zima/workspace/export_operations.hpp>

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
            QMessageBox::warning(this,tr("Nastavení importu"),tr(error.what()));
        }
        return;
    }
    if (workspace_.open_part(workspace_.active_document_id())) {
        begin_status_operation(tr("Importuji %1…").arg(QFileInfo(path).fileName()));
        try {
            zima::workspace::PartImportOptions options;
            options.mesh_deflection = mesh_deflection;
            options.sketch_id = active_sketch_id_;
            const auto report = zima::workspace::import_part(workspace_, workspace_.active_document_id(),
                std::filesystem::u8path(path.toStdString()), options,
                [](auto task) { run_background_task(std::move(task)); });
            refresh_tabs(); refresh_scene();
            if (format == zima::interchange::Format::Dxf) {
                finish_status_operation(tr("DXF importováno: %1 entit").arg(report.dxf.imported_entities));
                if (!report.dxf.warnings.empty()) {
                    QStringList warnings;
                    for (const auto& warning : report.dxf.warnings)
                        if (!warnings.contains(QString::fromStdString(warning))) warnings << QString::fromStdString(warning);
                    QMessageBox::information(this, tr("Upozornění importu DXF"), warnings.join("\n"));
                }
            } else finish_status_operation(format == zima::interchange::Format::Step
                ? tr("STEP importován: %1 těles").arg(report.body_ids.size()) : tr("IGES importován"));
        } catch (const std::exception& error) {
            finish_status_operation(tr("Import selhal"), false);
            QMessageBox::warning(this, tr("Import selhal"), tr(error.what()));
        }
        return;
    }
    if (!workspace_.open_assembly(workspace_.active_document_id())) return;
    begin_status_operation(tr("Importuji %1…").arg(QFileInfo(path).fileName()));
    try {
        zima::workspace::AssemblyImportOptions options;
        options.geometry.mesh_deflection=mesh_deflection;options.geometry.sketch_id=active_sketch_id_;
        options.working_directory=working_directory_;options.templates=native_template_settings(application_settings_);
        const auto report=zima::workspace::import_assembly(workspace_,workspace_.active_document_id(),
            std::filesystem::u8path(path.toStdString()),options,[](auto task){run_background_task(std::move(task));});
        refresh_tabs();refresh_scene();
        finish_status_operation(tr("Import dokončen: %1 dílů, %2 podsestav").arg(report.part_ids.size()).arg(report.assembly_ids.size()));
        if(!report.dxf.warnings.empty()) {
            QStringList warnings;for(const auto& warning:report.dxf.warnings)
                if(!warnings.contains(QString::fromStdString(warning)))warnings << QString::fromStdString(warning);
            QMessageBox::information(this,tr("Upozornění importu DXF"),warnings.join("\n"));
        }
    } catch(const std::exception& error) {
        finish_status_operation(tr("Import selhal"),false);
        QMessageBox::warning(this,tr("Import selhal"),tr(error.what()));
    }
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
    begin_status_operation(tr("Exportuji %1…").arg(QFileInfo(path).fileName()));
    try {
        const zima::workspace::ExportOptions options{active_sketch_id_,true};
        // QFileDialog has already obtained confirmation for an existing file.
        static_cast<void>(zima::workspace::export_file(workspace_,workspace_.active_document_id(),
            std::filesystem::u8path(path.toStdString()),options,
            [](auto task){run_background_task(std::move(task));}));
        finish_status_operation(tr("Model exportován: %1").arg(path));
    } catch(const std::exception& error) {
        finish_status_operation(tr("Export modelu selhal"),false);
        QMessageBox::warning(this,tr("Export modelu selhal"),tr(error.what()));
    }
}

} // namespace zima::app
