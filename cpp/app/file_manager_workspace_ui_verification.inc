#include "file_manager_material_verification.inc"

int verify_file_manager_workspace(QApplication& application) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto check_window_menu=[&](zima::app::AssemblyWorkspaceWindow& host){
        auto* tabs=host.findChild<QTabBar*>("documentTabs");
        auto* menu=host.findChild<QMenu*>("windowMenu");
        check(tabs&&menu,"Window menu or tabs are missing");
        host.raise();host.activateWindow();flush();
        auto* bar=host.menuBar();const QPoint point=bar->actionGeometry(menu->menuAction()).center();
        QMouseEvent press(QEvent::MouseButtonPress,point,bar->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(bar,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,bar->mapToGlobal(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(bar,&release);flush();
        check(menu->isVisible(),"Clicking Window did not open its menu");
        const auto actions=menu->actions();
        if(tabs->count()==0){check(actions.size()==1&&!actions.front()->isEnabled(),"Empty Window menu is not disabled");menu->hide();return;}
        check(actions.size()==tabs->count(),"Window menu does not list every native and utility tab");
        for(int index=0;index<tabs->count();++index){
            check(actions[index]->isCheckable()&&actions[index]->isChecked()==(index==tabs->currentIndex()),"Window menu active checkmark is incorrect");
            auto* marker=tabs->tabButton(index,QTabBar::RightSide)->findChild<QLabel*>("documentTabDirtyMarker");
            check(marker&&actions[index]->text()==tabs->tabText(index)+(marker->text().isEmpty()?QString{}:QStringLiteral(" *")),
                "Window menu title or unsaved marker differs from its tab");
        }
        std::vector<QVariant> ids;for(int index=0;index<tabs->count();++index)ids.push_back(tabs->tabData(index));
        menu->hide();bar->setActiveAction(nullptr);
        for(int index=0;index<actions.size();++index){actions[index]->trigger();flush();check(tabs->tabData(tabs->currentIndex())==ids[index],"Window menu selected the wrong tab");}
    };
    const auto close_with_f10=[&](zima::app::AssemblyWorkspaceWindow& host){
        auto* close=host.findChild<QAction*>("closeDocumentAction");
        check(close&&close->shortcuts().contains(QKeySequence(Qt::Key_F10))&&
            close->shortcuts().contains(QKeySequence(Qt::Key_F9))&&close->shortcuts().contains(QKeySequence::Close),
            "Close does not retain F10, F9 and the standard shortcut");
        check(close->isEnabled(),"Close is disabled for the active native or utility tab");
        host.raise();host.activateWindow();host.setFocus();flush();
        QKeyEvent press(QEvent::KeyPress,Qt::Key_F10,Qt::NoModifier);QApplication::sendEvent(&host,&press);
        QKeyEvent release(QEvent::KeyRelease,Qt::Key_F10,Qt::NoModifier);QApplication::sendEvent(&host,&release);flush();
    };
    const auto choose_document=[&](zima::app::AssemblyWorkspaceWindow& host,const QString& selected){
        bool dialog_seen=false,dialog_contract=false;QTimer chooser;chooser.setInterval(20);
        QObject::connect(&chooser,&QTimer::timeout,&host,[&]{
            auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)return;
            dialog_seen=true;const QString filters=dialog->nameFilters().join(';');
            dialog_contract=dialog->testOption(QFileDialog::DontUseNativeDialog)&&dialog->fileMode()==QFileDialog::ExistingFile;
            for(const auto* extension:{"*.prtz","*.asmz","*.drwz","*.frmz","*.tblz","*.symz"})dialog_contract=dialog_contract&&filters.contains(extension);
            chooser.stop();if(selected.isEmpty())dialog->reject();else{dialog->selectFile(selected);if(auto* name=dialog->findChild<QLineEdit*>("fileNameEdit"))name->setText(selected);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);}
        });
        chooser.start();host.findChild<QAction*>("openDocumentAction")->trigger();chooser.stop();flush();
        check(dialog_seen&&dialog_contract,"File Open did not use the existing localized native-document chooser");
    };
    try {
        const QString performance_source=qEnvironmentVariable("ZIMA_FILE_MANAGER_PERFORMANCE_SOURCE");
        if(!performance_source.isEmpty()){
            QTemporaryDir fixture;check(fixture.isValid(),"Cannot create performance fixture");
            const QString copied=fixture.filePath(QFileInfo(performance_source).fileName());check(QFile::copy(performance_source,copied),"Cannot isolate performance document");
            QSettings config(fixture.filePath("config.ini"),QSettings::IniFormat);config.setValue("Paths/WorkingDirectory",fixture.path());config.sync();
            zima::app::AssemblyWorkspaceWindow host(fixture.path(),fixture.path());host.resize(1366,768);host.show();flush();
            QElapsedTimer timer;timer.start();host.findChild<QAction*>("newFileManagerTabAction")->trigger();flush();const auto first_ms=timer.elapsed();
            auto* stack=host.findChild<QStackedWidget*>("fileManagerStack");auto* manager=dynamic_cast<zima::app::FileManagerTab*>(stack->currentWidget());check(manager,"Performance File Manager missing");
            timer.restart();const auto parameters=manager->native_parameters_requested(copied);const auto read_ms=timer.elapsed();check(parameters.error.isEmpty()&&!parameters.order.isEmpty(),"Performance metadata read failed");
            QSettings mirror(fixture.filePath("0000-index/metadata.ini"),QSettings::IniFormat);const auto stem=QFileInfo(copied).completeBaseName();
            check(mirror.value("Directory/Version").toInt()==3&&mirror.value("Directory/Parameters").toString().split(',').size()==parameters.order.size()&&mirror.value("Files/"+stem+"/Provider").toString()=="zima-cad","Saved native parameters are not discoverable by independent metadata readers");
            for(auto it=parameters.values.cbegin();it!=parameters.values.cend();++it)check(mirror.value("Parts/"+stem+QLatin1Char('/')+it.key()).toString()==it.value(),"Saved shared native parameter was not mirrored");
            for(auto it=parameters.localized_values.cbegin();it!=parameters.localized_values.cend();++it)for(auto language=it->cbegin();language!=it->cend();++language)check(mirror.value("Parts/"+stem+QLatin1Char('/')+it.key()+(language.key().isEmpty()?QString{}:QLatin1Char('/')+language.key())).toString()==language.value(),"Saved localized native parameter was not mirrored");
            if(qEnvironmentVariableIsSet("ZIMA_FILE_MANAGER_COMPARE_FULL_METADATA")){
                const auto full=zima::document::PartDocument::load(std::filesystem::u8path(copied.toStdString()));
                check(parameters.order.size()==full.user_parameter_order.size(),"Large metadata order changed");
                for(int index=0;index<parameters.order.size();++index)check(parameters.order[index].toStdString()==full.user_parameter_order[index],"Large metadata order changed");
                for(const auto& [key,value]:full.user_parameters)check(parameters.values.value(QString::fromStdString(key)).toStdString()==value,"Large shared parameter changed");
                for(const auto& [key,values]:full.user_parameter_values)for(const auto& [language,value]:values)check(parameters.localized_values.value(QString::fromStdString(key)).value(QString::fromStdString(language)).toStdString()==value,"Large localized parameter changed");
            }
            timer.restart();manager->navigate(fixture.path(),false);flush();const auto warm_ms=timer.elapsed();
            std::cout<<"FILE_MANAGER_PERFORMANCE bytes="<<QFileInfo(copied).size()<<" first_ms="<<first_ms<<" native_read_ms="<<read_ms<<" warm_ms="<<warm_ms<<" parameters="<<parameters.order.size()<<'\n';
            host.close();return 0;
        }
        QTemporaryDir directory;check(directory.isValid(),"Cannot create File Manager workspace fixture");
        QSettings settings(directory.filePath("config.ini"),QSettings::IniFormat);settings.setValue("Paths/WorkingDirectory",directory.path());settings.setValue("Application/Language","cs");settings.sync();
        zima::app::AssemblyWorkspaceWindow window(directory.path(),directory.path());
        window.resize(1366,768);window.show();flush();check_window_menu(window);
        check(window.execute_console_command("new part file-manager-context").ok,"Cannot create File Manager CAD context");
        check(window.execute_console_command("save").ok,"Cannot save File Manager CAD context");flush();check_window_menu(window);
        const QString part_path=directory.filePath("file-manager-context.prtz");check(QFileInfo::exists(part_path),"File Manager native fixture was not saved");
        const QString thumbnail=directory.filePath("0000-index/thumbnails/file-manager-context.prtz.png");QImage saved_thumbnail(thumbnail);check(!saved_thumbnail.isNull()&&saved_thumbnail.width()<=320&&saved_thumbnail.height()<=240,"Save did not create a bounded PNG preview");
        auto* tabs=window.findChild<QTabBar*>("documentTabs");check(tabs,"Document tabs missing");
        const auto document=tabs->tabData(tabs->currentIndex());const int original=tabs->count();
        auto* action=window.findChild<QAction*>("newFileManagerTabAction");check(action,"File Manager action missing");
        action->trigger();flush();
        check(tabs->count()==original+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("files:"),"File Manager is not an ordinary active tab");
        check_window_menu(window);
        auto* files=window.findChild<QStackedWidget*>("fileManagerStack");auto* browser=window.findChild<QStackedWidget*>("browserStack");auto* cad=window.findChild<QSplitter*>("documentSplitter");
        check(files&&files->isVisible()&&browser&&!browser->isVisible()&&cad&&!cad->isVisible(),"File Manager did not exclusively replace the document area");
        auto* table=window.findChild<QTreeWidget*>("fileManagerFiles");check(table,"File Manager file table missing");
        auto* live_manager=dynamic_cast<zima::app::FileManagerTab*>(files->currentWidget());check(live_manager,"Active File Manager is missing");
        auto* retained_view=live_manager->findChild<QTabWidget*>("fileManagerViews");check(retained_view&&retained_view->currentIndex()==1,"New File Manager did not start in Files");
        auto parameter_request=nlohmann::json{{"command","document.parameters.get"},{"arguments",{{"document",document.toString().toStdString()}}}};auto rows=window.execute_console_command(QString::fromStdString(parameter_request.dump())).data.at("parameters");for(auto& row:rows)if(row.at("key")=="name")row["values"][""]="Unsaved name from the open Part";
        auto parameter_change=nlohmann::json{{"command","document.parameters.set"},{"arguments",{{"document",document.toString().toStdString()},{"parameters",rows}}}};check(window.execute_console_command(QString::fromStdString(parameter_change.dump())).ok,"Cannot prepare an unsaved native parameter change");live_manager->navigate(directory.path(),false);flush();check_window_menu(window);
        check(zima::document::PartDocument::load(std::filesystem::u8path(part_path.toStdString())).user_parameters.at("name")!="Unsaved name from the open Part","Parameter inspection unexpectedly saved the open document");
        QSettings saved_mirror(directory.filePath("0000-index/metadata.ini"),QSettings::IniFormat);check(saved_mirror.value("Parts/file-manager-context/name").toString()!="Unsaved name from the open Part","Inspection persisted an unsaved native parameter into the file cache");
        QTreeWidgetItem* part_item{};for(int row=0;row<table->topLevelItemCount();++row)if(table->topLevelItem(row)->text(0).compare("file-manager-context.prtz",Qt::CaseInsensitive)==0)part_item=table->topLevelItem(row);
        check(part_item,"Saved native Part is missing from File Manager");int name_column=-1;for(int column=5;column<table->columnCount();++column)if(table->headerItem()->data(column,Qt::UserRole).toString()=="name")name_column=column;
        check(name_column>=5,"Native Part parameters were not read");check(part_item->text(name_column)=="Unsaved name from the open Part","File Manager cache hid the authoritative unsaved parameter value");const auto cell=table->indexFromItem(part_item,name_column);QStyleOptionViewItem option;auto* editor=table->itemDelegate()->createEditor(table,option,cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Native Part parameter is not editable");line->setText("Edited from File Manager");table->itemDelegate()->setModelData(editor,table->model(),cell);delete editor;flush();
        check(zima::document::PartDocument::load(std::filesystem::u8path(part_path.toStdString())).user_parameters.at("name")=="Edited from File Manager","File Manager edit was not saved to the authoritative native Part");
        QSettings metadata(directory.filePath("0000-index/metadata.ini"),QSettings::IniFormat);check(metadata.value("Directory/Version").toInt()==3&&metadata.value("Files/file-manager-context/Provider").toString()=="zima-cad","Native edit did not create the metadata v3 provider mirror");
        action->trigger();flush();check(tabs->count()==original+2,"Multiple File Manager tabs were not retained");
        auto* web_action=window.findChild<QAction*>("newBrowserTabAction");check(web_action,"Browser action missing");web_action->trigger();flush();
        check(tabs->tabData(tabs->currentIndex()).toString().startsWith("web:")&&browser->isVisible()&&!files->isVisible(),"Browser switch retained the File Manager surface");check_window_menu(window);
        int file_index=-1,document_index=-1;for(int index=0;index<tabs->count();++index){const auto id=tabs->tabData(index);if(file_index<0&&id.toString().startsWith("files:"))file_index=index;if(id==document)document_index=index;}
        check(file_index>=0&&document_index>=0,"File Manager or CAD tab disappeared");tabs->setCurrentIndex(file_index);flush();check(files->isVisible()&&!browser->isVisible()&&!cad->isVisible(),"Returning to File Manager retained another surface");
        tabs->setCurrentIndex(document_index);flush();check(cad->isVisible()&&!files->isVisible()&&!browser->isVisible(),"Returning to CAD retained a utility surface");
        const int before_open=tabs->count();choose_document(window,{});check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==document,"Cancelling File Open changed the CAD tabs or active context");
        tabs->setCurrentIndex(file_index);flush();const auto file_tab=tabs->tabData(tabs->currentIndex());choose_document(window,{});check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==file_tab&&files->isVisible(),"Cancelling File Open duplicated or replaced the existing File Manager");
        choose_document(window,part_path);check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==document&&cad->isVisible(),"File Open did not reuse the existing native document without adding a File Manager tab");
        tabs->setCurrentIndex(file_index);flush();
        auto* manager=dynamic_cast<zima::app::FileManagerTab*>(files->currentWidget());check(manager&&manager->preview_file_requested,"File Manager does not expose native preview routing");
        check(manager==live_manager&&retained_view->currentIndex()==1,"Opening a native document reset Files to Overview");
        manager->open_file_requested(part_path);flush();check(tabs->tabData(tabs->currentIndex())==document,"File Manager filename did not activate the existing Part");
        tabs->setCurrentIndex(file_index);flush();check(retained_view->currentIndex()==1,"Returning from a File Manager file launch reset its view");
        const QString new_working=directory.filePath("pracovní-проект");QDir().mkpath(new_working);check(static_cast<bool>(manager->working_directory_requested),"File Manager does not expose working-directory routing");manager->working_directory_requested(new_working);flush();
        check(QDir::cleanPath(QString::fromStdString(window.execute_console_command("context").data.at("working_directory").get<std::string>()))==QDir::cleanPath(new_working),"Context action did not change the application working directory");
        for(auto* widget:window.findChildren<QWidget*>())if(auto* page=dynamic_cast<zima::app::FileManagerTab*>(widget)){auto* field=page->findChild<QLineEdit*>("fileManagerPath");check(field&&field->isReadOnly()&&field->text()==QDir::toNativeSeparators(new_working),"Working directory did not update all File Manager tabs");}
        auto* choose_working=manager->findChild<QToolButton*>("fileManagerChooseWorkingDirectory");check(choose_working&&choose_working->defaultAction()==window.findChild<QAction*>("workingDirectoryAction"),"Working directory button does not reuse the File menu action");
        bool chosen_working=false;QTimer working_chooser;working_chooser.setInterval(20);QObject::connect(&working_chooser,&QTimer::timeout,&window,[&]{if(auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){chosen_working=dialog->testOption(QFileDialog::DontUseNativeDialog)&&dialog->fileMode()==QFileDialog::Directory;working_chooser.stop();dialog->setDirectory(directory.path());dialog->selectFile(directory.path());QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);}});working_chooser.start();choose_working->click();working_chooser.stop();flush();check(chosen_working,"Working directory button did not open the shared directory chooser");
        const QString factory_formats=qEnvironmentVariable("ZIMA_FILE_MANAGER_VERIFY_FORMATS",QDir::current().filePath("config/formats"));const QString demo=directory.filePath("localized-formats");QDir().mkpath(demo);
        for(const auto* filename:{"ZE-TITLE-BLOCK.tblz","ZE-DRAWING-FRAME.frmz"})check(QFile::copy(QDir(factory_formats).filePath(filename),QDir(demo).filePath(filename)),"Cannot isolate localized native library examples");
        const QString demo_title=QDir(demo).filePath("ZE-TITLE-BLOCK.tblz");auto parameters=manager->native_parameters_requested(demo_title);check(parameters.order.contains("name")&&parameters.localized_values.value("name").size()==6,"Native library parameters did not expose all five languages and the shared value");
        for(const auto& language:{"cs","en","de","fr","ru"}){zima::app::ApplicationSettings localized;localized.language=language;localized.file_manager.default_view="files";manager->reload_settings(localized);manager->navigate(demo);auto* demo_table=manager->findChild<QTreeWidget*>("fileManagerFiles");int name_column=-1;for(int column=5;column<demo_table->columnCount();++column)if(demo_table->headerItem()->data(column,Qt::UserRole).toString()=="name")name_column=column;check(name_column>=5&&demo_table->headerItem()->text(name_column)==parameters.labels.value("name").value(language),"Library Name header did not follow the active language");for(int row=0;row<demo_table->topLevelItemCount();++row){auto* item=demo_table->topLevelItem(row);const auto native=manager->native_parameters_requested(item->data(0,Qt::UserRole).toString());check(item->text(name_column)==native.localized_values.value("name").value(language),"Library Name value did not follow the active language");}}
        zima::app::ApplicationSettings czech_demo;czech_demo.language="cs";czech_demo.file_manager.default_view="files";manager->reload_settings(czech_demo);manager->navigate(demo);auto* demo_table=manager->findChild<QTreeWidget*>("fileManagerFiles");QTreeWidgetItem* title_item{};for(int row=0;row<demo_table->topLevelItemCount();++row)if(demo_table->topLevelItem(row)->text(0)=="ZE-TITLE-BLOCK.tblz")title_item=demo_table->topLevelItem(row);check(title_item,"Localized title block row is missing");const auto name_cell=demo_table->indexFromItem(title_item,5);auto* name_editor=demo_table->itemDelegate()->createEditor(demo_table,option,name_cell);auto* name_line=qobject_cast<QLineEdit*>(name_editor);check(name_line,"Localized library parameter did not create an editor");name_line->setText("Kontrolní název razítka");demo_table->itemDelegate()->setModelData(name_editor,demo_table->model(),name_cell);delete name_editor;flush();
        const auto edited_library=zima::document::PartDocument::load(std::filesystem::u8path(demo_title.toStdString()));check(edited_library.user_parameter_values.at("name").at("cs")=="Kontrolní název razítka"&&edited_library.user_parameter_values.at("name").at("en")==parameters.localized_values.value("name").value("en").toStdString()&&edited_library.user_parameter_values.at("name").at("")==parameters.values.value("name").toStdString(),"Localized edit overwrote another language or the shared native value");
        QSettings demo_metadata(QDir(demo).filePath("0000-index/metadata.ini"),QSettings::IniFormat);check(demo_metadata.value("Parts/ZE-TITLE-BLOCK/name/cs").toString()=="Kontrolní název razítka"&&demo_metadata.value("Parts/ZE-TITLE-BLOCK/name/en").toString()==parameters.localized_values.value("name").value("en"),"Localized metadata mirror lost a language value");manager->navigate(directory.path());
        verify_file_manager_materials(application,*manager,directory.path());
        const QString dxf=directory.filePath("inspection.dxf");{QFile fixture(dxf);check(fixture.open(QIODevice::WriteOnly),"Cannot write DXF preview fixture");fixture.write("0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n10\n21\n5\n0\nENDSEC\n0\nEOF\n");}
        manager->navigate(directory.path());flush();auto* preview_table=manager->findChild<QTreeWidget*>("fileManagerFiles");QTreeWidgetItem* dxf_item{};
        for(int row=0;row<preview_table->topLevelItemCount();++row)if(preview_table->topLevelItem(row)->data(0,Qt::UserRole).toString()==dxf)dxf_item=preview_table->topLevelItem(row);
        check(dxf_item&&dxf_item->icon(zima::app::file_preview_column).isNull(),"DXF empty preview fixture is missing");preview_table->scrollToItem(dxf_item);flush();
        const auto preview_point=preview_table->visualRect(preview_table->indexFromItem(dxf_item,zima::app::file_preview_column)).center();
        const int before_preview=tabs->count();QMouseEvent preview_click(QEvent::MouseButtonDblClick,preview_point,preview_table->viewport()->mapToGlobal(preview_point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(preview_table->viewport(),&preview_click);flush();auto* preview=window.findChild<QStackedWidget*>("modelPreviewStack");check(tabs->count()==before_preview+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("preview:")&&preview&&preview->isVisible()&&!files->isVisible()&&!cad->isVisible()&&!browser->isVisible(),"DXF preview did not open as one exclusive main tab");
        check(!window.findChild<QAction*>("saveDocumentAction")->isEnabled()&&!window.findChild<QAction*>("saveDocumentAsAction")->isEnabled(),"Read-only preview enabled native save commands");
        manager->preview_file_requested(dxf);flush();check(tabs->count()==before_preview+1,"Repeated preview duplicated its main tab");check_window_menu(window);close_with_f10(window);check(tabs->count()==before_preview&&!preview->isVisible(),"Closing preview retained its surface or tab");
        for(int index=tabs->count()-1;index>=0;--index){const QString id=tabs->tabData(index).toString();if(id.startsWith("files:")||id.startsWith("web:")){tabs->setCurrentIndex(index);flush();close_with_f10(window);}}
        check(tabs->count()==original&&tabs->tabData(tabs->currentIndex())==document&&cad->isVisible(),"Closing utility tabs did not restore CAD context");
        {
            zima::app::AssemblyWorkspaceWindow shutdown(directory.path(),directory.path());shutdown.show();flush();
            shutdown.findChild<QAction*>("newFileManagerTabAction")->trigger();flush();
            auto* shutdown_tabs=shutdown.findChild<QTabBar*>("documentTabs");const int shutdown_count=shutdown_tabs->count();const auto shutdown_active=shutdown_tabs->tabData(shutdown_tabs->currentIndex());choose_document(shutdown,{});check(shutdown_tabs->count()==shutdown_count&&shutdown_tabs->tabData(shutdown_tabs->currentIndex())==shutdown_active,"File Open duplicated the File Manager when no CAD document was open");
            check_window_menu(shutdown);shutdown.findChild<QAction*>("newBrowserTabAction")->trigger();flush();check_window_menu(shutdown);
            const QString close_fixture=directory.filePath("close-first.prtz");check(QFile::copy(part_path,close_fixture),"Cannot isolate first-tab close fixture");
            choose_document(shutdown,close_fixture);
            check(shutdown_tabs->count()==3&&!shutdown_tabs->tabData(0).toString().startsWith("web:")&&
                !shutdown_tabs->tabData(0).toString().startsWith("files:"),"Native document is not the first tab");
            auto* close_button=shutdown_tabs->tabButton(0,QTabBar::RightSide)->findChild<QPushButton*>("documentTabCloseButton");
            check(close_button,"Native close button is missing");close_button->click();flush();
            check(shutdown_tabs->count()==2&&shutdown_tabs->currentIndex()>=0,"Closing the first native tab left no active remaining tab");
            auto* remaining_files=shutdown.findChild<QStackedWidget*>("fileManagerStack");
            auto* remaining_browser=shutdown.findChild<QStackedWidget*>("browserStack");
            auto* remaining_cad=shutdown.findChild<QSplitter*>("documentSplitter");
            for(int cycle=0;cycle<3;++cycle)for(int index=0;index<shutdown_tabs->count();++index) {
                shutdown_tabs->setCurrentIndex(index);flush();
                const bool file_tab=shutdown_tabs->tabData(index).toString().startsWith("files:");
                check(remaining_files->isVisible()==file_tab&&remaining_browser->isVisible()!=file_tab&&!remaining_cad->isVisible(),
                    "Remaining utility tabs lost exclusive page display after closing the first native tab");
            }
            while(shutdown_tabs->count()>0){check_window_menu(shutdown);const int count=shutdown_tabs->count();close_with_f10(shutdown);check(shutdown_tabs->count()==count-1,"F10 did not close a utility-only tab");}
            check_window_menu(shutdown);
        }
        {
            const QString native_directory=directory.filePath("keyboard-native");QDir().mkpath(native_directory);
            zima::app::AssemblyWorkspaceWindow keyboard(native_directory,directory.path());keyboard.show();flush();
            auto* native_tabs=keyboard.findChild<QTabBar*>("documentTabs");
            for(const auto* type:{"part","assembly","drawing"}){
                check(keyboard.execute_console_command(QString("new %1 F10-%1").arg(type)).ok,"Cannot create F10 native fixture");
                check(keyboard.execute_console_command("save").ok,"Cannot save F10 native fixture");flush();check_window_menu(keyboard);
                const int count=native_tabs->count();close_with_f10(keyboard);check(native_tabs->count()==count-1,"F10 did not close the saved native tab");
            }
            check(keyboard.execute_console_command("new part F10-PROMPT").ok&&keyboard.execute_console_command("save").ok,"Cannot create saved prompt fixture");flush();
            const auto native_id=native_tabs->tabData(native_tabs->currentIndex()).toString().toStdString();
            const auto change_name=[&](const char* value){
                auto get=nlohmann::json{{"command","document.parameters.get"},{"arguments",{{"document",native_id}}}};
                auto parameters=keyboard.execute_console_command(QString::fromStdString(get.dump())).data.at("parameters");
                for(auto& parameter:parameters)if(parameter.at("key")=="name")parameter["values"][""]=value;
                auto set=nlohmann::json{{"command","document.parameters.set"},{"arguments",{{"document",native_id},{"parameters",parameters}}}};
                check(keyboard.execute_console_command(QString::fromStdString(set.dump())).ok,"Cannot prepare unsaved F10 changes");flush();
            };
            const auto answer_prompt=[&](QMessageBox::StandardButton answer){
                bool seen=false;QTimer timer;timer.setInterval(20);
                QObject::connect(&timer,&QTimer::timeout,&keyboard,[&]{if(auto* prompt=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){seen=true;timer.stop();prompt->button(answer)->click();}});
                timer.start();close_with_f10(keyboard);timer.stop();check(seen,"F10 skipped the unsaved-change prompt");
            };
            change_name("F10 pending save");check_window_menu(keyboard);answer_prompt(QMessageBox::Cancel);
            check(native_tabs->count()==1&&native_tabs->tabData(native_tabs->currentIndex()).toString().toStdString()==native_id,"Cancel lost the native tab");
            answer_prompt(QMessageBox::Save);check(native_tabs->count()==0,"Save did not close the native tab");
            const auto path=std::filesystem::u8path(QDir(native_directory).filePath("F10-PROMPT.prtz").toStdString());
            check(zima::document::PartDocument::load(path).user_parameters.at("name")=="F10 pending save","F10 Save lost pending changes");
            check(keyboard.execute_console_command("open F10-PROMPT.prtz").ok,"Cannot reopen F10 prompt fixture");flush();
            change_name("F10 discard");answer_prompt(QMessageBox::Discard);check(native_tabs->count()==0,"Discard did not close the native tab");
            check(zima::document::PartDocument::load(path).user_parameters.at("name")=="F10 pending save","F10 Discard saved pending changes");
            check_window_menu(keyboard);
        }
        std::cout<<"File Manager native editing, tabs, File Open and exclusive CAD/browser switching passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
