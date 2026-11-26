static int verify_create_component(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& root) {
    try {
        const auto directory=root/("create_component-"+QUuid::createUuid().toString(QUuid::Id128).toStdString());
        std::filesystem::create_directories(directory);
        const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto r=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!r.ok)throw std::runtime_error(r.message);flush();return r.data;
        };
        run("cd",{{"path",document::path_to_utf8(directory)}});
        auto group=assembly::AssemblyDocument::create_default();const auto file=directory/"create-component.asmz";group.save(file);
        check(window.open_document_path(QString::fromStdString(file.string())),"Cannot open component creation fixture");window.showMaximized();flush();
        const auto editor=[&] {ComponentPropertiesDialog* result{};for(auto* candidate:window.findChildren<QDialog*>())if(auto* found=dynamic_cast<ComponentPropertiesDialog*>(candidate);found&&found->isVisible())result=found;return result;};
        for(const auto* type:{"part","assembly"})for(bool accept:{false,true}) {
            const auto before=run("documents");
            auto* action=window.findChild<QAction*>("createComponentAction");check(action&&action->isEnabled(),"Create component command missing");action->trigger();flush();
            auto* create=window.findChild<QDialog*>("newDocumentDialog");check(create,"Create component did not open New");
            for(auto* radio:create->findChildren<QRadioButton*>()) {
                const auto kind=radio->property("documentType").toString();
                check(radio->isEnabled()==(kind=="part"||kind=="assembly"),"Create component offers an unsupported document kind");
                if(kind==type)radio->setChecked(true);
            }
            const auto name=std::string("created-")+type+(accept?"-accept":"-cancel");
            create->findChild<QLineEdit*>("newDocumentFileName")->setText(QString::fromStdString(name));
            create->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();flush();
            auto* dialog=editor();check(dialog,"New did not continue to component placement");
            check(window.active_occurrence_path_for_test().empty(),"Component became active before placement confirmation");
            const auto position=dialog->findChildren<QDoubleSpinBox*>("componentTranslation");check(position.size()==3,"Component placement fields missing");
            position[0]->setValue(37);position[1]->setValue(-12);flush();
            auto* origin=dialog->findChild<QPushButton*>("componentDefaultOriginButton");check(origin&&!origin->icon().isNull(),"Default Origin button or icon missing");origin->click();flush();
            check(dialog->placement_references().size()==3,"Default Origin did not enter the three existing plane references");
            const auto pose=dialog->pending_value().placement;check(std::abs(pose.x)+std::abs(pose.y)+std::abs(pose.z)<1e-6,"Default Origin did not align component position");
            if(accept)dialog->buttons()->button(QDialogButtonBox::Ok)->click();else dialog->reject();flush();flush();
            check(!editor(),"Component placement did not close");
            check(window.active_occurrence_path_for_test().empty()!=accept,"Confirmed component activation or Cancel restoration failed");
            if(accept)window.deactivate_active_occurrence_for_test();flush();run("save");
            const auto saved=assembly::AssemblyDocument::load(file);
            const auto expected=std::string(type)=="part"?(accept?1:0):(accept?2:1);
            check(saved.components.size()==expected,"Component creation changed the wrong number of occurrences");
            if(accept) {
                check(saved.components.back().placement_references.size()==3,"Default Origin was not persisted");
                run("undo");run("save");check(assembly::AssemblyDocument::load(file).components.size()==expected-1,"Component create Undo did not remove one occurrence");
                run("redo");run("save");check(assembly::AssemblyDocument::load(file).components.size()==expected,"Component create Redo failed");
            }
        }
        std::cout<<"Create Part/Assembly: New, placement, Default Origin, Cancel, activation, persistence and Undo/Redo passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Create component GUI: "<<e.what()<<'\n';return 1;}
}
