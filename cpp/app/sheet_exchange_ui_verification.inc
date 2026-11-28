int verify_sheet_exchange_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        kernel::OcctKernel kernel;std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::create_default();
        const auto fixture=qEnvironmentVariable("ZIMA_VERIFY_SHEET_EXCHANGE");
        const bool same_body=qEnvironmentVariableIsSet("ZIMA_VERIFY_SHEET_SAME_BODY");
        std::string rip_id,rip_owner;
        std::optional<kernel::FaceReference> authored_seed;
        std::vector<std::string> fixture_rips;
        if(fixture!="1")part=document::PartDocument::load(fixture.toStdString(),&cache);
        else {
            static_cast<void>(part.body_history.create_body("Source"));
            auto box=test::rectangular_feature(part,{100,80,3});
            auto size=part.sketches.back().create_segment_dimension(
                part.sketches.back().segments.front().id);
            part.sketches.back().apply_dimension(size);
            part.insert_history_entry(document::PartHistoryKind::Feature,box.id);part.history.push_back(box);
            auto rip_feature=document::PartDocument::create_sketch_container();
            auto rip=sketcher::Sketch::create_default();
            rip.owner_container_id=rip_feature.id;
            rip_feature.feature_kind=document::FeatureKind::Feature;
            rip_feature.feature.type=document::FeatureType::Sketch;
            rip_feature.feature.sketch_id=rip.id;
            rip_feature.placement.z=1.5;
            rip.plane_reference_owner_id=rip_feature.container_origin.id;
            static_cast<void>(rip.add_segment(-50,0,50,0));
            rip_id=rip.id;rip_owner=rip_feature.id;
            part.insert_history_entry(document::PartHistoryKind::Feature,rip_feature.id);
            part.history.push_back(rip_feature);part.sketches.push_back(rip);
            cache=workspace::calculate_part_with_resolved_references(kernel,part);
            if(!same_body)static_cast<void>(part.body_history.create_body("Sheet"));
        }
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CONVERTED_FLAT_CORRECTIONS")) {
            const auto conversion=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetFromBody;});
            check(conversion!=part.history.end(),"Correction fixture has no source conversion");
            const auto prefix=std::size_t(conversion-part.history.begin());
            authored_seed=conversion->sheet_from_body.source_face.reference;
            fixture_rips=conversion->sheet_from_body.rip_sketches;
            std::vector<std::pair<document::HistoryContainer,sketcher::Sketch>> preserved_rips;
            for(const auto& id:fixture_rips) {
                const auto sketch=std::ranges::find(part.sketches,id,&sketcher::Sketch::id);
                check(sketch!=part.sketches.end(),"Correction fixture lost its authored RIP");
                preserved_rips.emplace_back(*part.find_container(sketch->owner_container_id),*sketch);
            }
            while(part.history.size()>prefix)part.erase_history_object(part.history.back().id);
            for(auto& [owner,sketch]:preserved_rips)if(!part.find_container(owner.id)) {
                part.insert_history_entry(document::PartHistoryKind::Feature,owner.id);
                part.history.push_back(std::move(owner));part.sketches.push_back(std::move(sketch));
            }
            cache.clear();
        }
        if(cache.empty())cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto target=std::ranges::find_if(part.body_history.bodies(),[](const auto& body){return body.entries.empty();});
        const auto target_id=same_body?part.body_history.active_body_id():target==part.body_history.bodies().end()?part.body_history.create_body("Conversion verification"):target->scope.id;
        part.body_history.move_body(target_id,part.body_history.order().size()-1);part.body_history.activate(target_id);
        const auto fixture_bodies=part.body_history.bodies();
        for(auto body:fixture_bodies){body.visible=true;part.body_history.update_body(body);}
        const auto path=directory/"sheet-exchange.prtz";part.save(path,cache);const auto original=part.history.size();
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Sheet exchange fixture did not open");flush();
        const auto check_source_references=[&](const char* stage) {
            auto* source_tree=window.findChild<QTreeWidget*>("documentTree");
            check(source_tree,"Sheet source Tree is missing");
            for(QTreeWidgetItemIterator it(source_tree);*it;++it)
                if(part.find_container((*it)->data(0,Qt::UserRole).toString().toStdString())&&
                        (*it)->data(0,app::missing_reference_role).toBool())
                    throw std::runtime_error(std::string("Sheet source reference lost at ")+stage+": "+(*it)->text(0).toStdString());
        };
        check_source_references("open");
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
        auto seed=*std::ranges::max_element(offered_faces,{},[&](const auto& entry) {
            return entry.second->measured_area.value_or(0)+(fixture=="1"&&entry.second->surface->origin.z>0?1.:0.);
        })->second;
        if(authored_seed) {
            const auto found=offered_faces.find(authored_seed->owner_id+":"+authored_seed->semantic_key);
            check(found!=offered_faces.end(),"Correction fixture source face is unavailable");seed=*found->second;
        }
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
            if(!requested_face.isEmpty()||authored_seed) {
                dialog()->set_reference(seed,1);
                for(std::size_t i=0;i<fixture_rips.size();++i)dialog()->assign_rip(int(i),fixture_rips[i],QStringLiteral("RIP"));
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
            const auto boundary_note=settings.qt_translations.value(
                "Without selections, create only a history boundary that hides the preceding solid.");
            const auto labels=current->findChildren<QLabel*>();
            check(!boundary_note.isEmpty()&&std::ranges::any_of(labels,[&](auto* label){
                return label->text().contains(boundary_note);}),"Empty conversion explanation is untranslated");
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
            check_source_references("reference Cancel");
            action->trigger();flush();current=dialog();
            check(!current->selected(),"Empty conversion silently selected a source face");
            current->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check_source_references("empty boundary OK");
            check(!window.findChild<QDialog*>("sheetFromBodyDialog"),"Empty conversion OK did not close");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto boundary_document=document::PartDocument::load(path);
            check(boundary_document.history.size()==original+1&&
                    boundary_document.history.back().feature_kind==document::FeatureKind::SheetFromBody&&
                    boundary_document.history.back().sheet_from_body.generated_features.empty()&&
                    boundary_document.history.back().sheet_from_body.replaces_body,
                "Empty conversion did not persist only a replacement boundary");
            if(same_body)check(view->mesh().triangles.empty(),"Empty conversion left source material visible");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check_source_references("empty boundary Undo");
            check(!view->mesh().triangles.empty(),"Empty conversion Undo did not restore the model");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check_source_references("empty boundary Redo");
            if(same_body)check(view->mesh().triangles.empty(),"Empty conversion Redo displayed the source solid");
            window.findChild<QAction*>("undoAction")->trigger();flush();
        }
        app::apply_application_translations(application,settings_before);
        if(!rip_id.empty())for(int mode=0;mode<3;++mode) {
            const bool commit=mode!=0,failed=mode==2;
            action->trigger();flush();pick();
            const auto before=window.execute_console_command("documents").data;
            auto* current=dialog();auto* rips=current->findChild<QTableWidget*>("sheetRipSketches");
            // Empty entry opens the ordinary owned Sketch properties. Cancelling
            // that first editor must not leave a bogus empty RIP reference.
            rips->cellClicked(0,1);flush();
            auto* owned=window.findChild<QDialog*>("generalSurfaceSketchProperties");
            check(owned&&owned->isVisible(),"RIP field did not create its Sketch editor");
            auto* owned_properties=dynamic_cast<app::PrimitivePropertiesDialog*>(owned);
            check(owned_properties&&owned_properties->pending_value().placement.references.empty(),
                "New RIP inherited the conversion source face instead of offering an empty support");
            owned->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            check(current->rip_sketches().empty(),"Cancelled new RIP left an empty reference");
            rips->cellClicked(0,1);flush();owned=window.findChild<QDialog*>("generalSurfaceSketchProperties");
            check(owned&&owned->isVisible(),"RIP editor did not reopen");
            owned_properties=dynamic_cast<app::PrimitivePropertiesDialog*>(owned);
            check(owned_properties&&owned_properties->pending_value().placement.references.empty(),
                "Recreated RIP inherited a source plane");
            document::ConstructionReference rip_support{{},seed.owner_id,seed.semantic_key,0.,true};
            check(owned_properties->set_reference(0,rip_support,QStringLiteral("RIP support")),
                "Explicit RIP support was rejected");
            owned_properties->refresh_sketch_preview();flush();
            const auto entered_rip_references=owned_properties->pending_value().placement.references;
            check(!entered_rip_references.empty()&&std::ranges::all_of(entered_rip_references,[&](const auto& ref){
                    return (ref.owner_id.empty()&&ref.semantic_key.empty())||
                        (ref.owner_id==seed.owner_id&&ref.semantic_key==seed.semantic_key);}),
                "Explicit RIP support changed before drawing");
            const auto entered_rip_placement=owned_properties->pending_value().placement;
            // Leave the next placement row armed, as automatic advancement does
            // after picking only one plane, then enter the embedded Sketcher.
            auto* placement_rows=owned->findChild<QTableWidget*>("primitiveReferenceTable");
            check(placement_rows,"Owned RIP placement table missing");
            placement_rows->cellClicked(1,1);flush();
            auto* open=owned->findChild<QPushButton*>("featureSketchButton");
            check(open&&open->isEnabled(),"RIP Sketch drawing is unavailable");open->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            const auto* drawing=window.active_sketch_for_test();
            check(drawing&&!view->mesh().triangles.empty(),"RIP Sketcher lost source material context");
            const auto frame=*drawing;
            if(failed){view->fit_points({frame.world_point(-60,-45),frame.world_point(110,45)});flush();}
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"RIP camera ray missing");
                const auto q=frame.intersect_ray(ray->first,ray->second);check(q.has_value(),"RIP camera does not face its Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](kernel::Vec3 world){const auto p=frame.local_point(world);
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);};
            const auto click=[&](QPointF p){for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();}};
            window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();
            click(screen({failed?70.:-50.,0,1.5}));click(screen({failed?90.:50.,0,1.5}));
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(window.active_sketch_for_test()->segments.size()==1,"RIP mouse drawing did not create a native segment");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            check(owned->isVisible(),"Finishing RIP Sketch did not return to its Properties");
            check(owned_properties->pending_value().placement.references==entered_rip_references,
                "Finishing RIP drawing changed the explicit support");
            const auto drawn_rip_placement=owned_properties->pending_value().placement;
            const auto rip_plane_owner=owned_properties->pending_value().id;
            check(drawn_rip_placement.x==entered_rip_placement.x&&drawn_rip_placement.y==entered_rip_placement.y&&
                    drawn_rip_placement.z==entered_rip_placement.z&&drawn_rip_placement.rotation_x==entered_rip_placement.rotation_x&&
                    drawn_rip_placement.rotation_y==entered_rip_placement.rotation_y&&drawn_rip_placement.rotation_z==entered_rip_placement.rotation_z,
                "Drawing RIP geometry moved its owning Sketch frame");
            owned->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(current->isVisible()&&current->rip_sketches().size()==1,"Owned RIP confirmation did not restore conversion");
            const auto owned_rip=current->rip_sketches().front();
            rips->cellClicked(0,1);flush();
            owned=window.findChild<QDialog*>("generalSurfaceSketchProperties");
            owned_properties=dynamic_cast<app::PrimitivePropertiesDialog*>(owned);
            check(owned_properties&&owned_properties->pending_value().placement.references==entered_rip_references,
                "Editing an existing RIP discarded its explicitly selected support");
            owned->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==before,"Draft RIP modified the live document");
            current->buttons()->button(commit?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
            if(failed) {
                check(current->isVisible()&&window.execute_console_command("documents").data==before,
                    "First failed RIP OK committed or closed the dialog");
                current->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            }
            check(!window.findChild<QDialog*>("sheetFromBodyDialog"),"RIP confirmation did not close the dialog");
            check(std::ranges::none_of(view->mesh().edges,[&](const auto& edge) {
                return edge.reference.owner_id==rip_plane_owner;
            }),"The owned RIP placement plane remained after conversion finished");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);
            if(commit) {
                const auto conversion=std::ranges::find_if(saved.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetFromBody;});
                check(conversion!=saved.history.end()&&conversion->sheet_from_body.rip_sketches==std::vector<std::string>{owned_rip},"Owned RIP was not persisted");
                check(conversion->sheet_from_body.generated_features.size()==(failed?0:2),"Local RIP did not retain its expected regions");
                if(failed) {
                    std::vector<kernel::BodyResult> cached;static_cast<void>(document::PartDocument::load(path,&cached));
                    check(!cached.empty()&&cached.back().calculation_errors.contains(conversion->id)&&!view->mesh().triangles.empty(),
                        "Confirmed failed RIP lost its diagnostic or source material");
                }
                const auto sketch=std::ranges::find(saved.sketches,owned_rip,&sketcher::Sketch::id);
                check(sketch!=saved.sketches.end()&&sketch->segments.size()==1,"Owned RIP native geometry missing");
                auto* tree=window.findChild<QTreeWidget*>("documentTree");bool nested=false;
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==sketch->owner_container_id&&(*it)->parent())
                        nested|=(*it)->parent()->data(0,Qt::UserRole).toString().toStdString()==conversion->id;
                check(nested,"Owned RIP is not nested under Sheet from Body");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                check(document::PartDocument::load(path).history.size()==original,"Owned RIP Undo changed source history");
                window.findChild<QAction*>("redoAction")->trigger();flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                check(document::PartDocument::load(path).serialized()==saved.serialized(),"Owned RIP Redo changed its definition");
                window.findChild<QAction*>("undoAction")->trigger();flush();
            } else check(saved.history.size()==original,"Owned RIP Cancel changed history");
        }
        action->trigger();flush();pick();dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).history.size()==original,"Conversion Cancel modified history");
        action->trigger();flush();pick();window.grab().save(QString::fromStdString((directory/"sheet-conversion-properties.png").string()));
        QMouseEvent middle(QEvent::MouseButtonDblClick,QPointF(20,20),QPointF(20,20),QPointF(20,20),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        const auto confirmation_started=std::chrono::steady_clock::now();
        QApplication::sendEvent(view,&middle);flush();
        std::cout<<"sheet_conversion_gui_confirmation_seconds="<<
            std::chrono::duration<double>(std::chrono::steady_clock::now()-confirmation_started).count()<<std::endl;
        if(window.findChild<QDialog*>("sheetFromBodyDialog"))window.grab().save(QString::fromStdString((directory/"sheet-conversion-failure.png").string()));
        check(!window.findChild<QDialog*>("sheetFromBodyDialog"),"Conversion OK failed");
        check(!view->mesh().triangles.empty(),
            "Sheet from Body history boundary hid the complete model View");
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto saved=save();check(saved.history.size()>original,"Conversion created no native sheet features");
        check_source_references("material conversion OK");
        const auto conversion=std::find_if(saved.history.begin()+original,saved.history.end(),[](const auto& feature){return feature.feature_kind==document::FeatureKind::SheetFromBody;});
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
            std::set<std::string> source_owners;
            for(const auto& binding:
                    conversion->sheet_from_body.child_bindings)
                for(const auto& face:binding.faces)
                    if(face.reference.valid())
                        source_owners.insert(face.reference.owner_id);
            bool source_offered=false;
            for(int y=10;y<view->height()&&!source_offered;y+=20)
                for(int x=10;x<view->width()&&!source_offered;x+=20)
                    source_offered=std::ranges::any_of(
                        view->selection_candidates_at(QPointF(x,y)),
                        [&](const auto& candidate) {
                            return candidate.kind==
                                    viewer::CandidateKind::Container&&
                                source_owners.contains(candidate.owner_id);
                        });
            check(source_offered,
                "Converted sheet hover cannot cycle to an associated source feature");
            const auto source_owner=*source_owners.begin();
            const auto* source_feature=saved.find_container(source_owner);
            check(source_feature,
                "Associated source feature is absent from the document");
            const auto source_sketch_id=
                test::rectangular_sketch_id(*source_feature);
            const auto source_sketch=std::ranges::find(saved.sketches,
                source_sketch_id,&sketcher::Sketch::id);
            check(source_sketch!=saved.sketches.end(),
                "Associated source feature has no editable Sketch");
            const auto source_dimension=std::ranges::find_if(
                source_sketch->dimensions,[](const auto& dimension) {
                    return dimension.driving&&!dimension.suppressed;
                });
            check(source_dimension!=source_sketch->dimensions.end(),
                "Associated source Sketch has no driving dimension");
            QTreeWidgetItem* source_row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==
                        source_owner&&
                    (*it)->data(0,Qt::UserRole+3).toString()==
                        QStringLiteral("part-container")){
                    source_row=*it;
                    break;
                }
            check(source_row,
                "Associated source feature cannot be reached in the Tree");
            window.show_tree_item_properties(source_row);flush();
            app::PrimitivePropertiesDialog* source_properties=nullptr;
            for(auto* candidate:window.findChildren<QDialog*>())
                if(candidate->isVisible())
                    if(auto* primitive=dynamic_cast<
                            app::PrimitivePropertiesDialog*>(candidate)){
                        source_properties=primitive;
                        break;
                    }
            check(source_properties&&source_properties->isVisible(),
                "Associated source feature Properties did not open");
            const auto semantic="dimension:"+source_dimension->id;
            const auto shown=std::ranges::find_if(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==source_sketch_id&&
                        dimension.reference.semantic_key==semantic;
                });
            check(shown!=view->mesh().dimensions.end(),
                "Source Sketch dimension is absent while Properties is open");
            viewer::ViewerCandidate dimension_candidate;
            dimension_candidate.kind=viewer::CandidateKind::Dimension;
            dimension_candidate.owner_id=source_sketch_id;
            dimension_candidate.semantic_key=semantic;
            dimension_candidate.geometry_index=static_cast<std::size_t>(
                shown-view->mesh().dimensions.begin());
            window.edit_dimension_inline(dimension_candidate);flush();
            auto* inline_edit=view->findChild<QLineEdit*>(
                "inlineDimensionValueEdit");
            check(inline_edit&&inline_edit->isVisible(),
                "Source Sketch dimension cannot be edited from open Properties");
            const double edited_value=source_dimension->value+1;
            inline_edit->setText(QString::number(edited_value,'g',17));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(inline_edit,&enter);flush();
            auto* source_buttons=source_properties->findChild<
                QDialogButtonBox*>();
            check(source_buttons&&source_buttons->button(
                    QDialogButtonBox::Ok)->isEnabled(),
                "Source feature cannot confirm its edited Sketch dimension");
            source_buttons->button(QDialogButtonBox::Ok)->click();flush();
            const auto source_edited=save();
            const auto edited_sketch=std::ranges::find(
                source_edited.sketches,source_sketch_id,
                &sketcher::Sketch::id);
            const auto edited_dimension=edited_sketch==
                    source_edited.sketches.end()?nullptr:
                [&]() -> const sketcher::SketchDimension* {
                    const auto value=std::ranges::find(
                        edited_sketch->dimensions,source_dimension->id,
                        &sketcher::SketchDimension::id);
                    return value==edited_sketch->dimensions.end()?nullptr:
                        &*value;
                }();
            const auto* edited_parent=source_edited.find_container(
                conversion->id);
            check(edited_dimension&&
                    std::abs(edited_dimension->value-edited_value)<1e-7&&
                    edited_parent&&edited_parent->sheet_from_body.
                        generated_features==conversion->sheet_from_body.
                            generated_features,
                "Source dimension edit did not preserve the associated sheet");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            const auto source_restored=save();
            const auto restored_sketch=std::ranges::find(
                source_restored.sketches,source_sketch_id,
                &sketcher::Sketch::id);
            const auto restored_dimension=restored_sketch==
                    source_restored.sketches.end()?nullptr:
                [&]() -> const sketcher::SketchDimension* {
                    const auto value=std::ranges::find(
                        restored_sketch->dimensions,source_dimension->id,
                        &sketcher::SketchDimension::id);
                    return value==restored_sketch->dimensions.end()?nullptr:
                        &*value;
                }();
            check(restored_dimension&&std::abs(restored_dimension->value-
                    source_dimension->value)<1e-7,
                "Undo did not restore the source Sketch dimension");
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
        const auto tree_action_enabled=[&](const std::string& id,
                const char* object_name) {
            auto* item=tree_row(id);
            check(item,"Sheet from Body context Tree row is missing");
            tree->setCurrentItem(item);tree->scrollToItem(item);flush();
            bool available{};
            const auto name=QString::fromLatin1(object_name);
            QTimer::singleShot(0,&window,[&,name] {
                auto* menu=qobject_cast<QMenu*>(
                    QApplication::activePopupWidget());
                if(!menu)return;
                if(auto* command=menu->findChild<QAction*>(name))
                    available=command->isEnabled();
                menu->close();
            });
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",
                Qt::DirectConnection,
                Q_ARG(QPoint,tree->visualItemRect(item).center()));
            flush();
            return available;
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
                std::ranges::all_of(partial_parent->sheet_from_body.
                    child_bindings,[&](const auto& binding) {
                        const auto* row=tree_row(binding.feature_id);
                        return binding.detached&&row&&row->parent()==
                            tree_row(target_id);
                    }),
            "Partial explode did not detach its complete generated Tree branch");
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

        std::vector<kernel::BodyResult> before_release;
        static_cast<void>(document::PartDocument::load(path,&before_release));
        check(!before_release.empty(),"Conversion release has no calculated input");
        kernel.ensure_body_properties(before_release.back());
        run_tree_action(conversion->id,
            "explodeSheetFromBodyAllAction");
        const auto fully_exploded=save();
        std::vector<kernel::BodyResult> after_release;
        static_cast<void>(document::PartDocument::load(path,&after_release));
        check(!after_release.empty(),"Conversion release lost its calculated material");
        kernel.ensure_body_properties(after_release.back());
        check(std::abs(after_release.back().volume-before_release.back().volume)<1e-5,
            "Complete explode changed sheet material volume");
        const auto fully_exploded_parent=std::ranges::find_if(
            fully_exploded.history,
            [&](const auto& feature){return feature.id==conversion->id;});
        check(fully_exploded_parent!=fully_exploded.history.end()&&
                std::ranges::all_of(fully_exploded_parent->sheet_from_body.
                    child_bindings,[](const auto& binding) {
                        return binding.detached;
                    })&&tree_row(conversion->id)&&
                tree_row(conversion->id)->childCount()==0&&
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
                    [](const auto& feature){
                        const bool sketch=feature.feature_kind==document::FeatureKind::Sketch||
                            (feature.feature_kind==document::FeatureKind::Feature&&feature.feature.type==document::FeatureType::Sketch);
                        return sketch?!feature.suppressed:feature.suppressed;
                    }),
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
            check(row,"Generated feature is absent from the Tree");
            check(!tree_action_enabled(feature->id,
                        "historyObjectProperties")&&
                    !tree_action_enabled(feature->id,
                        "suppressHistoryObject")&&
                    !tree_action_enabled(feature->id,
                        "deleteHistoryObject")&&
                    tree_action_enabled(feature->id,
                        "editHistoryObject")==
                        (kind==document::FeatureKind::Bend),
                "Bound Sheet from Body child exposes invalid Tree actions");
            window.show_tree_item_properties(row);flush();
            auto* properties=window.findChild<QDialog*>(kind==document::FeatureKind::Flat?"flatPropertiesDialog":"bendPropertiesDialog");
            check(!properties||!properties->isVisible(),
                "Bound Sheet from Body child opened independent Properties");
            check(save().serialized()==unchanged,
                "Blocked generated feature Properties changed the document");
        }
        const auto corrected_flat=std::find_if(saved.history.begin()+original,saved.history.end(),[&](const auto& feature) {
            return feature.feature_kind==document::FeatureKind::Flat&&feature.flat.sheet_attachment&&
                saved.body_owner_for_object(feature.id)->scope.id==target_id&&saved.sheet_from_body_child_bound(feature.id);
        });
        check(corrected_flat!=saved.history.end()||!qEnvironmentVariableIsSet("ZIMA_VERIFY_CONVERTED_FLAT_CORRECTIONS"),
            "Converted Flat correction GUI fixture missing");
        if(corrected_flat!=saved.history.end()) {
        const auto correction_owner=corrected_flat->id,correction_sketch=corrected_flat->flat.sketch_id;
        const auto& correction_profile=*std::ranges::find(saved.sketches,correction_sketch,&sketcher::Sketch::id);
        view->set_view_direction(correction_profile.resolved_normal);view->fit_all();flush();
        // Resolve the mouse target after the camera transition, as for Bend
        // inspection below. A changing projection invalidates its pixel label.
        QEventLoop flat_camera;QTimer::singleShot(950,&flat_camera,&QEventLoop::quit);flat_camera.exec();
        const auto unchanged_corrections=save().serialized();
        for(const auto suffix:{":first",":last"})for(double value:{.5,-.25}) {
            const auto& original_dimension=*std::ranges::find(correction_profile.dimensions,correction_sketch+":flat-correction"+suffix,&sketcher::SketchDimension::id);
            const auto expected_native=sketcher::dimension_value_from_input(original_dimension,value,correction_profile.plane);
            std::cout<<"Flat GUI correction "<<suffix<<" input "<<value<<std::endl;
            auto* row=tree_row(correction_owner);check(row,"Corrected Flat Tree row missing");
            const auto inspection_before=window.execute_console_command("documents").data;
            tree->setCurrentItem(row);tree->itemClicked(row,0);flush();
            check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==correction_sketch;}),
                "Single Flat selection exposed its corrections");
            row=tree_row(correction_owner);tree->itemDoubleClicked(row,0);flush();
            check(window.execute_console_command("documents").data==inspection_before,
                "Inspecting Flat corrections changed the document");
            const auto key="dimension:"+correction_sketch+":flat-correction"+suffix;
            const auto shown=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==correction_sketch&&d.reference.semantic_key==key;});
            check(shown!=view->mesh().dimensions.end(),"Zero Flat correction is not inspectable");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
            candidate.owner_id=correction_sketch;candidate.semantic_key=key;
            candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
            const auto pixel=view->candidate_dimension_label_position(candidate);
            check(pixel.has_value(),"Flat correction label has no screen position");
            for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                QMouseEvent mouse(type,*pixel,QPointF(view->mapToGlobal(*pixel)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&mouse);flush();
            }
            auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(editor&&editor->isVisible(),"Flat correction double-click did not open its value editor");
            check(editor->property("dimensionIdentifier").toString().toStdString()==saved.dimension_identifiers.identifier(correction_sketch,key),
                "Flat correction mouse click selected another dimension");
            editor->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(editor,&enter);flush();
            window.grab().save(QString::fromStdString((directory/"flat-correction-after-edit.png").string()));
            const auto changed=save();const auto& profile=*std::ranges::find(changed.sketches,correction_sketch,&sketcher::Sketch::id);
            const auto dimension=std::ranges::find(profile.dimensions,correction_sketch+":flat-correction"+suffix,&sketcher::SketchDimension::id);
            check(dimension!=profile.dimensions.end()&&std::abs(dimension->value-expected_native)<1e-7,
                "Flat correction value was not persisted");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(save().serialized()==unchanged_corrections,"Flat correction GUI Undo changed its source");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(save().serialized()==changed.serialized(),"Flat correction GUI Redo changed identity");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            window.finish_parameter_dimensions();flush();
        }
        }
        const auto corrected_bend=std::find_if(saved.history.begin()+original,saved.history.end(),[&](const auto& feature) {
            return feature.feature_kind==document::FeatureKind::Bend&&
                saved.body_owner_for_object(feature.id)->scope.id==target_id&&saved.sheet_from_body_child_bound(feature.id);
        });
        if(corrected_bend!=saved.history.end()) {
            const auto owner=corrected_bend->id,sketch_id=corrected_bend->bend.sketch_id;
            const auto& initial=*std::ranges::find(saved.sketches,sketch_id,&sketcher::Sketch::id);
            view->set_view_direction(initial.resolved_normal);view->fit_all();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            const auto baseline=save().serialized();
            for(const auto suffix:{":first",":last"})for(double value:{.5,-.25,0.}) {
                const auto id=sketch_id+":position"+suffix,key="dimension:"+id;
                const auto& original_dimension=*std::ranges::find(initial.dimensions,id,&sketcher::SketchDimension::id);
                const auto expected=sketcher::dimension_value_from_input(original_dimension,value,initial.plane);
                auto* row=tree_row(owner);check(row,"Converted Bend Tree row missing");
                const auto inspection=window.execute_console_command("documents").data;
                tree->setCurrentItem(row);tree->itemClicked(row,0);flush();
                check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch_id;}),
                    "Single Bend selection exposed first Sketch corrections");
                row=tree_row(owner);tree->itemDoubleClicked(row,0);flush();
                check(window.execute_console_command("documents").data==inspection,"Bend inspection modified the document");
                const auto shown=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch_id&&d.reference.semantic_key==key;});
                check(shown!=view->mesh().dimensions.end(),"Converted first Sketch zero correction is hidden");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
                candidate.owner_id=sketch_id;candidate.semantic_key=key;
                candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto pixel=view->candidate_dimension_label_position(candidate);
                check(pixel.has_value(),"First Sketch correction has no screen label");
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    QMouseEvent mouse(type,*pixel,QPointF(view->mapToGlobal(*pixel)),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                        type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&mouse);flush();
                }
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                check(editor&&editor->isVisible(),"First Sketch correction double-click did not open the editor");
                check(editor->property("dimensionIdentifier").toString().toStdString()==saved.dimension_identifiers.identifier(sketch_id,key),
                    "First Sketch correction mouse click picked another dimension");
                editor->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                QApplication::sendEvent(editor,&enter);flush();
                const auto changed=save();
                const auto& profile=*std::ranges::find(changed.sketches,sketch_id,&sketcher::Sketch::id);
                const auto d=std::ranges::find(profile.dimensions,id,&sketcher::SketchDimension::id);
                const auto support=std::ranges::find(profile.external_references,d->first_point_id,&sketcher::SketchExternalReference::id);
                check(support!=profile.external_references.end()&&support->cached_points.size()==1,
                    "Corrected first Sketch lost its exact endpoint");
                check(std::abs(d->value-expected)<1e-7&&
                    std::abs(profile.find_point(d->second_point_id)->x-support->cached_points.front()[0]-expected)<1e-7,
                    "First Sketch GUI correction violates the endpoint equation");
                window.grab().save(QString::fromStdWString((directory/"bend-first-correction-after-edit.png").wstring()));
                if(changed.serialized()!=baseline) {
                    window.findChild<QAction*>("undoAction")->trigger();flush();
                    check(save().serialized()==baseline,"First Sketch Undo changed its source");
                    window.findChild<QAction*>("redoAction")->trigger();flush();
                    check(save().serialized()==changed.serialized(),"First Sketch Redo changed identity");
                    window.findChild<QAction*>("undoAction")->trigger();flush();
                }
                window.finish_parameter_dimensions();flush();
            }
            std::cout<<"Converted box first Sketch actual mouse correction, equations and Undo/Redo passed\n";
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
            check(!tree_action_enabled(generated.front(),
                        "deleteHistoryObject")&&
                    !tree_action_enabled(generated.front(),
                        "suppressHistoryObject"),
                "Bound Sheet from Body child can be deleted or suppressed");
            remove(conversion->id);
            for (const auto& id : generated)
                check(!row(id), "Deleting Sheet from Body retained a bound generated child");
            check(!view->mesh().triangles.empty(), "Deleting Sheet from Body did not restore the original Body");
            if(!rip_id.empty())for(int repeat=0;repeat<2;++repeat) {
                QTreeWidgetItem* sketch_row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==rip_id&&
                            (*it)->data(0,Qt::UserRole+3)=="part-sketch") {sketch_row=*it;break;}
                check(sketch_row,"Released RIP Sketch Tree row is missing");
                const auto unchanged_document=window.execute_console_command("documents").data;
                window.show_tree_item_properties(sketch_row);flush();
                if(!window.active_sketch_for_test()) {
                    auto* open=window.findChild<QPushButton*>("featureSketchButton");
                    if(!open||!open->isVisible())open=window.findChild<QPushButton*>("sketchOpenButton");
                    if(open&&open->isVisible()) {open->click();flush();}
                }
                const auto* active=window.active_sketch_for_test();
                if(!active||active->id!=rip_id) {
                    std::string dialogs;
                    for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())dialogs+=" "+dialog->objectName().toStdString();
                    throw std::runtime_error("RIP Sketch cannot enter Sketcher after deleting conversion; repeat="+
                        std::to_string(repeat)+"; dialogs="+dialogs);
                }
                check(!view->mesh().triangles.empty(),"RIP Sketch lost the preceding model after deleting conversion");
                check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){
                    return edge.reference.owner_id==rip_id||edge.reference.owner_id==rip_owner;
                }),"RIP Sketch geometry disappeared after deleting conversion");
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                check(!window.active_sketch_for_test(),"RIP Sketch cannot finish after deleting conversion");
                for(auto* properties:window.findChildren<QDialog*>())if(properties->isVisible())
                    if(auto* buttons=properties->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Cancel))
                        buttons->button(QDialogButtonBox::Cancel)->click();
                flush();
                check(window.execute_console_command("documents").data==unchanged_document,
                    "Unchanged RIP Sketch entry or Cancel changed the document");
            }
        }
        window.grab().save(QString::fromStdString((directory/"sheet-conversion-result.png").string()));
        std::cout<<"Sheet exchange GUI: native picker, all languages, Cancel, middle-button OK, Undo/Redo and DXF passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

int verify_surface_shell_source_ui(QApplication& application,
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
        const auto source_body=part.body_history.create_body("Surface source");
        auto source=test::rectangular_feature(part,{30,20,10});
        source.name="Surface source";
        part.insert_history_entry(document::PartHistoryKind::Feature,
            source.id);
        part.history.push_back(source);
        part.resolve_constructions();
        kernel::OcctKernel kernel;
        auto calculated=workspace::calculate_part_with_resolved_references(
            kernel,part);
        const auto destination=part.body_history.create_body("Surface result");
        {
            auto body=*part.body_history.find(destination);
            body.scope.placement.x=-37.;
            body.scope.placement.y=19.;
            body.scope.placement.z=6.;
            body.scope.placement.rotation_x=-13.;
            body.scope.placement.rotation_z=23.;
            body.scope.placement.absolute_rotation_x=-13.;
            body.scope.placement.absolute_rotation_z=23.;
            part.body_history.update_body(body);
        }
        part.body_history.activate(destination);
        const auto path=directory/"surface-shell-source-ui.prtz";
        part.save(path,calculated);

        app::AssemblyWorkspaceWindow window(
            QString::fromStdString(directory.string()));
        window.resize(1300,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),
            "Surface extraction fixture did not open");
        flush();
        check(activate_test_body(application,window,destination),
            "Surface extraction destination Body could not activate");
        const auto created=window.execute_console_command(
            QString::fromStdString(commands::Json{
                {"command","surface_shell.create"},
                {"arguments",{{"source_body",source_body}}}}.dump()));
        check(created.ok,"Surface extraction could not be created");
        flush();
        auto* view=dynamic_cast<viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(view&&!view->mesh().triangles.empty(),
            "Surface extraction produced no visible result");
        bool source_offered=false;
        for(int step:{30,5})
            for(int y=5;y<view->height()&&!source_offered;y+=step)
                for(int x=5;x<view->width()&&!source_offered;x+=step)
                    source_offered=std::ranges::any_of(
                        view->selection_candidates_at(QPointF(x,y)),
                        [&](const auto& candidate) {
                            return candidate.kind==
                                    viewer::CandidateKind::Container&&
                                candidate.owner_id==source.id;
                        });
        check(source_offered,
            "Extracted surface hover cannot reach its original solid feature");
        const auto extracted=created.data.at("container").get<std::string>();
        const auto available=window.execute_console_command(
            QString::fromStdString(commands::Json{
                {"command","edge_treatment.edges"},
                {"arguments",commands::Json::object()}}.dump()));
        check(available.ok,"Extracted surface edges cannot be queried");
        const auto& edge_items=available.data.at("items");
        const auto edge=std::ranges::find_if(edge_items,[&](const auto& item) {
            return item.at("owner").template get<std::string>()==extracted&&
                !item.value("ambiguous",false);
        });
        check(edge!=edge_items.end(),
            "Extracted surface offers no stable edge for a Fillet");
        const commands::Json routes=commands::Json::array({
            {{"edges",commands::Json::array({
                {{"owner",edge->at("owner")},{"key",edge->at("key")}}
            })}}
        });
        const auto fillet_created=window.execute_console_command(
            QString::fromStdString(commands::Json{
                {"command","fillet.create"},
                {"arguments",{{"routes",routes},{"radius_mm",1.}}}}.dump()));
        check(fillet_created.ok,"Extracted surface Fillet could not be created");
        const auto fillet=fillet_created.data.at("container").get<std::string>();
        flush();
        std::optional<QPointF> fillet_pick;
        std::size_t fillet_candidate{};
        for(int step:{24,5})
            for(int y=5;y<view->height()&&!fillet_pick;y+=step)
                for(int x=5;x<view->width()&&!fillet_pick;x+=step) {
                    const auto candidates=view->selection_candidates_at(
                        QPointF(x,y));
                    for(std::size_t index=0;index<candidates.size();++index)
                        if(candidates[index].kind==
                                viewer::CandidateKind::Container&&
                            candidates[index].owner_id==fillet) {
                            fillet_pick=QPointF(x,y);
                            fillet_candidate=index;
                            break;
                        }
                }
        check(fillet_pick.has_value(),
            "Rounded extracted surface does not offer its Fillet container");
        const auto send=[&](QEvent::Type type,Qt::MouseButton button,
                Qt::MouseButtons buttons) {
            QMouseEvent event(type,*fillet_pick,
                QPointF(view->mapToGlobal(fillet_pick->toPoint())),button,
                buttons,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        send(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        for(std::size_t index=0;index<fillet_candidate;++index) {
            send(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);
            send(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
        }
        static_cast<void>(view->grabFramebuffer());
        check(view->offered_candidate()&&
                view->offered_candidate()->owner_id==fillet&&
                view->property("gpuSourceWireVertexCount").toULongLong()>0,
            "Rounded extracted surface hover omitted its authored Fillet wire");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> reopened_cache;
        const auto reopened=document::PartDocument::load(path,&reopened_cache);
        check(!reopened_cache.empty()&&
                reopened_cache.back().body_outputs.contains(source_body)&&
                reopened_cache.back().body_outputs.contains(destination)&&
                std::ranges::find(
                    reopened_cache.back().kernel_compound_bodies,source_body)!=
                    reopened_cache.back().kernel_compound_bodies.end()&&
                std::ranges::find(
                    reopened_cache.back().kernel_compound_bodies,destination)!=
                    reopened_cache.back().kernel_compound_bodies.end(),
            "Surface extraction discarded or changed its independent source Body");
        const document::DocumentSession session(reopened,reopened_cache);
        check(!app::authored_feature_wire(session,source.id).empty(),
            "Original solid has no authored wire for hover inspection");
        const auto fillet_wire=app::authored_feature_wire(session,fillet);
        check(fillet_wire.size()>=4,
            "Saved extracted-surface Fillet retained only its route instead of the complete authored preview wire");
        zima::kernel::Vec3 low{1e100,1e100,1e100};
        zima::kernel::Vec3 high{-1e100,-1e100,-1e100};
        for(std::size_t triangle=0;
                triangle<view->mesh().triangle_references.size();++triangle) {
            const auto& reference=view->mesh().triangle_references[triangle];
            if(reference.owner_id!=fillet&&
                    reference.display_owner_id!=fillet)continue;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto& point=view->mesh().vertices[
                    view->mesh().triangles[3*triangle+corner]];
                low={std::min(low.x,point.x),std::min(low.y,point.y),
                    std::min(low.z,point.z)};
                high={std::max(high.x,point.x),std::max(high.y,point.y),
                    std::max(high.z,point.z)};
            }
        }
        check(low.x<high.x&&low.y<high.y&&low.z<high.z,
            "Rounded extracted surface has no owned display bounds");
        for(const auto& wire:fillet_wire)
            for(const auto& point:wire.points)
                check(point.x>=low.x-1e-6&&point.x<=high.x+1e-6&&
                    point.y>=low.y-1e-6&&point.y<=high.y+1e-6&&
                    point.z>=low.z-1e-6&&point.z<=high.z+1e-6,
                    "Extracted-surface Fillet wire is displaced from its result");
        std::cout<<"Surface extraction GUI: source-solid hover, transformed Fillet hover wire and independent physical result passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
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
        const auto line_bend_icon=action->icon().pixmap(48,48).toImage();
        const auto ordinary_bend_icon=window.findChild<QAction*>(
            "bendAction")->icon().pixmap(48,48).toImage();
        std::size_t cyan_pixels{};
        for(int y=0;y<line_bend_icon.height();++y)
            for(int x=0;x<line_bend_icon.width();++x) {
                const auto color=line_bend_icon.pixelColor(x,y);
                if(color.alpha()>100&&color.blue()>180&&
                        color.green()>120&&color.red()<80)
                    ++cyan_pixels;
            }
        check(cyan_pixels>30&&line_bend_icon!=ordinary_bend_icon,
            "Line Bend does not use its azure V-and-sheet icon");
        action->trigger();flush();
        auto* dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("lineBendPropertiesDialog"));
        check(dialog&&dialog->isVisible()&&dialog->windowType()==Qt::SubWindow,
            "Line Bend Properties did not open as an internal window");
        auto* plane_offset=dialog->findChild<QDoubleSpinBox*>(
            "sketchPlaneOffset");
        check(plane_offset&&!plane_offset->isVisible()&&
                std::abs(plane_offset->value())<1e-12,
            "Line Bend exposes or retains a redundant Sketch plane offset");
        auto* sketch_button=dialog->findChild<QPushButton*>(
            "sketchOpenButton");
        auto* plane_selector=dialog->findChild<QWidget*>(
            "sketchPlaneSelector");
        auto* plane_view=dialog->findChild<QTableWidget*>(
            "sketchPlaneView");
        const auto mapped=[&](const QWidget* widget) {
            return QRect(widget->mapTo(dialog,QPoint{}),widget->size());
        };
        check(sketch_button&&plane_selector&&plane_view&&
                sketch_button->parentWidget()==plane_selector&&
                mapped(plane_view).right()<mapped(sketch_button).left(),
            "Line Bend Sketch button is not after the View work-plane field");
        const auto draft=dialog->pending_value().first;
        check(dialog->mutate_sketch(draft.id,[](auto& sketch) {
            static_cast<void>(sketch.add_segment(-50,0,50,0));
        }),"Line Bend draft could not accept its bend line");
        auto* view=dynamic_cast<viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>());
        check(view&&!view->transient_edges().empty(),
            "Line Bend did not preview its completed bend line");
        sketch_button->click();flush();
        auto* finish_sketch=window.findChild<QAction*>("finishSketchAction");
        check(finish_sketch&&finish_sketch->isEnabled(),
            "Line Bend could not enter its owned Sketch");
        finish_sketch->trigger();flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("lineBendPropertiesDialog"));
        check(dialog&&dialog->isVisible()&&!view->transient_edges().empty(),
            "Line Bend lost its cyan preview after returning from Sketcher");
        check(std::ranges::any_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==
                            draft.owner_container_id&&
                        dimension.reference.semantic_key==
                            "parameter:angle"&&
                        dimension.kind==
                            kernel::ViewerDimensionKind::Angular;
                }),"Line Bend preview does not show its bend angle");
        auto* radius=dialog->findChild<QDoubleSpinBox*>("lineBendRadius");
        auto* angle=dialog->findChild<QDoubleSpinBox*>("lineBendAngle");
        auto* fixed=dialog->findChild<QComboBox*>("lineBendFixedSide");
        check(radius&&angle&&fixed&&
                !dialog->findChild<QCheckBox*>("lineBendReverse"),
            "Line Bend parameter controls are incomplete");
        angle->setValue(0);check(std::abs(angle->value())<1e-12,
            "Line Bend does not accept the zero-degree state");
        radius->setValue(6);angle->setValue(75);
        fixed->setCurrentIndex(fixed->findData(-1));
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
                bend->line_bend.fixed_side==-1&&
                !bend->line_bend.segment_id.empty(),
            "Line Bend parameters did not persist");
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
