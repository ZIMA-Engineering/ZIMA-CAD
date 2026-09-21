int verify_new_document_options(QApplication& application) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(const QString language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir directory;
            QSettings config(directory.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);
            config.setValue("Application/UseISOFont",true);
            config.sync();
            app::AssemblyWorkspaceWindow window(directory.path());window.resize(1366,768);window.show();flush();
            const auto create=[&](const QString& type,const QString& name,int mode=0,bool a3=false) {
                window.findChild<QAction*>("newDocumentAction")->trigger();flush();
                auto* dialog=window.findChild<QDialog*>("newDocumentDialog");check(dialog,"New Document did not open");
                dialog->findChild<QLineEdit*>("newDocumentFileName")->setText(name);
                for(auto* radio:dialog->findChildren<QRadioButton*>())
                    if(radio->property("documentType").toString()==type)radio->setChecked(true);
                auto* modes=dialog->findChild<QComboBox*>("newDocumentPartMode");
                auto* formats=dialog->findChild<QComboBox*>("newDocumentDrawingFormat");
                check(modes&&formats&&modes->count()==2,"New Document option controls missing");
                check(modes->isEnabled()==(type=="part")&&formats->isEnabled()==(type=="drawing"),"Options do not follow document type");
                if(type=="part")modes->setCurrentIndex(mode);
                if(a3) {
                    int index=-1;for(int i=0;i<formats->count();++i)
                        if(QFileInfo(formats->itemData(i).toString()).fileName()=="ZE-A3.frmz")index=i;
                    check(index>=0,"Configured A3 frame is missing from chooser");formats->setCurrentIndex(index);
                }
                const auto capture=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"build"/("new-document-"+language.toStdString()+"-"+type.toStdString()+".png");
                dialog->grab().save(QString::fromStdString(capture.generic_string()));
                QTimer picker;
                QObject::connect(&picker,&QTimer::timeout,&window,[&]{
                    if(auto* file=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                        picker.stop();file->selectFile(directory.filePath("model.prtz"));
                        QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);
                    }
                });
                if(type=="drawing")picker.start(10);
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();picker.stop();flush();
                check(!window.findChild<QDialog*>("newDocumentDialog"),"New Document failed to commit");
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            };
            const auto check_drawing=[&](drawing::SheetFormat expected,const QString& name) {
                auto* view=dynamic_cast<app::DrawingWindow*>(window.findChild<QWidget*>("drawingWorkspace"));
                check(view,"Drawing workspace is missing");
                const auto& sheet=view->document_for_test().sheets.front();
                check(sheet.format==expected&&!sheet.frame_lines.empty()&&!sheet.title_block_lines.empty(),"New Drawing lacks selected frame or title block");
                check(sheet.title_block_locale==language.toStdString(),"New Drawing has the wrong title-block language");
                check(!sheet.title_block_images.empty(),"New Drawing did not embed the company logo");
                check(!view->document_for_test().source_document_id.empty()&&sheet.views.empty(),"Drawing source or initial view state is wrong");
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                const auto saved=drawing::DrawingDocument::load(std::filesystem::u8path(directory.filePath(name+".drwz").toStdString()));
                check(saved.sheets.front().format==expected&&saved.sheets.front().title_block_locale==language.toStdString()&&
                      !saved.sheets.front().frame_lines.empty()&&!saved.sheets.front().title_block_images.empty(),"Drawing defaults did not survive native save/reopen");
            };
            create("part","model");
            check(window.findChild<QComboBox*>("applicationModeSelector")->currentData().toInt()==0,"Modeling selection was ignored");
            const auto model=document::PartDocument::load(std::filesystem::u8path(directory.filePath("model.prtz").toStdString()));
            check(model.history.empty()&&model.body_history.bodies().size()==1&&!model.body_history.active_body_id().empty(),"New Part template context changed");
            window.findChild<QToolButton*>("documentKindButton")->click();flush();
            check_drawing(drawing::SheetFormat::A4,"model");
            create("part","sheet",1);
            check(window.findChild<QComboBox*>("applicationModeSelector")->currentData().toInt()==2,"Sheet Metal selection was ignored");
            const auto sheet=document::PartDocument::load(std::filesystem::u8path(directory.filePath("sheet.prtz").toStdString()));
            check(sheet.history.empty()&&sheet.body_history.bodies().size()==1,"Sheet Metal choice changed the Part model");
            create("assembly","assembly");
            window.findChild<QToolButton*>("documentKindButton")->click();flush();
            check_drawing(drawing::SheetFormat::A4,"assembly");
            create("drawing","chosen-format",0,true);
            check_drawing(drawing::SheetFormat::A3,"chosen-format");
            check(window.close(),"New Document tests left unsaved changes");
        }
        std::cout<<"New Document: Modeling/Sheet Metal, configured A3, linked Part/Assembly A4 and localized title blocks passed in five languages\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
