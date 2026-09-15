#include "updates_ui_verification.hpp"
#include "assembly_workspace_window.hpp"
#include "global_settings_dialog.hpp"
#include "aiprovider.h"
#include "aipreferences.h"
#include <QLineEdit>
#include <QComboBox>
#include <QToolButton>
#include "updateservice.h"
#include "updatespage.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSettings>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace zima::app {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

struct UpdateInteraction {
    UpdatesPage::State state;
    QString blocker;
    int downloads = 0, cancellations = 0, restarts = 0, rollbacks = 0;
    std::unique_ptr<UpdatesPage> page;
    explicit UpdateInteraction(QWidget* parent) {
        state.installed = true;
        state.previous = "2026091501";
        state.offer = {{"availableVersion", "2026091504"}, {"installable", true},
            {"notes", "Update workflow verification\nPreserve projects and configuration."},
            {"manifest", QJsonObject{{"archive", QJsonObject{{"size", 67108864}}}}}};
        page = std::make_unique<UpdatesPage>([this](bool rollback) {
            if (rollback) ++rollbacks; else ++restarts;
        }, UpdatesPage::Backend{
            [this] { return state; },
            [this] { state.prepared.clear(); state.offer = {}; state.phase = "current"; },
            [this] { ++downloads; state.busy = true; state.error.clear(); state.prepared.clear(); state.phase = "downloading"; },
            [this] { ++cancellations; state.busy = false; },
            [](bool, QString*) { return true; }, [this] { return blocker; }
        }, parent);
    }
    QPushButton* button(const char* name) { return page->findChild<QPushButton*>(name); }
    void finish(const QString& version = "2026091504") {
        state.busy = false; state.phase = "prepared"; state.prepared = version; page->refresh();
    }
};

void verify_install_interactions(QApplication& app, QWidget& parent) {
    {
        UpdateInteraction test(&parent);
        app.processEvents();
        require(test.downloads == 0 && test.restarts == 0, "An available update must not authorize installation");
        require(test.page->findChild<QPlainTextEdit*>()->toPlainText() == test.state.offer["notes"].toString(), "Release notes are missing");
        test.button("installUpdate")->click();
        require(test.downloads == 1 && test.restarts == 0 && !test.button("installUpdate")->isEnabled(), "One click must start preparation without premature restart");
        test.state.received = 32; test.state.total = 64; test.page->refresh();
        require(test.page->findChild<QProgressBar*>("updateProgress")->value() == 50, "Download progress is incorrect");
        test.finish(); test.page->refresh(); test.button("installUpdate")->click();
        app.processEvents(); app.processEvents();
        require(test.downloads == 1 && test.restarts == 1, "Prepared update must restart exactly once without a second click");
        require(test.button("installUpdate")->isEnabled(), "Rejected Settings validation must leave an explicit restart retry available");
    }
    {
        UpdateInteraction test(&parent);
        test.blocker = "Save the modified document first.";
        test.button("installUpdate")->click();
        require(test.downloads == 0, "Unsaved documents must block the initial install action");
        test.blocker.clear(); test.button("installUpdate")->click();
        test.blocker = "Document changed during download."; test.finish(); app.processEvents();
        require(test.restarts == 0 && test.button("installUpdate")->isEnabled(), "Newly modified documents must block automatic restart and allow explicit retry");
        bool explained = false;
        for (const auto* label : test.page->findChildren<QLabel*>()) explained |= label->text().contains(test.blocker);
        require(explained, "Restart blocker must be explained inside Updates");
        test.blocker.clear(); test.button("installUpdate")->click();
        require(test.restarts == 1 && test.downloads == 1, "Explicit retry must use the verified prepared update");
    }
    for (const bool queued : {false, true}) {
        UpdateInteraction test(&parent);
        test.button("installUpdate")->click();
        if (queued) test.finish();
        test.button("cancelUpdateDownload")->click();
        test.finish(); app.processEvents();
        require(test.cancellations == 1 && test.restarts == 0, "Cancel must revoke restart even if preparation already completed");
    }
    {
        UpdateInteraction test(&parent);
        test.button("installUpdate")->click();
        test.state.busy = false; test.state.error = "Signature verification failed."; test.page->refresh(); app.processEvents();
        require(test.restarts == 0, "Failed verification must not restart");
        test.button("installUpdate")->click(); test.finish(); app.processEvents();
        require(test.downloads == 2 && test.restarts == 1, "Failed preparation must permit a newly authorized retry");
    }
    {
        UpdateInteraction test(&parent);
        test.button("installUpdate")->click(); test.finish("2026091599"); app.processEvents();
        require(test.restarts == 0, "Approval must apply only to the requested build");
    }
    {
        UpdateInteraction test(&parent);
        test.finish(); app.processEvents();
        require(test.restarts == 0, "Cached preparation must not authorize activation");
        test.button("checkForUpdates")->click();
        require(!test.button("installUpdate")->isEnabled(), "Fresh discovery must retire the previous prepared offer");
        test.button("rollbackUpdate")->click();
        require(test.rollbacks == 1 && test.restarts == 0, "Rollback must retain its separate explicit action");
    }
    for (const bool destroy : {false, true}) {
        UpdateInteraction test(&parent);
        test.button("installUpdate")->click(); test.finish();
        if (destroy) test.page.reset();
        else test.page->cancelPendingInstallation(); // Settings finished, before deferred widget deletion.
        app.processEvents();
        require(test.cancellations == 1 && test.restarts == 0, "Closing Settings must revoke queued restart immediately");
    }
}
}
int verify_updates_ui(QApplication& app, AssemblyWorkspaceWindow& window, const std::filesystem::path& directory) {
    try {
        window.showMaximized(); app.processEvents();
        verify_install_interactions(app, window);
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
        require(!cadAiProvider()->connected() && !cadAiProvider()->busy(),"Opening Settings started AI");
        dialog->show_ai();app.processEvents();
        require(tabs->currentWidget()->objectName()=="aiSettingsPage","Missing AI Settings section");
        auto* ai_executable=dialog->findChild<QLineEdit*>("aiExecutable");
        require(ai_executable!=nullptr,"Missing AI executable preference");
        const auto previous_ai=CadAi::preferences();
        ai_executable->setText("cancelled-ai-executable");
        require(window.grab().save(QString::fromStdU16String((directory/"ai-settings.png").u16string())),"Cannot capture AI Settings");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click(); app.processEvents();
        require(CadAi::preferences().executable==previous_ai.executable,"Settings Cancel persisted AI changes");
        require(service->automatic() == automatic, "Cancel changed startup preference");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if(auto* navigation=window.findChild<QToolButton*>("documentKindButton");navigation&&navigation->isVisible()) {
            navigation->setAttribute(Qt::WA_UnderMouse,false);navigation->setDown(false);
            navigation->grab().save(QString::fromStdU16String((directory/"document-switch-idle.png").u16string()));
            navigation->setAttribute(Qt::WA_UnderMouse,true);
            navigation->grab().save(QString::fromStdU16String((directory/"document-switch-hover.png").u16string()));
            navigation->setDown(true);
            navigation->grab().save(QString::fromStdU16String((directory/"document-switch-pressed.png").u16string()));
            navigation->setDown(false);navigation->setAttribute(Qt::WA_UnderMouse,false);
        }

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
        isolated.show_ai();isolated.findChild<QLineEdit*>("aiExecutable")->setText("chosen-native-codex");
        isolated.buttons()->button(QDialogButtonBox::Ok)->click(); app.processEvents();
        require(isolated.result() == QDialog::Accepted && !service->automatic(), "OK must persist startup check preference");
        require(!QSettings(config, QSettings::IniFormat).value("Updates/CheckAtStartup", true).toBool(), "Preference is missing from config");
        require(CadAi::preferences(config).executable=="chosen-native-codex","Settings OK did not persist AI preferences");
        std::cout << "Updates UI: one-action install, exact approval/cancellation lifetime, retry, rollback, progress, internal Settings, silent initial state, Cancel/OK and unsaved-document restart guard passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Updates UI: " << error.what() << '\n'; return 1;
    }
}
}
