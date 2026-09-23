// GUI acceptance on a private copy; never save the user's source fixture.
int verify_sheet_corner_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::load(qEnvironmentVariable("ZIMA_VERIFY_SHEET_CORNER_FILE").toStdString(),&cache);
        const auto target=std::ranges::find_if(part.history.rbegin(),part.history.rend(),[](const auto& feature){return feature.feature_kind==document::FeatureKind::Bend;});
        check(target!=part.history.rend(),"Corner GUI fixture has no Sheet Profile");const auto id=target->id;
        const auto path=directory/"corner-properties.prtz";part.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Corner GUI fixture did not open");flush();
        const auto* body=part.body_owner_for_object(id);
        check(body&&activate_test_body(application,window,body->scope.id),"Corner GUI Body did not activate");
        auto* tree=window.findChild<QTreeWidget*>();check(tree,"Corner GUI tree missing");
        auto* dimension_view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(dimension_view,"Corner dimension view missing");
        std::set<std::string> dimension_owners{id,target->bend.sketch_id};
        for(const auto& data:target->bend.auxiliary_sketches)
            dimension_owners.insert(sketcher::Sketch::from_serialized(data).id);
        const auto has_profile_dimensions=[&]{return std::ranges::any_of(dimension_view->mesh().dimensions,
            [&](const auto& dimension){return dimension_owners.contains(dimension.reference.owner_id);});};
        const auto open=[&] {
            QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
            check(row,"Corner GUI history row missing");window.show_tree_item_properties(row);flush();
            auto* dialog=window.findChild<QDialog*>("bendPropertiesDialog");check(dialog&&dialog->windowType()==Qt::SubWindow,"Corner properties are not internal");return dialog;
        };
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto original_settings=app::ApplicationSettings::load();
        QTemporaryDir locale_directory;check(locale_directory.isValid(),"Corner language directory failed");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings locale(locale_directory.filePath("config.ini"),QSettings::IniFormat);
            locale.setValue("Application/Language",language);
            locale.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));locale.sync();
            const auto settings=app::ApplicationSettings::load(locale_directory.path());app::apply_application_translations(application,settings);
            auto* localized=open();
            check(localized->findChild<QCheckBox*>("bendCornerFirst")->text()==settings.qt_translations.value("První konec")&&
                localized->findChild<QCheckBox*>("bendCornerLast")->text()==settings.qt_translations.value("Druhý konec"),"Corner end controls are untranslated");
            for(const auto* text:{"Uzavření rohu","Vůle rohu"}) {
                const auto labels=localized->findChildren<QLabel*>();
                check(std::ranges::any_of(labels,[&](const auto* label){return label->text()==settings.qt_translations.value(text);}),"Corner field label is untranslated");
            }
            localized->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        }
        app::apply_application_translations(application,original_settings);
        auto* dialog=open();
        auto* first=dialog->findChild<QCheckBox*>("bendCornerFirst");auto* last=dialog->findChild<QCheckBox*>("bendCornerLast");auto* gap=dialog->findChild<QDoubleSpinBox*>("bendCornerGap");
        check(first&&last&&gap&&!first->isChecked()&&!last->isChecked()&&!gap->isEnabled(),"Corner controls or disabled defaults are wrong");
        last->setChecked(true);gap->setValue(.08);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(!has_profile_dimensions(),"Sheet Profile dimensions remained after Cancel");
        auto saved=save();check(!saved.find_container(id)->bend.corner[1]&&saved.find_container(id)->bend.corner_gap==.05,"Corner Cancel committed pending values");
        dialog=open();dialog->findChild<QCheckBox*>("bendCornerLast")->setChecked(true);dialog->findChild<QDoubleSpinBox*>("bendCornerGap")->setValue(.06);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("bendPropertiesDialog"),"Corner OK failed");
        check(!has_profile_dimensions(),"Sheet Profile dimensions remained after OK");
        saved=save();check(!saved.find_container(id)->bend.corner[0]&&saved.find_container(id)->bend.corner[1]&&saved.find_container(id)->bend.corner_gap==.06,"Corner OK did not persist independent ends");
        dialog=open();check(dialog->findChild<QCheckBox*>("bendCornerLast")->isChecked()&&dialog->findChild<QDoubleSpinBox*>("bendCornerGap")->value()==.06,"Reopening lost corner settings");
        check(has_profile_dimensions(),"Reopening Sheet Profile did not restore editing dimensions");
        dialog->findChild<QCheckBox*>("bendCornerFirst")->setChecked(true);dialog->findChild<QDoubleSpinBox*>("bendCornerGap")->setValue(.09);
        check(window.grab().save("build/sheet-corner/properties.png"),"Corner properties screenshot failed");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        saved=save();check(!saved.find_container(id)->bend.corner[0]&&saved.find_container(id)->bend.corner_gap==.06,"Reopened corner Cancel modified the feature");
        check(!has_profile_dimensions(),"Reopened Sheet Profile retained dimensions after Cancel");
        std::cout<<"Corner properties verified; checking Undo/Redo"<<std::endl;
        window.findChild<QAction*>("undoAction")->trigger();flush();
        saved=save();check(!saved.find_container(id)->bend.corner[1],"Corner Undo retained the enabled end");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        saved=save();check(saved.find_container(id)->bend.corner[1]&&saved.find_container(id)->bend.corner_gap==.06,"Corner Redo lost settings");
        std::cout<<"Corner Undo/Redo verified; capturing result"<<std::endl;
        app::AssemblyWorkspaceWindow result_window(QString::fromStdString(directory.string()));result_window.resize(1400,1000);result_window.show();
        check(result_window.open_document_path(QString::fromStdString(std::filesystem::absolute("build/sheet-corner/PLECH-CORNER.prtz").string())),"Closed corner copy did not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(result_window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(view,"Corner result view missing");view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();
        {QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();}flush();
        check(result_window.grab().save("build/sheet-corner/result.png"),"Corner result screenshot failed");
        std::cout<<"Corner properties: independent ends, preview, OK, reopening and Cancel passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
