int verify_sheet_exchange_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        kernel::OcctKernel kernel;std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::create_default();
        const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_SHEET_EXCHANGE");
        const bool same_body=qEnvironmentVariableIsSet("ZIMA_VERIFY_SHEET_SAME_BODY");
        if(fixture!="1")part=document::PartDocument::load(fixture.toStdString(),&cache);
        else {
            static_cast<void>(part.body_history.create_body("Source"));
            auto box=test::rectangular_feature(part,{100,80,3});
            part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
            cache=workspace::calculate_part_with_resolved_references(kernel,part);
            if(!same_body)static_cast<void>(part.body_history.create_body("Sheet"));
        }
        const auto target=std::ranges::find_if(part.body_history.bodies(),[](const auto& body){return body.entries.empty();});
        const auto target_id=same_body?part.body_history.active_body_id():target==part.body_history.bodies().end()?part.body_history.create_body("Conversion verification"):target->scope.id;
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
        std::set<std::pair<std::string,std::string>> current_faces;
        for(const auto& face:source_mesh.triangle_references)
            current_faces.emplace(face.owner_id,face.semantic_key);
        std::map<std::string,const kernel::FaceReference*> offered_faces;
        const auto offered_geometry=workspace::construction_reference_source_geometry(cache);
        for(const auto& face:offered_geometry.triangle_references)
            if(current_faces.contains({face.owner_id,face.semantic_key})&&
                    face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane)
                offered_faces.emplace(face.owner_id+":"+face.semantic_key,&face);
        check(!offered_faces.empty(),"Sheet exchange fixture offers no current planar face");
        auto seed=*std::ranges::max_element(offered_faces,{},[](const auto& entry) {
            return entry.second->measured_area.value_or(0);
        })->second;
        const auto requested_face=qEnvironmentVariable("ZIMA_VERIFY_SHEET_EXCHANGE_FACE");
        if(!requested_face.isEmpty()) {
            bool valid=false;const auto index=requested_face.toULongLong(&valid);
            check(valid&&index<offered_faces.size(),"Requested Sheet from Body GUI face is unavailable");
            seed=*std::next(offered_faces.begin(),static_cast<std::ptrdiff_t>(index))->second;
        }
        auto direction=seed.surface->axis;if(seed.surface->reversed){direction.x=-direction.x;direction.y=-direction.y;direction.z=-direction.z;}
        view->set_view_direction(direction);view->fit_all();flush();
        const auto dialog=[&]{auto* result=dynamic_cast<app::SheetFromBodyDialog*>(window.findChild<QDialog*>("sheetFromBodyDialog"));check(result&&result->isVisible(),"Sheet from Body dialog did not open");return result;};
        const auto pick=[&] {
            const auto thickness=dialog()->findChild<QDoubleSpinBox*>("sheetSourceThickness")->value();
            // Exact user-file regressions exercise the persisted command and
            // resulting View independently of camera-facing picker coverage.
            // The ordinary fixture below still verifies the real View picker.
            if(!requested_face.isEmpty()) {
                dialog()->set_reference(seed,1);
                flush();
                check(dialog()->selected().has_value(),
                    "Exact source face was not accepted by Sheet from Body");
                check(dialog()->findChild<QDoubleSpinBox*>("sheetSourceThickness")->value()==thickness,
                    "Exact source face replaced the configured sheet thickness");
                return;
            }
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
            check(dialog()->findChild<QDoubleSpinBox*>("sheetSourceThickness")->value()==thickness,"Source selection replaced the configured sheet thickness");
        };
        const auto settings_before=app::ApplicationSettings::load();QTemporaryDir languages;
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(languages.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));config.sync();
            const auto settings=app::ApplicationSettings::load(languages.path());app::apply_application_translations(application,settings);
            action->trigger();flush();auto* current=dialog();check(current->windowType()==Qt::SubWindow,"Conversion dialog is not an internal window");
            auto* references=current->findChild<QTableWidget*>("sheetSourceFace");
            check(references->horizontalHeaderItem(1)->text()==settings.qt_translations.value("Výchozí rovinná plocha"),"Conversion face label is untranslated");
            auto* override=current->findChild<QCheckBox*>("sheetSourceThicknessOverride");
            auto* thickness=current->findChild<QDoubleSpinBox*>("sheetSourceThickness");
            check(override&&thickness&&override->text()==settings.qt_translations.value("Nedržet se tloušťky plechu")&&
                    !override->isChecked()&&!thickness->isEnabled(),
                "Conversion thickness inheritance is not localized or is not the default");
            auto* indicator=references->cellWidget(0,0);
            auto* arrow=qobject_cast<QWidget*>(indicator->property("_arrowWidget").value<QObject*>());
            auto* remove=qobject_cast<QPushButton*>(indicator->property("_removeWidget").value<QObject*>());
            check(arrow&&remove&&arrow->isVisible(),"Conversion reference arrow missing");
            check(arrow->toolTip()==settings.qt_translations.value("Zadejte referenci")&&remove->toolTip()==settings.qt_translations.value("Odstranit referenci"),"Conversion reference controls untranslated");
            const auto top=references->mapTo(current,QPoint{}).y();
            current->grab().save(QString::fromStdString((directory/"sheet-reference-before.png").string()));
            current->resize(current->width()+80,current->height()+150);flush();
            current->grab().save(QString::fromStdString((directory/"sheet-reference-after.png").string()));
            std::cerr<<"Reference top "<<top<<" -> "<<references->mapTo(current,QPoint{}).y()<<'\n';
            check(references->mapTo(current,QPoint{}).y()==top,"Conversion references moved vertically on resize");
            current->set_reference(seed,3);flush();check(remove->isVisible(),"Assigned reference has no remove action");
            remove->click();flush();check(!current->selected()&&current->active()&&arrow->isVisible(),"Removing reference did not restore entry");
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
        check(!view->mesh().triangles.empty(),
            "Sheet from Body history boundary hid the complete model View");
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto saved=save();check(saved.history.size()>original,"Conversion created no native sheet features");
        const auto conversion=std::ranges::find_if(saved.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::SheetFromBody;});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        if(same_body) {
            check(conversion!=saved.history.end()&&
                    !conversion->sheet_from_body.thickness_override&&
                    !conversion->sheet_from_body.generated_features.empty(),
                "Same-Body conversion did not persist its history boundary");
            QTreeWidgetItem* conversion_row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==
                        conversion->id){conversion_row=*it;break;}
            check(conversion_row,"Sheet from Body boundary is absent from the Tree");
            check(!conversion_row->icon(0).isNull(),
                "Sheet from Body boundary has no Tree icon");
            check(conversion_row->childCount()==static_cast<int>(
                    conversion->sheet_from_body.generated_features.size()),
                "Sheet from Body boundary does not own its generated Tree rows");
            auto* history_parent=conversion_row->parent();
            check(history_parent,"Sheet from Body boundary has no Body history owner");
            for(std::size_t index=0;
                    index<conversion->sheet_from_body.generated_features.size();
                    ++index) {
                const auto& id=conversion->sheet_from_body.generated_features[index];
                QTreeWidgetItem* feature_row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){feature_row=*it;break;}
                check(feature_row&&feature_row->parent()==conversion_row&&
                        conversion_row->indexOfChild(feature_row)==
                            static_cast<int>(index),
                    "Generated feature is not nested under its Sheet from Body boundary");
            }
        } else {
            check(conversion!=saved.history.end()&&
                    !conversion->sheet_from_body.replaces_body&&
                    conversion->sheet_from_body.source_body_id==
                        saved.body_history.order().front()&&
                    conversion->sheet_from_body.generated_features.size()==
                        conversion->sheet_from_body.child_bindings.size(),
                "Cross-Body conversion lost its associative history boundary");
            const auto* source_body=saved.body_history.find(
                saved.body_history.order().front());
            const auto* target_body=saved.body_history.find(target_id);
            check(source_body&&source_body->visible&&!source_body->suppressed&&
                    target_body&&target_body->visible&&!target_body->suppressed,
                "Cross-Body conversion did not retain both Bodies");
            QTreeWidgetItem* target_row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==target_id){target_row=*it;break;}
            check(target_row,"Cross-Body conversion target is absent from the Tree");
            QTreeWidgetItem* conversion_row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==
                        conversion->id){conversion_row=*it;break;}
            check(conversion_row&&conversion_row->parent()==target_row,
                "Cross-Body conversion boundary is not owned by its target Body");
            for(const auto& id:
                    conversion->sheet_from_body.generated_features) {
                QTreeWidgetItem* feature_row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){feature_row=*it;break;}
                check(feature_row&&feature_row->parent()==conversion_row,
                    "Cross-Body sheet feature is not nested under its conversion");
            }
        }
        check(window.findChild<QAction*>("sheetFromBodyAction")->isEnabled(),"Conversion unavailable in the active editable Body");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(save().history.size()==original,"Conversion Undo did not remove the whole batch");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(save().history.size()==saved.history.size(),"Conversion Redo lost features");
        const auto unchanged=save().serialized();
        for(const auto kind:{document::FeatureKind::Flat,document::FeatureKind::Bend}) {
            const auto feature=std::find_if(saved.history.begin()+original,saved.history.end(),
                [&](const auto& f){const auto* body=saved.body_owner_for_object(f.id);return body&&body->scope.id==target_id&&f.feature_kind==kind;});
            if(feature==saved.history.end())continue;
            QTreeWidgetItem* row=nullptr;
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
        if (same_body) {
            const auto row = [&](const std::string& id) -> QTreeWidgetItem* {
                for (QTreeWidgetItemIterator it(tree); *it; ++it)
                    if ((*it)->data(0, Qt::UserRole).toString().toStdString() == id)
                        return *it;
                return nullptr;
            };
            const auto remove = [&](const std::string& id) {
                auto* item = row(id);
                check(item, "Sheet deletion Tree row is missing");
                tree->setCurrentItem(item);
                tree->scrollToItem(item);
                flush();
                QTimer::singleShot(0, &window, [&] {
                    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                    if (!menu) return;
                    auto* action = menu->findChild<QAction*>("deleteHistoryObject");
                    if (!action || !action->isEnabled()) { menu->close(); return; }
                    menu->setActiveAction(action);
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(menu, &enter);
                });
                QMetaObject::invokeMethod(tree, "customContextMenuRequested", Qt::DirectConnection,
                    Q_ARG(QPoint, tree->visualItemRect(item).center()));
                flush();
                auto* confirmation = window.findChild<QDialog*>("deleteHistoryDialog");
                check(confirmation && confirmation->isVisible(), "Sheet deletion confirmation is missing");
                confirmation->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                flush();
                check(!row(id), "The selected Sheet from Body history row was not deleted");
            };
            const auto& generated = conversion->sheet_from_body.generated_features;
            remove(generated.front());
            check(row(conversion->id), "Deleting the first Flat removed the divider");
            for (const auto& id : generated | std::views::drop(1))
                check(row(id) && row(id)->foreground(0).color() == QColor(210, 75, 65),
                    "Deleting the first Flat did not retain its invalid dependent");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(row(generated.front()), "Flat deletion Undo did not restore its Tree row");
            remove(conversion->id);
            for (const auto& id : generated)
                check(row(id), "Deleting the divider removed a generated feature from the Tree");
            check(!view->mesh().triangles.empty(), "Deleting the divider did not restore the original Body");
        }
        window.grab().save(QString::fromStdString((directory/"sheet-conversion-result.png").string()));
        std::cout<<"Sheet exchange GUI: native picker, all languages, Cancel, middle-button OK, Undo/Redo and DXF passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
