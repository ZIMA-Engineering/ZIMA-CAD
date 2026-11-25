int verify_planar_surface_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const char* command) {
            const auto result=window.execute_console_command(command);check(result.ok,"Surface GUI command failed");flush();return result;
        };
        workspace::Workspace live;kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Surface UI");part.set_body_history(std::move(graph));
        live.add_part(part);live.activate(id);
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_rectangle(-5,-4,5,4));
        static_cast<void>(sketch.add_rectangle(-1,-1,1,1));
        static_cast<void>(sketch.add_rectangle(8,0,10,3));
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.feature.type=document::FeatureType::Sketch;
        feature.name=feature.feature.automatic_name="Sketch 01";
        workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,sketch);
        const auto path=directory/"surface-feature.prtz";
        live.open_part(id)->session.document().save(path,live.open_part(id)->session.calculated_boundaries());
        window.resize(1440,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Surface fixture");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Surface Body cannot activate");flush();
        const auto row=[&]()->QTreeWidgetItem* {
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
                   (*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
            return nullptr;
        };
        const auto editor=[&]()->PrimitivePropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())
                if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
            return nullptr;
        };
        auto* modeling=window.findChild<QAction*>("applicationModeAction0");
        check(modeling&&modeling->isEnabled(),"Modeling workspace unavailable");modeling->trigger();flush();
        auto* shortcut=window.findChild<QAction*>("surfaceFeatureAction");
        auto* tools=window.findChild<QToolBar*>("toolsToolbar");
        check(shortcut&&shortcut->isEnabled()&&tools,"Surface Feature shortcut unavailable");
        const auto actions=tools->actions();
        check(actions.indexOf(shortcut)==actions.indexOf(window.findChild<QAction*>("wrapReliefAction"))+1,
            "Surface Feature shortcut is not directly below Wrap relief");
        const auto before_shortcut=run("documents").data;const auto shortcut_calls=kernel::body_calculation_count.load();
        auto* button=qobject_cast<QToolButton*>(tools->widgetForAction(shortcut));
        check(button,"Surface Feature command button missing");button->click();flush();
        check(editor()&&editor()->windowType()==Qt::SubWindow&&editor()->pending_value().feature.planar_surface()&&
            editor()->findChild<QComboBox*>("featureType")->currentIndex()==planar_surface_type_index,
            "Surface shortcut did not preselect Surface in shared Properties");
        check(editor()->pending_value().name==QObject::tr("Plocha").toStdString()+" 01","Surface shortcut automatic name is incorrect");
        editor()->reject();flush();
        check(run("documents").data==before_shortcut&&kernel::body_calculation_count.load()==shortcut_calls,
            "Surface shortcut opening or Cancel changed the model");
        auto* surface_mode=window.findChild<QAction*>("applicationModeAction3");
        check(surface_mode&&surface_mode->isEnabled(),"Surface workspace unavailable");surface_mode->trigger();flush();
        check(!window.findChild<QAction*>("planarSurfaceAction"),"Obsolete standalone planar Surface action remains");
        auto* action=window.findChild<QAction*>("featurePrototypeAction");
        check(action&&action->isEnabled(),"Shared Feature action unavailable in Surface workspace");
        const auto original=run("documents").data;const auto count=kernel::body_calculation_count.load();
        action->trigger();flush();check(editor(),"New Feature did not open shared Properties");
        editor()->findChild<QComboBox*>("featureType")->setCurrentIndex(planar_surface_type_index);flush();
        check(editor()->pending_value().feature.planar_surface(),"Surface type does not define a planar face");
        editor()->reject();flush();
        check(run("documents").data==original&&kernel::body_calculation_count.load()==count,"New Surface Cancel changed the document or calculated geometry");
        check(row(),"Owned Sketch row missing");window.show_tree_item_properties(row());flush();
        check(editor()&&editor()->windowType()==Qt::SubWindow,"Surface uses a separate property dialog");
        auto* type=editor()->findChild<QComboBox*>("featureType");type->setCurrentIndex(planar_surface_type_index);flush();
        for(const auto* name:{"featureDirection","featureResult","featureSideMode0","featureSideMode1"})
            check(!editor()->findChild<QWidget*>(name)->isVisible(),"Surface exposes extrusion or rotation controls");
        check(editor()->findChild<QPushButton*>("featureSketchButton")->isVisible(),"Surface owned Sketch is unavailable");
        check(editor()->pending_value().name==QObject::tr("Plocha").toStdString()+" 01","Surface automatic naming failed");
        auto* origin=editor()->findChild<QPushButton*>("containerDefaultOriginButton");
        check(origin,"Surface shared Origin control missing");origin->click();flush();
        check(document::placement_references_use_whole_origin(editor()->pending_value().placement.references),"Surface did not adopt whole Origin");
        const auto settings_before=ApplicationSettings::load(QDir::currentPath());
        for(const auto* theme:{"light","dark","light"}) {
            auto settings=settings_before;settings.theme=theme;apply_application_appearance(application,settings);flush();
            check(!type->itemIcon(planar_surface_type_index).pixmap(24,24).isNull(),"Surface icon missing after theme change");
        }
        apply_application_appearance(application,settings_before);flush();
        QTemporaryDir translations;check(translations.isValid(),"Surface localization directory unavailable");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings ini(translations.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));
            ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
            const auto settings=ApplicationSettings::load(translations.path());apply_application_translations(application,settings);flush();
            check(type->itemText(planar_surface_type_index)==settings.qt_translations.value("Plocha"),"Surface Feature type did not translate");
        }
        apply_application_translations(application,settings_before);flush();
        check(run("documents").data==original&&kernel::body_calculation_count.load()==count,"Surface draft calculated or committed geometry");
        window.grab().save(QString::fromStdString((directory/"surface-feature-properties.png").string()));
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        if(editor())throw std::runtime_error("Surface OK failed: "+editor()->findChild<QLabel*>("propertiesSubmitError")->text().toStdString());
        run("save");std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
        const auto* stored=saved.find_container(feature.id);
        check(stored&&stored->feature.planar_surface()&&stored->feature.sketch_id==sketch.id&&
            document::placement_references_use_whole_origin(stored->placement.references),"Surface type, Sketch or placement did not persist");
        check(!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().volume)<1e-8&&
            std::abs(cache.back().surface_area-82)<1e-6,"Surface lost a hole or disconnected region");
        const auto accepted=run("documents").data;const auto calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();check(editor(),"Surface could not reopen shared Properties");
        check(editor()->findChild<QComboBox*>("featureType")->currentIndex()==planar_surface_type_index,"Reopened Surface shows another type");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor()&&run("documents").data==accepted&&kernel::body_calculation_count.load()==calls,"Unchanged Surface OK calculated or committed");
        window.show_tree_item_properties(row());flush();
        editor()->findChild<QComboBox*>("featureType")->setCurrentIndex(feature_type_index(document::FeatureType::Modeling));flush();
        editor()->reject();flush();check(run("documents").data==accepted,"Surface Cancel changed the definition");
        run("undo");run("redo");run("save");run("close");
        check(window.open_document_path(QString::fromStdString(path.string())),"Surface native reopen failed");flush();
        window.show_tree_item_properties(row());flush();check(editor(),"Saved Surface cannot be edited");
        check(editor()->pending_value().feature.planar_surface(),"Surface lost type after Undo/Redo and reopen");editor()->reject();flush();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings ini(translations.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));ini.sync();
            const auto settings=ApplicationSettings::load(translations.path());apply_application_translations(application,settings);flush();
            modeling->trigger();flush();
            check(shortcut->text()==settings.qt_translations.value("Plocha"),"Surface shortcut did not translate");
        }
        apply_application_translations(application,settings_before);flush();
        std::cout<<"Surface Feature creation/convert, shared placement, hole/island geometry, no-op OK, Cancel, Undo/Redo and native reopen passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
