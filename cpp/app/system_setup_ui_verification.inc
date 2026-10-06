#include "global_settings_dialog.hpp"

int verify_system_setup_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima::app;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    try {
        QTemporaryDir temporary(QString::fromStdString((directory/"setup-XXXXXX").string()));
        check(temporary.isValid(),"Cannot create setup fixture");
        auto settings=ApplicationSettings::load();
        apply_application_translations(application,settings);
        const auto root=temporary.path();QDir().mkpath(root+"/config");
        settings.installation_root=root;settings.config_path=root+"/config/config.ini";
        settings.base_config_path=settings.config_path;settings.platform_config_path.clear();settings.local_config_path.clear();
        settings.configured_paths["Templates"]=settings.resolved_paths.value("Templates");
        settings.configured_paths["WorkingDirectory"]=root+"/Projects";
        check(GlobalSettingsDialog::needs_initial_setup(settings),"New installation did not offer setup");
        QWidget owner;owner.resize(1200,900);owner.show();
        desktop::Context context;context.launcher=root+"/zima-cad.exe";context.id=desktop::installation_id(root);
        context.data=root+"/data";context.programs=root+"/programs";context.registry=root+"/registry.ini";
        context.registry_format=QSettings::IniFormat;context.installed=true;
        QFile launcher(context.launcher);check(launcher.open(QIODevice::WriteOnly),"Cannot create isolated launcher");launcher.write("fixture");launcher.close();
        {GlobalSettingsDialog dialog(settings,&owner,true,false,&context);dialog.show();application.processEvents();
            check(dialog.windowFlags().testFlag(Qt::SubWindow),"Setup escaped internal properties presentation");
            auto* buttons=dialog.findChild<QDialogButtonBox*>();check(buttons&&!buttons->button(QDialogButtonBox::Apply),"Setup exposes Apply");
            buttons->button(QDialogButtonBox::Cancel)->click();check(!QFileInfo::exists(settings.config_path),"Menu Cancel saved setup");}
        for(const auto& preset:{QString("mm"),QString("in")}) {
            GlobalSettingsDialog dialog(settings,&owner,true,false,&context);dialog.show();application.processEvents();
            auto* combo=dialog.findChild<QComboBox*>("setupUnitPreset");check(combo,"Preset missing");combo->setCurrentIndex(combo->findData(preset));
            check(dialog.findChild<QComboBox*>("setupPartTemplate")->currentText()=="START_PART_"+preset+".prtz","Preset did not select matching Part template");
            dialog.grab().save(QString::fromStdString((directory/("setup-"+preset.toStdString()+".png")).string()));
            dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();
            check(dialog.result()==QDialog::Accepted,"Setup OK did not save");
            settings=dialog.settings();QSettings saved(settings.config_path,QSettings::IniFormat);
            check(saved.value("Setup/Status")=="completed","Completion was not persisted");
            check(!GlobalSettingsDialog::needs_initial_setup(settings),"Completed setup offered again");
            const auto part=zima::document::PartDocument::load(std::filesystem::u8path((settings.resolved_paths.value("Templates")+"/"+settings.part_template).toStdString()));
            check(part.document_units.at("Length")==preset.toStdString(),"Native template units differ from preset");
            AssemblyWorkspaceWindow window(root+"/config");window.resize(1200,900);window.show();application.processEvents();
            for(const QString type:{"part","assembly"}) {
                window.findChild<QAction*>("newDocumentAction")->trigger();application.processEvents();
                auto* creation=window.findChild<QDialog*>("newDocumentDialog");check(creation,"New Document missing after setup");
                creation->findChild<QLineEdit*>("newDocumentFileName")->setText(type+"_"+preset);
                for(auto* radio:creation->findChildren<QRadioButton*>())if(radio->property("documentType").toString()==type)radio->setChecked(true);
                creation->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();
                if(type=="part")check(window.findChild<QAction*>("sketchAction")->isEnabled()&&window.findChild<QAction*>("featurePrototypeAction")->isEnabled(),"Preset Part did not activate editable Body");
                window.findChild<QAction*>("saveDocumentAction")->trigger();application.processEvents();
                QDirIterator files(root,{type+"_"+preset+(type=="part"?".prtz":".asmz")},QDir::Files,QDirIterator::Subdirectories);
                check(files.hasNext(),"GUI did not save the new preset document");
                const auto path=std::filesystem::u8path(files.next().toStdString());
                const auto units=type=="part"?zima::document::PartDocument::load(path).document_units:zima::assembly::AssemblyDocument::load(path).document_units;
                check(units.at("Length")==preset.toStdString(),"GUI new document overrode selected template units");
            }
        }
        QFile original(settings.config_path);check(original.open(QIODevice::ReadOnly),"Cannot read saved setup");const auto bytes=original.readAll();original.close();
        {GlobalSettingsDialog dialog(settings,&owner,true,false,&context);dialog.show();application.processEvents();
            dialog.findChild<QComboBox*>("setupPartTemplate")->setCurrentText("START_PART_mm.prtz");
            QTimer::singleShot(0,[]{for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget))box->accept();});
            dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            check(dialog.result()!=QDialog::Accepted,"Mismatched template was accepted");dialog.reject();}
        original.open(QIODevice::ReadOnly);check(original.readAll()==bytes,"Rejected template changed shared settings");original.close();
        {GlobalSettingsDialog dialog(settings,&owner,true,false,&context);dialog.show();application.processEvents();
            dialog.findChild<QComboBox*>("desktopIntegrationAction")->setCurrentIndex(1);
            dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();check(dialog.result()==QDialog::Accepted,"Isolated registration failed");}
        check(desktop::registered(context),"Setup did not call the existing registration service");desktop::remove(context);
        QFile::remove(settings.config_path);
        {GlobalSettingsDialog dialog(settings,&owner,true,true,&context);dialog.show();application.processEvents();dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();}
        check(!GlobalSettingsDialog::needs_initial_setup(settings),"Explicit skipping was not persisted");
        check(!desktop::registered(context),"Setup registered without an explicit registration request");
        for(const QString language:{"cs","en","de","fr","ru"}) {
            const auto folder=root+"/locale-"+language;QDir().mkpath(folder);
            QSettings config(folder+"/config.ini",QSettings::IniFormat);config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",settings.resolved_paths.value("Localization"));config.sync();
            auto localized=ApplicationSettings::load(folder);apply_application_translations(application,localized);
            GlobalSettingsDialog dialog(localized,&owner,true,false,&context);dialog.show();application.processEvents();
            check(dialog.windowTitle()==localized.qt_translations.value("System Setup"),"Setup title not translated");
            check(dialog.findChild<QComboBox*>("setupUnitPreset")->itemText(1)==localized.qt_translations.value("Metrické jednotky (mm)"),"Setup preset not translated");
            dialog.reject();
        }
        apply_application_translations(application,ApplicationSettings::load());
        std::cout<<"System Setup: internal dialog, matching metric/inch templates, transactional OK, menu Cancel and persisted first-launch skipping passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
