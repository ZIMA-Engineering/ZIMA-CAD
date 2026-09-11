#include "workspace/workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;
AssemblyWorkspaceWindow::AssemblyWorkspaceWindow(const QString& working_directory) {
    setProperty("applicationInstance", instance_.number());
    const bool has_explicit_working_directory =
        !working_directory.trimmed().isEmpty();
    if (has_explicit_working_directory) {
        working_directory_ =
            QFileInfo(working_directory).absoluteFilePath().toStdString();
    }
    application_settings_ = ApplicationSettings::load(
        has_explicit_working_directory ? working_directory : QString{});
    if (!has_explicit_working_directory) {
        const QString configured =
            application_settings_.resolved_paths.value("WorkingDirectory");
        if (!configured.trimmed().isEmpty() && QFileInfo(configured).isDir()) {
            working_directory_ = QFileInfo(configured).absoluteFilePath().toStdString();
        }
    }
    apply_application_translations(*qApp, application_settings_);
    setWindowTitle(tr("ZIMA-CAD"));
    setWindowIcon(application_icon());
    resize(1200, 800);
    create_actions();
    create_layout();
    create_command_console();
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
    delete rename_document_dialog_;
}

} // namespace zima::app
