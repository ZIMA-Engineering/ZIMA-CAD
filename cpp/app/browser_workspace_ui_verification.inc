int verify_browser_workspace(QApplication& application) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        QTemporaryDir directory;
        QSettings settings(directory.filePath("config.ini"),QSettings::IniFormat);
        settings.setValue("Paths/WorkingDirectory",directory.path());
        settings.setValue("Browser/HomePage","");settings.sync();
        zima::app::AssemblyWorkspaceWindow window(directory.path(),directory.path());window.resize(1366,768);window.show();flush();
        check(window.execute_console_command("new part browser-context").ok,"Cannot create CAD context");
        check(window.execute_console_command("save").ok,"Cannot save CAD context");flush();
        auto* tabs=window.findChild<QTabBar*>("documentTabs");
        const int original=tabs->count();const auto document=tabs->tabData(tabs->currentIndex());
        auto* globe=window.findChild<QAction*>("newBrowserTabAction");check(globe,"Browser action missing");
        globe->trigger();flush();check(tabs->count()==original+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("web:"),"Browser is not an ordinary active tab");
        check(window.execute_console_command("browser.open").ok,"Desktop browser.open failed");flush();
        check(tabs->count()==original+2,"Multiple browser tabs not retained");
        window.findChild<QAction*>("globalSettingsAction")->trigger();flush();
        auto* dialog=window.findChild<QDialog*>("globalSettingsDialog");check(dialog,"Global settings missing");
        auto* home=dialog->findChild<QLineEdit*>("browserHomePage");check(home&&home->text().isEmpty(),"Blank home not preserved");
        home->setText("https://example.com/");dialog->reject();flush();
        check(zima::app::ApplicationSettings::load(directory.path()).browser.home.isEmpty(),"Cancel committed browser preferences");
        tabs->tabCloseRequested(tabs->currentIndex());flush();check(tabs->count()==original+1,"Closing browser changed CAD tabs");
        tabs->tabCloseRequested(tabs->currentIndex());flush();
        check(tabs->count()==original&&tabs->tabData(tabs->currentIndex())==document,"Last browser close did not restore CAD context");
        std::cout<<"Browser workspace tabs, CLI opening and settings Cancel passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
