int verify_sheet_exchange_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        kernel::OcctKernel kernel;std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::create_default();
        const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_SHEET_EXCHANGE");
        if(fixture!="1")part=document::PartDocument::load(fixture.toStdString(),&cache);
        else {
            static_cast<void>(part.body_history.create_body("Source"));
            auto box=document::PartDocument::create_box_container();box.box.height=3;
            part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
            cache=workspace::calculate_part_with_resolved_references(kernel,part);
            static_cast<void>(part.body_history.create_body("Sheet"));
        }
        const auto target=std::ranges::find_if(part.body_history.bodies(),[](const auto& body){return body.entries.empty();});
        const auto target_id=target==part.body_history.bodies().end()?part.body_history.create_body("Conversion verification"):target->scope.id;
        part.body_history.move_body(target_id,part.body_history.order().size()-1);part.body_history.activate(target_id);
        const auto fixture_bodies=part.body_history.bodies();
        for(auto body:fixture_bodies){body.visible=true;part.body_history.update_body(body);}
        const auto path=directory/"sheet-exchange.prtz";part.save(path,cache);const auto original=part.history.size();
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Sheet exchange fixture did not open");flush();
        check(activate_test_body(application,window,target_id),"Empty target Body could not activate");
        window.findChild<QAction*>("applicationModeAction2")->trigger();flush();
        auto* action=window.findChild<QAction*>("sheetFromBodyAction");auto* dxf=window.findChild<QAction*>("sheetDxfAction");
        check(action&&action->isEnabled()&&!action->icon().isNull()&&dxf&&!dxf->icon().isNull(),"Sheet exchange toolbar actions are missing");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Sheet exchange View missing");
        const auto& source_mesh=cache.back().body_outputs.at(part.body_history.order().front())->mesh;
        const auto seed=*std::ranges::max_element(source_mesh.original_references.triangle_references,{},[](const auto& face){return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane?face.measured_area.value_or(0):0;});
        auto direction=seed.surface->axis;if(seed.surface->reversed){direction.x=-direction.x;direction.y=-direction.y;direction.z=-direction.z;}
        view->set_view_direction(direction);view->fit_all();flush();
        const auto dialog=[&]{auto* result=dynamic_cast<app::SheetFromBodyDialog*>(window.findChild<QDialog*>("sheetFromBodyDialog"));check(result&&result->isVisible(),"Sheet from Body dialog did not open");return result;};
        const auto pick=[&] {
            view->fit_all();flush();
            std::optional<QPointF> hit;std::size_t cycles=0;
            for(int step:{50,5})for(int y=10;y<view->height()&&!hit;y+=step)for(int x=10;x<view->width();x+=step) {
                const QPointF p(x,y);const auto candidates=view->selection_candidates_at(p);
                for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].kind==viewer::CandidateKind::Face&&candidates[i].owner_id==seed.owner_id&&candidates[i].semantic_key==seed.semantic_key){hit=p;cycles=i;break;}
                if(hit)break;
            }
            if(!hit)window.grab().save(QString::fromStdString((directory/"sheet-pick-failure.png").string()));
            check(hit.has_value(),"Common picker does not offer source planar face in another Body");
            QMouseEvent move(QEvent::MouseMove,*hit,*hit,*hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
            for(std::size_t i=0;i<cycles;++i){
                QMouseEvent cycle(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&cycle);QApplication::sendEvent(view,&release);
            }
            QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
            check(dialog()->selected().has_value(),"Source face click was not accepted");
            check(std::abs(dialog()->findChild<QDoubleSpinBox*>("sheetSourceThickness")->value()-3)<1e-6,"Thickness inference failed");
        };
        const auto settings_before=app::ApplicationSettings::load();QTemporaryDir languages;
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(languages.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));config.sync();
            const auto settings=app::ApplicationSettings::load(languages.path());app::apply_application_translations(application,settings);
            action->trigger();flush();auto* current=dialog();check(current->windowType()==Qt::SubWindow,"Conversion dialog is not an internal window");
            check(current->findChild<QTableWidget*>("sheetSourceFace")->horizontalHeaderItem(0)->text()==settings.qt_translations.value("Výchozí rovinná plocha"),"Conversion face label is untranslated");
            current->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        }
        app::apply_application_translations(application,settings_before);
        action->trigger();flush();pick();dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).history.size()==original,"Conversion Cancel modified history");
        action->trigger();flush();pick();window.grab().save(QString::fromStdString((directory/"sheet-conversion-properties.png").string()));
        QMouseEvent middle(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(20,20),QPointF(20,20),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&middle);flush();
        check(!window.findChild<QDialog*>("sheetFromBodyDialog"),"Conversion OK failed");
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto saved=save();check(saved.history.size()>original,"Conversion created no native sheet features");
        check(!window.findChild<QAction*>("sheetFromBodyAction")->isEnabled(),"Conversion enabled for a nonempty Body");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(save().history.size()==original,"Conversion Undo did not remove the whole batch");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(save().history.size()==saved.history.size(),"Conversion Redo lost features");
        const auto unchanged=save().serialized();
        for(const auto kind:{document::FeatureKind::Flat,document::FeatureKind::Bend}) {
            const auto feature=std::ranges::find_if(saved.history,[&](const auto& f){const auto* body=saved.body_owner_for_object(f.id);return body&&body->scope.id==target_id&&f.feature_kind==kind;});
            if(feature==saved.history.end())continue;
            QTreeWidgetItem* row=nullptr;auto* tree=window.findChild<QTreeWidget*>("documentTree");
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature->id){row=*it;break;}
            check(row,"Generated feature is absent from the Tree");window.show_tree_item_properties(row);flush();
            auto* properties=window.findChild<QDialog*>(kind==document::FeatureKind::Flat?"flatPropertiesDialog":"bendPropertiesDialog");
            check(properties&&properties->isVisible(),"Generated native feature cannot reopen for editing");
            properties->findChild<QLineEdit*>("sketchName")->setText("cancelled edit");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            check(save().serialized()==unchanged,"Generated feature Cancel changed the document");
        }
        const auto before=save().serialized();dxf=window.findChild<QAction*>("sheetDxfAction");dxf->trigger();flush();
        const auto directory_name=app::ApplicationSettings::load().drawing_dxf_directory;
        const auto output=path.parent_path()/std::filesystem::path(directory_name.toStdWString())/"sheet-exchange.dxf";
        check(std::filesystem::exists(output)&&std::filesystem::file_size(output)>100,"Sheet DXF action did not produce configured file");
        check(save().serialized()==before,"Sheet DXF action changed document history");
        window.grab().save(QString::fromStdString((directory/"sheet-conversion-result.png").string()));
        std::cout<<"Sheet exchange GUI: native picker, all languages, Cancel, middle-button OK, Undo/Redo and DXF passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
