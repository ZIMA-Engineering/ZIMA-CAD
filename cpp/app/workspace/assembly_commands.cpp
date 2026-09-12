#include "workspace_internal.hpp"
#include <zima/workspace/component_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


bool AssemblyWorkspaceWindow::has_insertable_component() const {
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return false;
    for (const auto& state : workspace_.documents()) {
        const bool available = std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                return item.session.document().document_id != owner_id &&
                    !item.session.document().history.empty() &&
                    !item.session.calculated_boundaries().empty();
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                return item.session.document().document_id != owner_id;
            }
            return false;
        }, state);
        if (available) return true;
    }
    return false;
}

void AssemblyWorkspaceWindow::rebuild_insert_menu() {
    insert_menu_->clear();
    const std::string owner_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return;
    auto* choose_file = insert_menu_->addAction(tr("Vybrat soubor…"));
    choose_file->setObjectName("insertComponentFromFileAction");
    connect(choose_file, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    insert_menu_->addSeparator();
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                const bool calculated = !document.history.empty() &&
                    !item.session.calculated_boundaries().empty();
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) +
                    (calculated ? tr(" — Part") : tr(" — Part (není vypočtený)")));
                action->setObjectName("insertSourceAction");
                action->setEnabled(calculated);
                if (!calculated) {
                    action->setToolTip(tr(
                        "Part musí obsahovat alespoň jeden potvrzený a vypočtený prvek."));
                }
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) + tr(" — sestava"));
                action->setObjectName("insertSourceAction");
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { insert_component(id); });
            }
        }, state);
    }
}

void AssemblyWorkspaceWindow::insert_component_from_file() {
    const QString path = open_file(this, tr("Vložit komponentu"),
        QString::fromStdString(working_directory_.string()),
        tr("Komponenty ZIMA-CAD (*.prtz *.asmz)"),
        application_settings_.translations);
    if (path.isEmpty()) return;
    const std::filesystem::path source_path = path.toStdString();
    try {
        std::string source_id;
        if (const auto open_id = workspace_.document_id_for_path(source_path)) {
            source_id = *open_id;
        } else if (path.endsWith(".prtz", Qt::CaseInsensitive)) {
            std::vector<zima::kernel::BodyResult> calculated;
            auto source = zima::document::PartDocument::load(
                source_path, &calculated);
            source_id = source.document_id;
            workspace_.add_part(std::move(source), std::move(calculated), source_path);
        } else if (path.endsWith(".asmz", Qt::CaseInsensitive)) {
            auto source = zima::assembly::AssemblyDocument::load(source_path);
            source_id = source.document_id;
            workspace_.add_assembly(std::move(source), source_path);
        } else {
            throw std::runtime_error("Komponenta musí být Part nebo sestava.");
        }
        if (!source_path.parent_path().empty()) {
            working_directory_ = source_path.parent_path();
        }
        insert_component(source_id);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::insert_component(
    const std::string& source_document_id) {
    const std::string assembly_id = workspace_.displayed_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    try {
        const auto occurrence_id=zima::workspace::insert_component(workspace_,assembly_id,source_document_id);
        workspace_.activate(assembly_id);
        refresh_tabs();
        refresh_scene();
        viewer_->confirm_occurrence(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded());
        show_component_properties(
            zima::assembly::InstancePath{}.child(occurrence_id).encoded(), true);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::regenerate_assembly() {
    const std::string id = workspace_.displayed_document_id();
    try {
        workspace::regenerate_assembly(workspace_, kernel_, id, part_calculation_policy());
        refresh_tabs();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    } catch (const std::exception& error) {
        report_operation_error(tr("Regenerace selhala"), error.what());
    }
}

} // namespace zima::app
