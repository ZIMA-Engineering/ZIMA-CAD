#include <zima/command_host/host.hpp>
#include "workspace/workspace_internal.hpp"
#include "updateservice.h"
#include <zima/workspace/document_operations.hpp>
#include <QStatusBar>

namespace zima::app {
using namespace workspace_detail;
AssemblyWorkspaceWindow::AssemblyWorkspaceWindow(const QString& working_directory) {
    setProperty("applicationInstance", instance_.number());
    const bool has_explicit_working_directory =
        !working_directory.trimmed().isEmpty();
    if (has_explicit_working_directory) {
        working_directory_ =
            std::filesystem::u8path(QFileInfo(working_directory).absoluteFilePath().toStdString());
    }
    application_settings_ = ApplicationSettings::load(
        has_explicit_working_directory ? working_directory : QString{});
    if (!has_explicit_working_directory) {
        const QString configured =
            application_settings_.resolved_paths.value("WorkingDirectory");
        if (!configured.trimmed().isEmpty() && QFileInfo(configured).isDir()) {
            working_directory_ = std::filesystem::u8path(QFileInfo(configured).absoluteFilePath().toStdString());
        }
    }
    apply_application_translations(*qApp, application_settings_);
    setWindowTitle(tr("ZIMA-CAD"));
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

} // namespace zima::app
