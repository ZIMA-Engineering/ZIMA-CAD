static int verify_command_guidance_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::filesystem::create_directories(directory);
        const auto inherited=app::ApplicationSettings::load(QDir::currentPath());
        QTemporaryDir settings_directory;check(settings_directory.isValid(),"Cannot isolate guidance settings");
        const auto config_path=settings_directory.filePath("config.ini");
        {
            QSettings config(config_path,QSettings::IniFormat);
            config.setValue("Application/Language","cs");config.setValue("Application/Theme","light");config.setValue("Application/Vladimir","YES");
            config.setValue("Browser/OpenOnStartup",false);config.setValue("Updates/CheckAtStartup",false);
            for(auto it=inherited.resolved_paths.cbegin();it!=inherited.resolved_paths.cend();++it)config.setValue("Paths/"+it.key(),it.value());
            config.setValue("Paths/WorkingDirectory",QString::fromStdString(directory.string()));config.sync();
        }
        auto part=document::PartDocument::create_default();auto feature=test::rectangular_feature(part,{40,30,10});
        part.history={feature};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Guidance fixture"));
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        part.relations="d42 = 1\nlabel = d42 & model.mass\nmass = model.mass\n";
        kernel::OcctKernel kernel;const auto cache=kernel.evaluate_history(part.kernel_operations());
        const auto source=directory/"guidance.prtz";part.save(source,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()),settings_directory.path());window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(source.string())),"Cannot open guidance fixture");flush();
        auto* guidance=dynamic_cast<app::CommandGuidance*>(window.findChild<QWidget*>("vladimirGuidanceOverlay"));
        check(guidance&&guidance->isVisible(),"Initial command guidance is missing");
        const auto revision=window.execute_console_command("documents").data;
        window.findChild<QAction*>("relationsAction")->trigger();flush();
        check(guidance->message().contains("d42")&&guidance->message().contains(QObject::tr("Check the dimension reference in Relations; affected expressions are skipped until it is restored.")),"Relations did not publish its real missing-dimension guidance");
        auto* relations=window.findChild<QDialog*>("relationsDialog");
        if(!relations)for(auto* dialog:window.findChildren<QDialog*>())if(dynamic_cast<app::RelationsDialog*>(dialog))relations=dialog;
        check(relations,"Relations dialog is missing");relations->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==revision,"Inspecting guidance changed the document revision");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            {QSettings config(config_path,QSettings::IniFormat);config.setValue("Application/Language",language);config.sync();}
            auto settings=app::ApplicationSettings::load(settings_directory.path());app::apply_application_translations(application,settings);
            app::GlobalSettingsDialog dialog(settings,&window);dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();flush();
            auto* control=dialog.findChild<QCheckBox*>("globalVladimirGuidance");
            check(control&&control->text()==settings.qt_translations.value("Show Vladimír guidance")&&control->toolTip()==settings.qt_translations.value("Show command guidance in the upper-left corner of the View."),"Global guidance control is untranslated");
            control->setChecked(false);dialog.buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(app::ApplicationSettings::load(settings_directory.path()).vladimir,"Global Settings Cancel saved the guidance toggle");
            for(const auto* theme:{"light","dark"}) {
                settings.theme=theme;app::apply_application_appearance(application,settings);
                guidance->set_message(QObject::tr("You can select an object or start a command."));flush();
                check(guidance->message()==settings.qt_translations.value("You can select an object or start a command."),"Guidance text is untranslated");
                window.grab().save(QString::fromStdString((directory/(std::string("guidance-")+language+"-"+theme+".png")).string()));
            }
        }
        {QSettings config(config_path,QSettings::IniFormat);config.setValue("Application/Language","cs");config.sync();}
        app::apply_application_translations(application,app::ApplicationSettings::load(settings_directory.path()));
        for(bool enabled:{false,true}) {
            window.findChild<QAction*>("globalSettingsAction")->trigger();flush();
            auto* dialog=dynamic_cast<app::GlobalSettingsDialog*>(window.findChild<QDialog*>("globalSettingsDialog"));check(dialog,"Global Settings did not open");
            dialog->findChild<QCheckBox*>("globalVladimirGuidance")->setChecked(enabled);
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(guidance->isVisible()==enabled,"Global Settings did not update guidance immediately");
            check(app::ApplicationSettings::load(settings_directory.path()).vladimir==enabled,"Guidance toggle did not persist");
        }
        check(window.execute_console_command("documents").data==revision,"Guidance settings changed document history");
        std::cout<<"Workspace guidance, Relations, five languages, themes and live Settings OK/Cancel passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
