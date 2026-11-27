int verify_documentation_workspace(QApplication& application) {
    const auto check=[](bool ok,const char* message){if(!ok){std::cerr<<message<<std::endl;throw std::runtime_error(message);}};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto original=zima::app::ApplicationSettings::load();
    try {
        QTemporaryDir directory;
        QSettings settings(directory.filePath("config.ini"),QSettings::IniFormat);
        settings.setValue("Paths/WorkingDirectory",directory.path());
        settings.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));
        settings.setValue("Browser/HomePage","");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            std::cerr<<"Guide check: "<<language<<" configuration"<<std::endl;
            settings.setValue("Application/Language",language);settings.sync();
            const auto localized=zima::app::ApplicationSettings::load(directory.path());
            zima::app::apply_application_translations(application,localized);
            const auto index=zima::app::documentation::guide_url(localized);
            const auto expected=zima::app::documentation::guide_url(localized,"featureShortcut4Action");
            check(!index.isEmpty()&&!expected.isEmpty(),"Generated documentation pages are missing");
            check(expected.toLocalFile().endsWith(QString("/command-feature-shortcut4/0000-index/index_%1.html").arg(language)),"Command guide or language identity changed");
            check(zima::app::documentation::guide_url(localized,"unmappedAction").isEmpty(),"Unknown command silently opened another guide");
            zima::app::AssemblyWorkspaceWindow window(directory.path(),directory.path());window.resize(1366,900);window.show();flush();
            check(window.execute_console_command(QString("new part guide-%1").arg(language)).ok,"Guide test Part creation failed");
            check(window.execute_console_command("save").ok,"Guide test initial save failed");flush();
            auto* tabs=window.findChild<QTabBar*>("documentTabs");check(tabs,"Document tabs missing");
            const int count=tabs->count();const auto document=tabs->tabData(tabs->currentIndex());
            auto* action=window.findChild<QAction*>("featureShortcut4Action");check(action,"Feature command missing");
            QToolButton* button=nullptr;
            for(auto* item:window.findChildren<QToolButton*>("applicationCommandButton"))if(item->defaultAction()==action)button=item;
            check(button,"Feature toolbar button missing");
            std::cerr<<"Guide check: actual context menu"<<std::endl;
            bool menu_found=false;
            QTimer::singleShot(0,&window,[&]{
                auto* menu=window.findChild<QMenu*>("commandGuideMenu");
                if(!menu)return;
                auto* guide=menu->findChild<QAction*>("showCommandGuideAction");
                menu_found=guide&&guide->isEnabled()&&guide->text()==localized.text("menu.help.guide","");
                if(menu_found)guide->trigger();
                menu->close();
            });
            const auto position=button->rect().center();
            QContextMenuEvent context(QContextMenuEvent::Mouse,position,button->mapToGlobal(position));
            QApplication::sendEvent(button,&context);flush();
            std::cerr<<"Guide check: context menu returned"<<std::endl;
            check(menu_found,"Actual right-click guide menu or localized action missing");
            check(tabs->count()==count+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("web:"),"Guide did not open a new native tab");
            auto* view=window.findChild<QWebEngineView*>("browserView");check(view,"Guide browser view missing");
            QElapsedTimer loading;loading.start();
            while(view->url()!=expected&&loading.elapsed()<10000){flush();QThread::msleep(10);}
            check(view->url()==expected,"Guide opened the wrong command URL");
            bool rendered=false,pending=false;
            loading.restart();
            while(!rendered&&loading.elapsed()<10000){
                if(!pending){pending=true;view->page()->runJavaScript(
                    QString("document.readyState==='complete' && document.documentElement.lang==='%1' && document.querySelector('.guide-copy[lang=\"%1\"][data-command=\"featureShortcut4Action\"]')!==null && document.querySelectorAll('.procedure-steps li').length>=3 && document.querySelectorAll('img[data-native-ui-lang=\"%1\"]').length===2 && [...document.images].filter(i=>i.getAttribute('src')).every(i=>i.complete&&i.naturalWidth>0)").arg(language),
                    [&](const QVariant& result){rendered=result.toBool();pending=false;});}
                flush();QThread::msleep(10);
            }
            check(rendered,"Offline guide image or annotations did not render");
            tabs->tabCloseRequested(tabs->currentIndex());flush();
            std::cerr<<"Guide check: first guide closed"<<std::endl;
            check(tabs->count()==count&&tabs->tabData(tabs->currentIndex())==document,"Guide close did not restore CAD tab");
            auto* dirty=tabs->tabButton(tabs->currentIndex(),QTabBar::RightSide)->findChild<QLabel*>("documentTabDirtyMarker");
            check(dirty&&dirty->text().isEmpty(),"Guide changed native document dirty state");
            window.findChild<QAction*>("documentationAction")->trigger();flush();
            check(tabs->count()==count+1,"Help menu did not open the shared index");
            tabs->tabCloseRequested(tabs->currentIndex());flush();
            window.findChild<QAction*>("newFileManagerTabAction")->trigger();flush();
            auto* stack=window.findChild<QStackedWidget*>("fileManagerStack");
            auto* manager=dynamic_cast<zima::app::FileManagerTab*>(stack->currentWidget());
            check(manager,"Guide File Manager tab missing");
            manager->navigate(zima::app::documentation::root(localized),false);flush();
            auto* overview=manager->findChild<QWebEngineView*>("fileManagerOverviewWeb");
            check(overview,"Guide index missing from File Manager Overview");
            loading.restart();
            while(overview->url()!=index&&loading.elapsed()<10000){flush();QThread::msleep(10);}
            check(overview->url()==index,"File Manager and Help use different guide sources");
            tabs->tabCloseRequested(tabs->currentIndex());flush();
            // Existing editor ownership remains unchanged: help must not escape it.
            action->trigger();flush();
            std::cerr<<"Guide check: editing guard"<<std::endl;
            QDialog* editing=nullptr;
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())editing=dialog;
            check(editing,"Feature editing fixture did not open");
            window.findChild<QAction*>("documentationAction")->trigger();flush();
            check(tabs->count()==count,"Documentation escaped an active feature editor");
            editing->reject();
            flush();
            std::cout<<"Documentation context menu, native tab and unchanged document: "<<language<<" passed\n";
        }
        zima::app::apply_application_translations(application,original);
        return 0;
    } catch(const std::exception& error) {
        zima::app::apply_application_translations(application,original);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
