int verify_application_lifecycle_ui(QApplication& application) {
    using namespace zima;
    const auto check = [](bool ok, const char* message) { if (!ok) throw std::runtime_error(message); };
    const auto flush = [&] {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
    };
    const auto configure = [](const QString& directory, const QString& language) {
        QSettings config(QDir(directory).filePath("config.ini"), QSettings::IniFormat);
        config.setValue("Application/Language", language);
        config.setValue("Paths/WorkingDirectory", directory);
        config.sync();
    };
    const auto run = [&](app::AssemblyWorkspaceWindow& window, const QString& command) {
        const auto result = window.execute_console_command(command);
        if (!result.ok) throw std::runtime_error(command.toStdString() + ": " + result.message);
        return result;
    };
    try {
        QTemporaryDir directory;
        check(directory.isValid(), "Cannot create lifecycle fixture directory");
        configure(directory.path(), "cs");
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            run(window, "new part close-part");
            run(window, "new assembly close-assembly");
            run(window, "new drawing close-drawing");
            run(window, R"({"command":"template.new","arguments":{"kind":"title_block","name":"close-title"}})");
            run(window, R"({"command":"template.new","arguments":{"kind":"drawing_format","name":"close-format"}})");
            const auto before = run(window, "documents").data;
            int prompts = 0;
            bool complete_warning = false;
            QTimer respond;
            QObject::connect(&respond, &QTimer::timeout, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    ++prompts;
                    complete_warning = message->text().contains("close-part") && message->text().contains("close-assembly") &&
                        message->text().contains("close-drawing");
                    message->button(QMessageBox::Cancel)->click();
                }
            });
            respond.start(10);
            check(!window.close(), "Cancel closed the application"); respond.stop();
            check(prompts == 1 && complete_warning && window.isVisible() && run(window, "documents").data == before,
                "Cancel changed the document set or history");
            QObject::disconnect(&respond, nullptr, nullptr, nullptr);
            QObject::connect(&respond, &QTimer::timeout, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    if (auto* save = message->button(QMessageBox::SaveAll)) save->click();
                    else { std::cerr << message->text().toStdString() << '\n'; message->accept(); }
                }
            });
            respond.start(10); check(window.close(), "Save All did not close"); respond.stop();
            for (const auto& file : {"close-part.prtz", "close-assembly.asmz", "close-drawing.drwz", "close-title.tblz", "close-format.frmz"})
                check(QFileInfo::exists(directory.filePath(file)), "Save All omitted a native document");
            for (const auto& doc : run(window, "documents").data)
                check(!doc.at("dirty").get<bool>(), "Save All left a dirty document");
        }
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            check(window.open_document_path(directory.filePath("close-part.prtz")), "Cannot reopen saved Part");
            check(window.close(), "Clean application close needed confirmation");
        }
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            run(window, "new part discarded");
            QTimer respond;
            QObject::connect(&respond, &QTimer::timeout, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    message->button(QMessageBox::Discard)->click();
            });
            respond.start(10); check(window.close(), "Explicit Discard did not close"); respond.stop();
            check(!QFileInfo::exists(directory.filePath("discarded.prtz")), "Discard wrote the document");
        }
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            run(window, "new part blocked-save");
            check(QDir().mkdir(directory.filePath("blocked-save.prtz")), "Cannot prepare failed save");
            int errors = 0;
            QTimer respond;
            QObject::connect(&respond, &QTimer::timeout, [&] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    if (auto* save = message->button(QMessageBox::SaveAll)) save->click();
                    else { ++errors; message->accept(); }
                }
            });
            respond.start(10); check(!window.close(), "Failed save closed the application"); respond.stop();
            check(errors > 0 && window.isVisible() &&
                window.execute_console_command("close").code == "unsaved_changes",
                "Failed save lost the unsaved session");
        }
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            window.findChild<QAction*>("newDocumentAction")->trigger(); flush();
            auto* edit = window.findChild<QDialog*>("newDocumentDialog");
            check(edit && edit->isVisible() && !window.close(), "Close discarded a pending property dialog");
            edit->reject(); flush();
            check(window.close(), "Finished dialog still blocked close");
        }
        {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
            run(window, "new part restart-cancelled");
            const auto before = run(window, "documents").data;
            const auto original_text = window.findChild<QAction*>("newDocumentAction")->text();
            for (bool accept_restart : {false, true}) {
                window.findChild<QAction*>("globalSettingsAction")->trigger(); flush();
                auto* settings = window.findChild<QDialog*>("globalSettingsDialog");
                settings->findChild<QComboBox*>("globalSettingsLanguage")->setCurrentText("en");
                QTimer respond;
                int prompts = 0;
                QObject::connect(&respond, &QTimer::timeout, [&] {
                    if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        ++prompts;
                        if (message->button(QMessageBox::Yes))
                            message->button(accept_restart ? QMessageBox::Yes : QMessageBox::No)->click();
                        else message->button(QMessageBox::Cancel)->click();
                    }
                });
                respond.start(10);
                settings->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
                respond.stop();
                check(prompts == (accept_restart ? 2 : 1) && window.isVisible() && !window.restart_state(),
                    "Restart refusal/cancel closed the application");
                check(run(window, "documents").data == before &&
                    window.findChild<QAction*>("newDocumentAction")->text() == original_text,
                    "Cancelled restart changed documents or partially translated the UI");
                check(app::ApplicationSettings::load(directory.path()).language == "en",
                    "Deferred language was not retained for next startup");
            }
        }
        configure(directory.path(), "cs");
        for (const QString language : {"en", "de", "fr", "ru", "cs"}) {
            std::optional<app::AssemblyWorkspaceWindow::RestartState> restart;
            {
                app::AssemblyWorkspaceWindow window(directory.path()); window.show(); flush();
                check(window.open_document_path(directory.filePath("close-part.prtz")), "Cannot open restart fixture");
                auto* settings_action = window.findChild<QAction*>("globalSettingsAction");
                const auto old_text = settings_action->text();
                settings_action->trigger(); flush();
                auto* settings = dynamic_cast<app::GlobalSettingsDialog*>(window.findChild<QDialog*>("globalSettingsDialog"));
                check(settings, "Missing Settings dialog");
                settings->findChild<QComboBox*>("globalSettingsLanguage")->setCurrentText(language);
                QTimer respond;
                int prompts = 0;
                QObject::connect(&respond, &QTimer::timeout, [&] {
                    if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        ++prompts;
                        message->button(QMessageBox::Yes)->click();
                    }
                });
                respond.start(10);
                settings->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); flush();
                respond.stop();
                check(prompts == 1 && window.restart_state().has_value() && !window.isVisible(),
                    "Language change did not request complete window restart");
                check(settings_action->text() == old_text, "Old window was partially translated");
                restart = window.restart_state();
                check(restart->documents.contains(directory.filePath("close-part.prtz")), "Restart omitted saved document");
            }
            app::AssemblyWorkspaceWindow reopened(restart->working_directory, restart->settings_directory);
            reopened.show(); flush();
            for (const auto& path : restart->documents) check(reopened.open_document_path(path), "Restart failed to reopen document");
            const auto settings = app::ApplicationSettings::load(restart->settings_directory);
            check(settings.language == language, "Restart loaded a different language");
            check(reopened.findChild<QAction*>("newDocumentAction")->text() == settings.text("menu.file.new", "Nový"),
                "New window menu retained the previous language");
            check(reopened.close(), "Reopened saved document became dirty");
        }
        for (int cycle = 0; cycle < 2; ++cycle) {
            app::AssemblyWorkspaceWindow window(directory.path()); window.show();
            QTimer::singleShot(0, &window, [&window] { window.close(); });
            check(application.exec() == 0, "Application event loop failed after a window restart");
        }
        std::cout << "Application close: Save All, Discard, Cancel, failure, pending edit; five-language restart and saved-document reopening passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
