int verify_file_manager_workspace(QApplication& application) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
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
        QTemporaryDir directory;check(directory.isValid(),"Cannot create File Manager workspace fixture");
        QSettings settings(directory.filePath("config.ini"),QSettings::IniFormat);settings.setValue("Paths/WorkingDirectory",directory.path());settings.sync();
        zima::app::AssemblyWorkspaceWindow window(directory.path(),directory.path());
        window.resize(1366,768);window.show();flush();
        check(window.execute_console_command("new part file-manager-context").ok,"Cannot create File Manager CAD context");
        check(window.execute_console_command("save").ok,"Cannot save File Manager CAD context");flush();
        const QString part_path=directory.filePath("file-manager-context.prtz");check(QFileInfo::exists(part_path),"File Manager native fixture was not saved");
        auto* tabs=window.findChild<QTabBar*>("documentTabs");check(tabs,"Document tabs missing");
        const auto document=tabs->tabData(tabs->currentIndex());const int original=tabs->count();
        auto* action=window.findChild<QAction*>("newFileManagerTabAction");check(action,"File Manager action missing");
        action->trigger();flush();
        check(tabs->count()==original+1&&tabs->tabData(tabs->currentIndex()).toString().startsWith("files:"),"File Manager is not an ordinary active tab");
        auto* files=window.findChild<QStackedWidget*>("fileManagerStack");auto* browser=window.findChild<QStackedWidget*>("browserStack");auto* cad=window.findChild<QSplitter*>("documentSplitter");
        check(files&&files->isVisible()&&browser&&!browser->isVisible()&&cad&&!cad->isVisible(),"File Manager did not exclusively replace the document area");
        auto* table=window.findChild<QTreeWidget*>("fileManagerFiles");check(table,"File Manager file table missing");
        QTreeWidgetItem* part_item{};for(int row=0;row<table->topLevelItemCount();++row)if(table->topLevelItem(row)->text(2).compare("file-manager-context.prtz",Qt::CaseInsensitive)==0)part_item=table->topLevelItem(row);
        check(part_item,"Saved native Part is missing from File Manager");int name_column=-1;for(int column=6;column<table->columnCount();++column)if(table->headerItem()->data(column,Qt::UserRole).toString()=="name")name_column=column;
        check(name_column>=6,"Native Part parameters were not read");const auto cell=table->indexFromItem(part_item,name_column);QStyleOptionViewItem option;auto* editor=table->itemDelegate()->createEditor(table,option,cell);auto* line=qobject_cast<QLineEdit*>(editor);check(line,"Native Part parameter is not editable");line->setText("Edited from File Manager");table->itemDelegate()->setModelData(editor,table->model(),cell);delete editor;flush();
        check(zima::document::PartDocument::load(std::filesystem::u8path(part_path.toStdString())).user_parameters.at("name")=="Edited from File Manager","File Manager edit was not saved to the authoritative native Part");
        QSettings metadata(directory.filePath("0000-index/metadata.ini"),QSettings::IniFormat);check(metadata.value("Directory/Version").toInt()==3&&metadata.value("Files/file-manager-context/Provider").toString()=="zima-cad","Native edit did not create the metadata v3 provider mirror");
        action->trigger();flush();check(tabs->count()==original+2,"Multiple File Manager tabs were not retained");
        auto* web_action=window.findChild<QAction*>("newBrowserTabAction");check(web_action,"Browser action missing");web_action->trigger();flush();
        check(tabs->tabData(tabs->currentIndex()).toString().startsWith("web:")&&browser->isVisible()&&!files->isVisible(),"Browser switch retained the File Manager surface");
        int file_index=-1,document_index=-1;for(int index=0;index<tabs->count();++index){const auto id=tabs->tabData(index);if(file_index<0&&id.toString().startsWith("files:"))file_index=index;if(id==document)document_index=index;}
        check(file_index>=0&&document_index>=0,"File Manager or CAD tab disappeared");tabs->setCurrentIndex(file_index);flush();check(files->isVisible()&&!browser->isVisible()&&!cad->isVisible(),"Returning to File Manager retained another surface");
        tabs->setCurrentIndex(document_index);flush();check(cad->isVisible()&&!files->isVisible()&&!browser->isVisible(),"Returning to CAD retained a utility surface");
        const int before_open=tabs->count();choose_document(window,{});check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==document,"Cancelling File Open changed the CAD tabs or active context");
        tabs->setCurrentIndex(file_index);flush();const auto file_tab=tabs->tabData(tabs->currentIndex());choose_document(window,{});check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==file_tab&&files->isVisible(),"Cancelling File Open duplicated or replaced the existing File Manager");
        choose_document(window,part_path);check(tabs->count()==before_open&&tabs->tabData(tabs->currentIndex())==document&&cad->isVisible(),"File Open did not reuse the existing native document without adding a File Manager tab");
        for(int index=tabs->count()-1;index>=0;--index){const QString id=tabs->tabData(index).toString();if(id.startsWith("files:")||id.startsWith("web:")){emit tabs->tabCloseRequested(index);flush();}}
        check(tabs->count()==original&&tabs->tabData(tabs->currentIndex())==document&&cad->isVisible(),"Closing utility tabs did not restore CAD context");
        {
            zima::app::AssemblyWorkspaceWindow shutdown(directory.path(),directory.path());shutdown.show();flush();
            shutdown.findChild<QAction*>("newFileManagerTabAction")->trigger();flush();
            auto* shutdown_tabs=shutdown.findChild<QTabBar*>("documentTabs");const int shutdown_count=shutdown_tabs->count();const auto shutdown_active=shutdown_tabs->tabData(shutdown_tabs->currentIndex());choose_document(shutdown,{});check(shutdown_tabs->count()==shutdown_count&&shutdown_tabs->tabData(shutdown_tabs->currentIndex())==shutdown_active,"File Open duplicated the File Manager when no CAD document was open");
        }
        std::cout<<"File Manager native editing, tabs, File Open and exclusive CAD/browser switching passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
