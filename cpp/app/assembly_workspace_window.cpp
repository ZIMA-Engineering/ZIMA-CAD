#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include "workspace/workspace_internal.hpp"
#include "updateservice.h"
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <QCloseEvent>
#include <QScopedValueRollback>
#include <QStatusBar>

namespace zima::app {
using namespace workspace_detail;
AssemblyWorkspaceWindow::AssemblyWorkspaceWindow(const QString& working_directory, const QString& settings_directory) {
    setProperty("applicationInstance", instance_.number());
    const bool has_explicit_working_directory =
        !working_directory.trimmed().isEmpty();
    if (has_explicit_working_directory) {
        working_directory_ =
            std::filesystem::u8path(QFileInfo(working_directory).absoluteFilePath().toStdString());
    }
    application_settings_ = ApplicationSettings::load(
        !settings_directory.isEmpty() ? settings_directory :
            has_explicit_working_directory ? working_directory : QString{});
    if (!has_explicit_working_directory) {
        const QString configured =
            application_settings_.resolved_paths.value("WorkingDirectory");
        if (!configured.trimmed().isEmpty() && QFileInfo(configured).isDir()) {
            working_directory_ = std::filesystem::u8path(QFileInfo(configured).absoluteFilePath().toStdString());
        }
    }
    instance_.set_directory(QString::fromStdString(document::path_to_utf8(working_directory_)));
    setProperty("instanceWorkingDirectory",QString::fromStdString(document::path_to_utf8(working_directory_)));
    workspace_.file_reservation=[this](const auto& path) {
        instance_.reserve_file(QString::fromStdString(document::path_to_utf8(path)));
    };
    apply_application_translations(*qApp, application_settings_);
    apply_application_font(*qApp, application_settings_);
    // Apply synchronously before child widgets copy and customize this font;
    // QApplication's queued font-change event arrives after their creation.
    setFont(qApp->font());
    setStyleSheet(
        "QPushButton:hover:enabled,QToolButton:hover:enabled,QComboBox:hover:enabled { background:#4DD811; color:#102027; }"
        "QMenu { background:palette(window); color:palette(window-text); border:1px solid palette(mid); padding:3px; }"
        "QMenu::item { background:transparent; padding:5px 24px; }"
        "QMenu::item:selected:enabled { background:#4DD811; color:#102027; }"
        "QMenu::item:disabled { color:palette(mid); }"
        "QMenu::separator { height:1px; background:palette(mid); margin:3px 5px; }");
    setWindowTitle(tr("ZIMA-CAD"));
    install_dialog_button_icons();
    setWindowIcon(application_icon());
    resize(1200, 800);
    create_actions();
    create_layout();
    create_command_console();
    auto* updates = UpdateService::get();
    updates->configure(application_settings_.installation_root.isEmpty()
        ? application_settings_.base_config_path
        : application_settings_.installation_root + "/config/config.ini", [this] {
        if (properties_dialog_ || !active_sketch_id_.empty())
            return tr("Nejprve dokončete nebo zrušte otevřenou úpravu modelu.");
        for (auto* dialog : findChildren<QDialog*>())
            if (dialog != global_settings_dialog_ && dialog->isVisible())
                return tr("Nejprve dokončete nebo zrušte otevřenou úpravu modelu.");
        for (const auto& state : workspace_.documents()) {
            const auto id = std::visit([](const auto& value) {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, workspace::DrawingState>) return value.document().document_id;
                else return value.session.document().document_id;
            }, state);
            if (workspace::document_needs_save(workspace_, id))
                return tr("Před instalací uložte nebo zavřete všechny neuložené dokumenty.");
        }
        return QString{};
    });
    auto* update_notice = new QLabel(this);
    update_notice->setObjectName("updateAvailableNotice");
    update_notice->hide();
    statusBar()->addPermanentWidget(update_notice);
    connect(updates, &UpdateService::changed, this, [updates, update_notice] {
        const auto version = updates->offer()["availableVersion"].toString();
        update_notice->setText(QStringLiteral("<a href=\"updates\">%1</a>").arg(tr("Nová verze %1").arg(version).toHtmlEscaped()));
        update_notice->setVisible(!version.isEmpty());
    });
    connect(update_notice, &QLabel::linkActivated, this, [this] {
        show_global_settings();
        if (auto* settings = dynamic_cast<GlobalSettingsDialog*>(global_settings_dialog_)) settings->show_updates();
    });
    refresh_tabs();
    refresh_scene();
}

AssemblyWorkspaceWindow::~AssemblyWorkspaceWindow() {
    // Qt only destroys QObject children (including any open Properties
    // dialog) from inside the QWidget base-class destructor, which runs
    // after every data member of this derived class has already been
    // destroyed. Several dialogs connect to `destroyed` with a lambda that
    // reads or assigns members such as viewer_, tree_, or
    // construction_reference_geometry_ (see e.g.
    // show_construction_properties). Left to Qt's automatic child deletion,
    // that lambda would run against already-freed members and corrupt the
    // heap (observed as a double free on exit). Deleting the dialog here,
    // while the destructor body is still executing and every member is
    // still valid, guarantees its `destroyed` handler runs safely.
    const QPointer<QDialog> outer_dialog = tree_edit_dialog_;
    delete properties_dialog_;
    // A nested Point can restore its hidden parent during its destroyed
    // callback. Retire that outer transaction while members are still alive.
    if (outer_dialog) delete outer_dialog.data();
    delete orientation_dialog_;
    delete rename_document_dialog_;
    delete global_settings_dialog_;
}

bool AssemblyWorkspaceWindow::confirm_application_close() {
    if (properties_dialog_) {
        properties_dialog_->show();
        properties_dialog_->raise();
        properties_dialog_->activateWindow();
        return false;
    }
    for (auto* dialog : findChildren<QDialog*>()) {
        if (!dialog->isVisible()) continue;
        dialog->raise();
        dialog->activateWindow();
        return false;
    }
    if ((!active_sketch_id_.empty() && !template_sketch() && !symbol_document_sketch()) ||
        (inline_dimension_edit_ && inline_dimension_edit_->isVisible())) {
        QMessageBox::information(this, tr("Neuložené změny"),
            tr("Nejprve dokončete nebo zrušte otevřenou úpravu modelu."));
        return false;
    }
    std::vector<std::string> dirty;
    QStringList names;
    for (const auto& state : workspace_.documents()) {
        const auto id = std::visit([](const auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, workspace::DrawingState>)
                return value.document().document_id;
            else return value.session.document().document_id;
        }, state);
        if (workspace::family_owner(workspace_, id) != id ||
            !workspace::document_needs_save(workspace_, id)) continue;
        dirty.push_back(id);
        names.push_back(std::visit([](const auto& value) {
            if (!value.path.empty()) return QString::fromStdString(document::path_to_utf8(value.path.filename()));
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, workspace::DrawingState>)
                return QString::fromStdString(value.document().name);
            else return QString::fromStdString(value.session.document().name);
        }, state));
        if (names.back().isEmpty()) names.back() = QString::fromStdString(id);
    }
    if (dirty.empty()) return true;
    QMessageBox prompt(QMessageBox::Warning, tr("Neuložené změny"),
        tr("Před zavřením aplikace uložit změny v těchto dokumentech?") +
            QStringLiteral("\n\n") + names.join('\n'),
        QMessageBox::SaveAll | QMessageBox::Discard | QMessageBox::Cancel, this);
    prompt.setObjectName("applicationCloseConfirmation");
    prompt.setDefaultButton(QMessageBox::Cancel);
    const auto answer = prompt.exec();
    if (answer == QMessageBox::Discard) return true;
    if (answer != QMessageBox::SaveAll) return false;
    const auto active = workspace_.active_document_id();
    const auto displayed = workspace_.displayed_document_id();
    const auto occurrence = workspace_.active_occurrence_path();
    for (const auto& id : dirty) {
        workspace_.activate(id);
        workspace_.display_top_level(id);
        save_active_document();
        if (workspace::document_needs_save(workspace_, id)) {
            if (workspace_.find(displayed)) workspace_.display_top_level(displayed);
            if (!occurrence.empty()) static_cast<void>(workspace_.activate_occurrence(displayed, assembly::InstancePath::decode(occurrence)));
            else if (workspace_.find(active)) workspace_.activate(active);
            refresh_tabs();
            preserve_view_on_refresh_ = true;
            refresh_scene();
            return false;
        }
    }
    return true;
}

void AssemblyWorkspaceWindow::closeEvent(QCloseEvent* event) {
    event->ignore();
    if (closing_) return;
    QScopedValueRollback<bool> closing(closing_, true);
    if (!confirm_application_close()) return;
    if (restart_requested_) {
        RestartState next;
        next.working_directory = QString::fromStdString(document::path_to_utf8(working_directory_));
        next.settings_directory = QFileInfo(application_settings_.config_path).absolutePath();
        for (int index = 0; index < tabs_->count(); ++index) {
            const auto id = workspace::family_owner(workspace_, tabs_->tabData(index).toString().toStdString());
            const auto* state = workspace_.find(id);
            if (!state) continue;
            const auto path = std::visit([](const auto& value) { return value.path; }, *state);
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error)) continue;
            const auto file = QString::fromStdString(document::path_to_utf8(path));
            if (!next.documents.contains(file)) next.documents.push_back(file);
        }
        restart_state_ = std::move(next);
    }
    event->accept();
}

void AssemblyWorkspaceWindow::request_language_restart() {
    if (QMessageBox::question(this, tr("Změna jazyka"),
        tr("Pro sjednocení všech nabídek a panelů je nutné znovu otevřít aplikaci. Restartovat nyní?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return;
    QScopedValueRollback<bool> requested(restart_requested_, true);
    close();
}

} // namespace zima::app
