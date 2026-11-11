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
            auto* included=current->findChild<QTableWidget*>("sheetIncludedFaces");
            auto* rips=current->findChild<QTableWidget*>("sheetRipSketches");
            check(included&&rips&&included->rowCount()==12&&rips->rowCount()==3,
                "Sheet from Body did not create the agreed initial reference slots");
            check(included->horizontalHeaderItem(1)->text()==
                    settings.qt_translations.value("Zahrnuté plochy")&&
                    rips->horizontalHeaderItem(1)->text()==
                    settings.qt_translations.value("RIP skici"),
                "Sheet from Body additional reference labels are untranslated");
            included->cellClicked(11,1);current->set_face_reference(seed,3);flush();
            check(included->rowCount()==13&&current->included_faces().size()==1,
                "Sheet from Body included-face slots did not expand");
            auto* included_indicator=included->cellWidget(11,0);
            auto* included_remove=qobject_cast<QPushButton*>(
                included_indicator->property("_removeWidget").value<QObject*>());
            check(included_remove&&included_remove->isVisible(),
                "Assigned included face has no remove action");
            included_remove->click();flush();
            check(current->included_faces().empty()&&current->face_entry(),
                "Removing an included face did not restore its input");
            rips->cellClicked(2,1);current->set_rip_reference(
                "sheet-ui:rip",QStringLiteral("RIP Sketch"));flush();
            check(rips->rowCount()==4&&current->rip_sketches()==
                    std::vector<std::string>{"sheet-ui:rip"},
                "Sheet from Body RIP slots did not expand");
            auto* rip_indicator=rips->cellWidget(2,0);
            auto* rip_remove=qobject_cast<QPushButton*>(
                rip_indicator->property("_removeWidget").value<QObject*>());
            check(rip_remove&&rip_remove->isVisible(),
                "Assigned RIP Sketch has no remove action");
            rip_remove->click();flush();
            check(current->rip_sketches().empty()&&current->rip_entry(),
                "Removing a RIP Sketch did not restore its input");
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
        const auto tree_row=[&](const std::string& id)
                ->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id)
                    return *it;
            return nullptr;
        };
        const auto run_tree_action=[&](const std::string& id,
                const char* object_name) {
            auto* item=tree_row(id);
            check(item,"Sheet from Body explode Tree row is missing");
            tree->setCurrentItem(item);tree->scrollToItem(item);flush();
            bool available{};
            const auto name=QString::fromLatin1(object_name);
            QTimer::singleShot(0,&window,[&,name] {
                auto* menu=qobject_cast<QMenu*>(
                    QApplication::activePopupWidget());
                if(!menu)return;
                auto* command=menu->findChild<QAction*>(name);
                available=command&&command->isEnabled();
                if(!available){menu->close();return;}
                menu->setActiveAction(command);
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,
                    Qt::NoModifier);
                QApplication::sendEvent(menu,&enter);
            });
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",
                Qt::DirectConnection,
                Q_ARG(QPoint,tree->visualItemRect(item).center()));
            flush();check(available,
                "Sheet from Body explode context action is unavailable");
        };
        const auto& generated=conversion->sheet_from_body.generated_features;
        check(!generated.empty(),
            "Sheet from Body has no generated part to explode");
        run_tree_action(generated.front(),
            "explodeSheetFromBodyPartAction");
        const auto partial=save();
        const auto partial_parent=std::ranges::find_if(partial.history,
            [&](const auto& feature){return feature.id==conversion->id;});
        check(partial_parent!=partial.history.end(),
            "Partial explode removed the Sheet from Body owner");
        const auto partial_binding=std::ranges::find(
            partial_parent->sheet_from_body.child_bindings,
            generated.front(),
            &document::SheetFromBodyChildBinding::feature_id);
        check(partial_binding!=partial_parent->sheet_from_body.
                child_bindings.end()&&partial_binding->detached&&
                tree_row(generated.front())&&
                tree_row(generated.front())->parent()==tree_row(target_id),
            "Partial explode did not detach its generated Tree part");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        const auto partial_restored=save();
        const auto restored_parent=std::ranges::find_if(
            partial_restored.history,
            [&](const auto& feature){return feature.id==conversion->id;});
        check(restored_parent!=partial_restored.history.end()&&
                std::ranges::none_of(
                    restored_parent->sheet_from_body.child_bindings,
                    [](const auto& binding){return binding.detached;})&&
                tree_row(generated.front())->parent()==
                    tree_row(conversion->id),
            "Partial explode Undo did not restore association nesting");

        run_tree_action(conversion->id,
            "explodeSheetFromBodyAllAction");
        const auto fully_exploded=save();
        check(std::ranges::none_of(fully_exploded.history,
                [&](const auto& feature){return feature.id==conversion->id;})&&
                !tree_row(conversion->id)&&
                std::ranges::all_of(generated,[&](const auto& id) {
                    return tree_row(id)&&tree_row(id)->parent()==
                        tree_row(target_id);
                }),
            "Complete explode did not retain every native part at Body level");
        if(same_body)
            check(std::ranges::all_of(
                    fully_exploded.history.begin(),
                    fully_exploded.history.begin()+
                        static_cast<std::ptrdiff_t>(original),
                    [](const auto& feature){return feature.suppressed;}),
                "Same-Body explode restored the source solid material");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        const auto all_restored=save();
        check(std::ranges::any_of(all_restored.history,
                [&](const auto& feature){return feature.id==conversion->id;})&&
                tree_row(conversion->id)&&
                std::ranges::all_of(generated,[&](const auto& id) {
                    return tree_row(id)&&tree_row(id)->parent()==
                        tree_row(conversion->id);
                }),
            "Complete explode Undo did not restore the conversion group");
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

int verify_line_bend_ui(QApplication& application,
        const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    const auto flush=[&] {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
    };
    try {
        auto part=document::PartDocument::create_default();
        const auto body=part.body_history.create_body("Line Bend UI");
        auto flat=document::PartDocument::create_sketch_container();
        flat.feature_kind=document::FeatureKind::Flat;
        flat.name="Flat";flat.flat.thickness_override=true;
        flat.flat.thickness=2;
        flat.flat.direction=document::ExtrusionDirection::Reverse;
        auto profile=sketcher::Sketch::create_default();
        profile.owner_container_id=flat.id;profile.name=flat.name;
        flat.flat.sketch_id=profile.id;
        static_cast<void>(profile.add_rectangle(-50,-40,50,40));
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);
        part.history.push_back(flat);part.sketches.push_back(profile);
        kernel::OcctKernel kernel;
        const auto calculated=workspace::calculate_part_with_resolved_references(
            kernel,part);
        const auto path=directory/"line-bend-ui.prtz";
        part.save(path,calculated);

        app::AssemblyWorkspaceWindow window(
            QString::fromStdString(directory.string()));
        window.resize(1300,900);window.show();
        check(window.open_document_path(
            QString::fromStdString(path.string())),
            "Line Bend fixture did not open");
        flush();
        check(activate_test_body(application,window,body),
            "Line Bend fixture Body could not activate");
        window.findChild<QAction*>("applicationModeAction2")->trigger();
        flush();
        auto* action=window.findChild<QAction*>("lineBendAction");
        check(action&&action->isEnabled()&&!action->icon().isNull(),
            "Line Bend toolbar action is missing");
        action->trigger();flush();
        auto* dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("lineBendPropertiesDialog"));
        check(dialog&&dialog->isVisible()&&dialog->windowType()==Qt::SubWindow,
            "Line Bend Properties did not open as an internal window");
        const auto draft=dialog->pending_value().first;
        check(dialog->mutate_sketch(draft.id,[](auto& sketch) {
            static_cast<void>(sketch.add_segment(-50,0,50,0));
        }),"Line Bend draft could not accept its bend line");
        auto* radius=dialog->findChild<QDoubleSpinBox*>("lineBendRadius");
        auto* angle=dialog->findChild<QDoubleSpinBox*>("lineBendAngle");
        auto* fixed=dialog->findChild<QComboBox*>("lineBendFixedSide");
        auto* reverse=dialog->findChild<QCheckBox*>("lineBendReverse");
        check(radius&&angle&&fixed&&reverse,
            "Line Bend parameter controls are incomplete");
        radius->setValue(6);angle->setValue(75);
        fixed->setCurrentIndex(fixed->findData(-1));reverse->setChecked(true);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("lineBendPropertiesDialog"),
            "Line Bend OK did not close Properties");
        const auto save=[&] {
            window.findChild<QAction*>("saveDocumentAction")->trigger();
            flush();return document::PartDocument::load(path);
        };
        auto saved=save();
        const auto bend=std::ranges::find_if(saved.history,
            [](const auto& feature) {
                return feature.feature_kind==document::FeatureKind::LineBend;
            });
        check(bend!=saved.history.end()&&bend->line_bend.radius==6&&
                bend->line_bend.angle_degrees==75&&
                bend->line_bend.fixed_side==-1&&bend->line_bend.reverse&&
                !bend->line_bend.segment_id.empty(),
            "Line Bend parameters did not persist");
        auto* view=dynamic_cast<viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>());
        check(view&&!view->mesh().triangles.empty(),
            "Line Bend produced no displayed sheet result");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator item(tree);*item;++item)
            if((*item)->data(0,Qt::UserRole).toString().toStdString()==
                    bend->id){row=*item;break;}
        check(row&&!row->icon(0).isNull(),
            "Line Bend is absent from the Tree or has no icon");
        const auto unchanged=saved.serialized();
        window.show_tree_item_properties(row);flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("lineBendPropertiesDialog"));
        check(dialog&&dialog->isVisible(),
            "Stored Line Bend cannot reopen for unchanged confirmation");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(save().serialized()==unchanged,
            "Unchanged Line Bend OK changed the document");
        row=nullptr;
        for(QTreeWidgetItemIterator item(tree);*item;++item)
            if((*item)->data(0,Qt::UserRole).toString().toStdString()==
                    bend->id){row=*item;break;}
        check(row,"Line Bend Tree row was not restored after unchanged OK");
        window.show_tree_item_properties(row);flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("lineBendPropertiesDialog"));
        check(dialog&&dialog->isVisible(),
            "Stored Line Bend cannot reopen for editing");
        dialog->findChild<QLineEdit*>("sketchName")->setText("Cancelled");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(save().serialized()==unchanged,
            "Line Bend Cancel changed the document");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        check(std::ranges::none_of(save().history,[](const auto& feature) {
                return feature.feature_kind==document::FeatureKind::LineBend;
            }),"Line Bend Undo did not remove the feature");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        check(std::ranges::any_of(save().history,[](const auto& feature) {
                return feature.feature_kind==document::FeatureKind::LineBend;
            }),"Line Bend Redo did not restore the feature");
        std::cout<<"Line Bend GUI: create, edit Cancel, save/reopen and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
