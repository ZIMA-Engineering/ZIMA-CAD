int verify_curve_arc_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const char* command){auto r=window.execute_console_command(command);check(r.ok,"Arc GUI command failed");flush();return r;};
        auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Arc UI");part.set_body_history(std::move(graph));
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        for(kernel::Vec3 p:{kernel::Vec3{0,0,0},{10,0,10},{20,10,10}}) {
            auto point=document::create_owned_point(curve.id);point.origin=p;
            point.curve_tangent_enabled=true;point.curve_tangent=document::Curve3DTangentMode::PositiveX;
            curve.curve_points.push_back(point);
        }
        part.constructions.push_back(curve);auto authored_graph=part.body_history;
        authored_graph.insert({document::PartHistoryKind::Construction,curve.id});part.set_body_history(std::move(authored_graph));
        const auto file=directory/"arc-curve-ui.prtz";part.save(file);
        window.resize(1440,1000);window.show();check(window.open_document_path(QString::fromStdString(file.string())),"Arc GUI fixture failed to open");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Arc Body cannot activate");flush();
        const auto row=[&]()->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(window.findChild<QTreeWidget*>("documentTree"));*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==curve.id)return *it;
            return nullptr;
        };
        const auto editor=[&]()->ConstructionPropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<ConstructionPropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
            return nullptr;
        };
        check(row(),"Arc curve Tree row missing");
        const auto original=run("documents").data;const auto calls=kernel::body_calculation_count.load();
        window.show_tree_item_properties(row());flush();check(editor(),"Arc curve shared Properties did not open");
        auto* type=editor()->findChild<QComboBox*>("curve3DType");
        check(type&&type->findData(int(document::Curve3DType::Arc))>=0,"Arc curve type is missing");
        type->setCurrentIndex(type->findData(int(document::Curve3DType::Arc)));flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(view&&std::ranges::count_if(view->mesh().edges,[&](const auto& edge){return
            edge.reference.owner_id==curve.entity_id&&edge.reference.semantic_key.starts_with("curve:segment:")&&
            edge.points.size()>2&&edge.exact_spline.has_value();})==2,"Arc GUI preview does not display the two exact circular segments");
        for(std::size_t i=0;i<curve.curve_points.size();++i) {
            for(const auto* prefix:{"curve3DAxis","curve3DFlip","curve3DDirectionEnabled"}) {
                auto* field=editor()->findChild<QWidget*>(QString(prefix)+QString::number(i));
                check(field&&!field->isEnabled(),"Arc permits independent point direction editing");
            }
        }
        check(!editor()->findChild<QCheckBox*>("curve3DRounding")->isEnabled(),"Arc exposes corner rounding");
        const auto original_settings=ApplicationSettings::load(QDir::currentPath());
        QTemporaryDir translations;check(translations.isValid(),"Arc localization directory unavailable");
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings ini(translations.filePath("config.ini"),QSettings::IniFormat);
            ini.setValue("Application/Language",QString::fromLatin1(language));
            ini.setValue("Paths/Localization",QDir::current().absoluteFilePath("config/localization"));ini.sync();
            const auto settings=ApplicationSettings::load(translations.path());apply_application_translations(application,settings);flush();
            check(type->currentText()==settings.qt_translations.value("Oblouk"),"Arc type did not translate");
            check(editor()->findChild<QComboBox*>("curve3DAxis0")->toolTip()==settings.qt_translations.value("Arc directions follow the first plane normal and preceding arc tangent."),"Arc direction explanation did not translate");
        }
        apply_application_translations(application,original_settings);flush();
        check(editor()->pending_value().curve_points==curve.curve_points,"Arc mode changed point definitions");
        check(run("documents").data==original&&kernel::body_calculation_count.load()==calls,"Arc preview committed or calculated a solid");
        auto* buttons=editor()->findChild<QDialogButtonBox*>();check(buttons,"Arc confirmation actions missing");
        buttons->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Arc OK did not close Properties");run("save");
        const auto saved=document::PartDocument::load(file);const auto* stored=saved.find_construction(curve.id);
        check(stored&&stored->curve_type==document::Curve3DType::Arc&&stored->curve_points==curve.curve_points,"Arc save changed type or point identity");
        const auto route=document::curve3d_route(*stored);check(route.segments.size()==2&&route.segments[0].arc_midpoint.has_value(),"Arc GUI produced a different curve");
        const auto accepted=run("documents").data;
        window.show_tree_item_properties(row());flush();check(editor(),"Arc Properties cannot reopen");
        editor()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor()&&run("documents").data==accepted&&kernel::body_calculation_count.load()==calls,"Unchanged Arc OK changed the document or calculated a solid");
        window.show_tree_item_properties(row());flush();editor()->findChild<QComboBox*>("curve3DType")->setCurrentIndex(0);flush();editor()->reject();flush();
        check(run("documents").data==accepted,"Arc Cancel failed to restore type");
        run("undo");run("redo");run("save");run("close");check(window.open_document_path(QString::fromStdString(file.string())),"Arc native reopen failed");flush();
        window.show_tree_item_properties(row());flush();check(editor()&&editor()->pending_value().curve_type==document::Curve3DType::Arc,"Arc type lost after reopen");
        window.grab().save(QString::fromStdString((directory/"arc-curve-properties.png").string()));editor()->reject();flush();
        std::cout<<"Arc curve shared GUI, fixed tangent controls, identities, no-op OK, Cancel, Undo/Redo and reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
