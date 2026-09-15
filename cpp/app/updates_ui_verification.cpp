#include "updates_ui_verification.hpp"
#include "assembly_workspace_window.hpp"
#include "global_settings_dialog.hpp"
#include "updateservice.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <stdexcept>

namespace zima::app {
int verify_updates_ui(QApplication& app, AssemblyWorkspaceWindow& window, const std::filesystem::path& directory) {
    try {
        const auto require = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
        window.showMaximized(); app.processEvents();
        auto* service = UpdateService::get();
        auto* notice = window.findChild<QLabel*>("updateAvailableNotice");
        require(notice && notice->isHidden() && !service->busy(), "Startup must not open an update popup or run during verification");
        require(service->restartBlocker().isEmpty(), "Empty workspace should permit an explicit restart");
        require(window.execute_console_command("new part update-restart-guard").ok, "Cannot prepare unsaved Part");
        require(!service->restartBlocker().isEmpty(), "Unsaved Part must prevent update restart");
        service->rollback();
        require(!service->busy() && !service->error().isEmpty() && window.isVisible(), "Blocked restart must keep the application open");
        const bool automatic = service->automatic();
        auto* action = window.findChild<QAction*>("globalSettingsAction");
        require(action != nullptr, "Missing Settings action"); action->trigger(); app.processEvents();
        auto* dialog = dynamic_cast<GlobalSettingsDialog*>(window.findChild<QDialog*>("globalSettingsDialog"));
        require(dialog && dialog->windowFlags().testFlag(Qt::SubWindow), "Updates must use internal Settings");
        dialog->show_updates(); app.processEvents();
        auto* tabs = dialog->findChild<QTabWidget*>("globalSettingsSections");
        require(tabs && tabs->currentWidget()->objectName() == "updatesPage", "Missing Updates section");
        auto* checkbox = dialog->findChild<QCheckBox*>("updatesAutomatic");
        auto* install = dialog->findChild<QPushButton*>("installUpdate");
        require(checkbox && install && !install->isEnabled(), "Unverified update must not be installable");
        checkbox->setChecked(!automatic);
        const auto image = QString::fromStdU16String((directory / "updates-settings.png").u16string());
        require(window.grab().save(image), "Cannot capture Updates UI");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click(); app.processEvents();
        require(service->automatic() == automatic, "Cancel changed startup preference");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QTemporaryDir temp;
        require(temp.isValid(), "Cannot create isolated preference test");
        const auto config = temp.filePath("config.ini");
        service->configure(config, [] { return QString("Test restart is blocked"); });
        auto settings = ApplicationSettings::load();
        settings.config_path = config; settings.base_config_path = config;
        settings.platform_config_path.clear(); settings.installation_root.clear();
        GlobalSettingsDialog isolated(settings, &window);
        isolated.show_updates(); isolated.show(); app.processEvents();
        isolated.findChild<QCheckBox*>("updatesAutomatic")->setChecked(false);
        isolated.buttons()->button(QDialogButtonBox::Ok)->click(); app.processEvents();
        require(isolated.result() == QDialog::Accepted && !service->automatic(), "OK must persist startup check preference");
        require(!QSettings(config, QSettings::IniFormat).value("Updates/CheckAtStartup", true).toBool(), "Preference is missing from config");
        std::cout << "Updates UI: internal Settings, silent initial state, Cancel/OK and unsaved-document restart guard passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Updates UI: " << error.what() << '\n'; return 1;
    }
}
}
