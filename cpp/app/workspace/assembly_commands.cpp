#include "workspace_internal.hpp"
#include <zima/workspace/component_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>

namespace zima::app {
using namespace workspace_detail;


bool AssemblyWorkspaceWindow::has_insertable_component() const {
    const std::string owner_id = workspace_.active_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return false;
    for (const auto& state : workspace_.documents()) {
        const bool available = std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                return item.session.document().document_id != owner_id &&
                    (zima::assembly::is_skeleton_file(item.path) || (!item.session.document().history.empty() &&
                    !item.session.calculated_boundaries().empty()));
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
    const std::string owner_id = workspace_.active_document_id();
    if (workspace_.open_assembly(owner_id) == nullptr) return;
    auto* choose_file = insert_menu_->addAction(tr("Vybrat soubor…"));
    choose_file->setObjectName("insertComponentFromFileAction");
    connect(choose_file, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    auto* skeleton = insert_menu_->addAction(resource_icon("skeleton"), tr("Vložit Skeleton…"));
    skeleton->setObjectName("insertSkeletonAction");
    skeleton->setEnabled(std::ranges::none_of(workspace_.open_assembly(owner_id)->session.document().components,
        [](const auto& value) { return zima::assembly::is_skeleton(value); }));
    connect(skeleton, &QAction::triggered, this, [this] { insert_component_from_file(true); });
    insert_menu_->addSeparator();
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& item) {
            using State = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<State, zima::workspace::PartState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                const bool calculated = zima::assembly::is_skeleton_file(item.path) || (!document.history.empty() &&
                    !item.session.calculated_boundaries().empty());
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
                    [this, id = document.document_id] { choose_component_variant(id); });
            } else if constexpr (
                std::is_same_v<State, zima::workspace::AssemblyState>) {
                const auto& document = item.session.document();
                if (document.document_id == owner_id) return;
                auto* action = insert_menu_->addAction(
                    QString::fromStdString(document.name) + tr(" — sestava"));
                action->setObjectName("insertSourceAction");
                connect(action, &QAction::triggered, this,
                    [this, id = document.document_id] { choose_component_variant(id); });
            }
        }, state);
    }
}

void AssemblyWorkspaceWindow::insert_component_from_file(bool skeleton_only) {
    const QString path = open_file(this, tr("Vložit komponentu"),
        QString::fromStdString(working_directory_.string()),
        skeleton_only ? tr("Skeleton (*_skeleton.prtz)") : tr("Komponenty ZIMA-CAD (*.prtz *.asmz)"),
        application_settings_.translations);
    if (path.isEmpty()) return;
    const auto source_path = std::filesystem::u8path(path.toStdString());
    if (skeleton_only && !zima::assembly::is_skeleton_file(source_path)) {
        QMessageBox::warning(this, tr("Skeleton"), tr("Název souboru musí končit na _skeleton.prtz."));
        return;
    }
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
            change_working_directory(source_path.parent_path());
        }
        choose_component_variant(source_id);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::choose_component_variant(const std::string& requested,const std::string&) {
    if(properties_dialog_){properties_dialog_->raise();return;}
    insert_component(requested);
}

void AssemblyWorkspaceWindow::insert_component(
    const std::string& source_document_id) {
    const std::string assembly_id = workspace_.active_document_id();
    if (workspace_.open_assembly(assembly_id) == nullptr) return;
    const auto original_session = workspace_.open_assembly(assembly_id)->session;
    try {
        const auto occurrence_id=zima::workspace::insert_component(workspace_,assembly_id,source_document_id);
        workspace_.activate(assembly_id);
        refresh_tabs();
        refresh_scene();
        const auto path=(workspace_.active_occurrence_path().empty()?zima::assembly::InstancePath{}:
            zima::assembly::InstancePath::decode(workspace_.active_occurrence_path())).child(occurrence_id).encoded();
        viewer_->confirm_occurrence(path);
        show_component_properties(path, true);
        if (auto* dialog = component_placement_dialog_) {
            connect(dialog, &QDialog::finished, this, [this, assembly_id, original_session](int result) {
                auto* state = workspace_.open_assembly(assembly_id);
                if (!state) return;
                auto final_document = state->session.document();
                state->session = original_session;
                if (result == QDialog::Accepted) state->session.commit(std::move(final_document));
                preserve_view_on_refresh_ = true;
                refresh_tabs(); refresh_scene();
            });
        } else {
            workspace_.open_assembly(assembly_id)->session = original_session;
            refresh_scene();
        }
    } catch (const std::exception& error) {
        workspace_.open_assembly(assembly_id)->session = original_session;
        QMessageBox::critical(this, tr("Vložení selhalo"), error.what());
    }
}

void AssemblyWorkspaceWindow::regenerate_assembly() {
    const std::string id = workspace_.active_document_id();
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
