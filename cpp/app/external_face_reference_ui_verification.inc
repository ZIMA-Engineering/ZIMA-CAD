static int verify_plane_and_generator_reference_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* error){if(!value)throw std::runtime_error(error);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(bool thin:{false,true})for(unsigned mode=0;mode<3;++mode) {
            auto part=document::PartDocument::create_default();auto profile=sketcher::Sketch::create_default();
            if(mode)static_cast<void>(profile.add_circle(0,0,5));else static_cast<void>(profile.add_rectangle(0,0,30,20));
            auto feature=document::PartDocument::create_feature_container(profile.id);profile.owner_container_id=feature.id;
            feature.feature.sides[0].length=10;feature.feature.result_type=thin?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
            feature.feature.thin_thickness=1;
            auto target=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();
            target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;target.plane=sketcher::SketchPlane::XZ;target.plane_auto=false;
            part.history={feature,owner};part.sketches={profile,target};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Plane and generator sources");graph.insert({document::PartHistoryKind::Feature,feature.id});
            graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
            kernel::OcctKernel engine;const auto geometry=engine.evaluate_history(part.kernel_operations());
            const auto file=directory/("plane-generator-"+std::to_string(thin)+"-"+std::to_string(mode)+".prtz");part.save(file,geometry);
            const auto baseline=document::PartDocument::load(file);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1100,850);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open plane/generator fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate plane/generator Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target.id&&(*i)->data(0,Qt::UserRole+3).toString()=="part-sketch"){row=*i;break;}
            check(row,"Plane/generator Sketch row is missing");window.show_tree_item_properties(row);flush();
            if(auto* open=window.findChild<QPushButton*>("sketchOpenButton"))open->click();flush();
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Cannot edit plane/generator Sketch");
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* action=window.findChild<QAction*>(mode==2?"sketchExternalProfileAction":"sketchExternalReferenceAction");
            check(action&&action->isEnabled(),"Plane/generator reference command is unavailable");action->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Plane/generator View is missing");
            view->set_view_direction({1,-2,3},0);
            QEventLoop orbit;QTimer::singleShot(950,&orbit,&QEventLoop::quit);orbit.exec();view->fit_all();flush();
            const auto wanted=[&](const auto& c){
                if(c.owner_id!=feature.id)return false;
                if(mode)return c.kind==viewer::CandidateKind::Edge&&sketcher::is_face_curve_key(c.semantic_key);
                return c.kind==viewer::CandidateKind::Face&&c.geometry==viewer::CandidateGeometry::Display&&
                    c.geometry_index<view->mesh().triangle_references.size()&&view->mesh().triangle_references[c.geometry_index].surface&&
                    view->mesh().triangle_references[c.geometry_index].surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                    std::abs(kernel::dimension_dot(view->mesh().triangle_references[c.geometry_index].surface->axis,part.sketches.back().normal()))<1e-8;
            };
            std::optional<QPointF> pixel;
            for(int y=15;y<view->height()-15&&!pixel;y+=3)for(int x=15;x<view->width()-15&&!pixel;x+=3)
                if(std::ranges::any_of(view->selection_candidates_at({double(x),double(y)}),wanted))pixel=QPointF(x,y);
            if(!pixel) {
                std::cerr<<"Missing plane/generator: thin="<<thin<<" mode="<<mode<<" display faces="<<view->mesh().triangle_references.size()
                    <<" original faces="<<view->mesh().original_references.triangle_references.size()<<std::endl;
                for(const auto& face:view->mesh().triangle_references) {
                    std::cerr<<"Display owner="<<face.owner_id<<" expected="<<feature.id<<" key="<<face.semantic_key<<" surface="<<bool(face.surface)<<std::endl;
                    if(face.surface)std::cerr<<"Source surface="<<int(face.surface->kind)<<" axis="<<face.surface->axis.x<<","<<face.surface->axis.y<<","<<face.surface->axis.z<<std::endl;
                }
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"plane-generator-missing.png")));
            }
            check(pixel.has_value(),"A planar face or finite generator is absent from the real hover picker");
            const auto mouse=[&](QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,*pixel,QPointF(view->mapToGlobal(pixel->toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            mouse(QEvent::MouseMove,Qt::NoButton);
            for(unsigned cycle=0;cycle<64;++cycle){const auto c=view->offered_candidate();if(c&&wanted(*c))break;mouse(QEvent::MouseButtonPress,Qt::RightButton);mouse(QEvent::MouseButtonRelease,Qt::RightButton);}
            const auto offered=view->offered_candidate();check(offered&&wanted(*offered),"Candidate cycling did not retain the plane/generator");
            mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
            const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
            const auto saved=snapshot();const auto& result=*std::ranges::find(saved.sketches,target.id,&sketcher::Sketch::id);
            check(result.external_references.size()==(mode==2?3u:1u),"Reference insertion did not create the expected single support and native endpoints");
            const auto& reference=result.external_references.front();
            check(!reference.broken&&reference.kind==sketcher::ExternalReferenceKind::Face&&reference.infinite==(mode==0),"Plane/generator support kind or finite extent is wrong");
            check(result.segments.size()==(mode==2?1u:0u)&&result.bsplines.empty()&&result.circles.empty(),"Reference entry created extraneous profile geometry");
            if(mode)check(reference.cached_paths.size()==1&&reference.cached_paths.front().size()==2,"Generator entry projected an entire face");
            else check(reference.cached_paths.empty()&&reference.cached_points.size()==2,"Planar face did not become an infinite dashed support");
            check(saved.history==baseline.history&&saved.sketches.front().serialized()==baseline.sketches.front().serialized(),"Reference entry changed its source definition");
            window.findChild<QAction*>("undoAction")->trigger();flush();const auto undone=snapshot();
            check(std::ranges::find(undone.sketches,target.id,&sketcher::Sketch::id)->external_references.empty(),"Plane/generator reference Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=snapshot();
            check(std::ranges::find(redone.sketches,target.id,&sketcher::Sketch::id)->serialized()==result.serialized(),"Plane/generator reference Redo/reopen changed its identity");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/("plane-generator-"+std::to_string(thin)+"-"+std::to_string(mode)+".png"))));
            std::cout<<"Plane/generator real mouse, native geometry, immutable source and Undo/Redo passed: thin="<<thin<<" mode="<<mode<<std::endl;
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}

int verify_external_face_reference_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool yes,const char* message){if(!yes)throw std::runtime_error(message);};
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_PLANE_GENERATOR_REFERENCE"))return verify_plane_and_generator_reference_ui(application,directory);
        const auto user=qEnvironmentVariable("ZIMA_VERIFY_EXTERNAL_FACE_SOURCE");
        for(bool thin:{false,true})for(bool end_cap:{false,true}) {
            document::PartDocument part;std::vector<kernel::BodyResult> calculated;
            std::string source_id,sketch_id,body_id,curve_id;
            if(!user.isEmpty()) {
                if(thin)break;
                part=document::PartDocument::load(std::filesystem::u8path(user.toStdString()),&calculated);
                check(part.history.size()>=2,"Reference fixture needs preceding source history");
                source_id=part.history.front().id;body_id=part.body_owner_for_object(source_id)->scope.id;
                sketch_id=part.history.back().feature.sketch_id;
                const auto& source=*std::ranges::find(part.sketches,part.history.front().feature.sketch_id,&sketcher::Sketch::id);
                curve_id=source.arcs.empty()?source.segments.front().id:source.arcs.front().id;
                const auto& target=*std::ranges::find(part.sketches,sketch_id,&sketcher::Sketch::id);
                for(const auto& reference:target.external_references)if(reference.source_owner_id==source_id)
                    for(const auto& arc:source.arcs)if(reference.source_semantic_key.find(arc.id)!=std::string::npos)
                        curve_id=arc.id;
            } else {
                part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
                static_cast<void>(source.add_rectangle(0,0,30,20));
                auto feature=document::PartDocument::create_feature_container(source.id);source.owner_container_id=feature.id;
                feature.feature.type=document::FeatureType::Modeling;feature.feature.sides[0].length=10;
                feature.feature.result_type=thin?document::ProfileResultType::Thin:document::ProfileResultType::Solid;
                feature.feature.thin_thickness=2;source_id=feature.id;curve_id=source.segments.front().id;
                auto target=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();
                target.owner_container_id=owner.id;target.plane=sketcher::SketchPlane::XZ;target.plane_auto=false;sketch_id=target.id;
                static_cast<void>(target.add_circle(10,5,3));part.history={feature,owner};part.sketches={source,target};
                document::BodyHistoryGraph graph;body_id=graph.create_body("External face verification");
                graph.insert({document::PartHistoryKind::Feature,feature.id});graph.insert({document::PartHistoryKind::Feature,owner.id});
                part.set_body_history(graph);part.resolve_constructions();kernel::OcctKernel kernel;calculated=kernel.evaluate_history(part.kernel_operations());
            }
            const std::string fixture_name=std::string(thin?"thin-":"solid-")+(end_cap?"end-cap":"start-cap");
            const auto file=directory/(fixture_name+"-reference.prtz");part.save(file,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1100,850);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open cap reference fixture");flush();
            check(activate_test_body(application,window,body_id),"Cannot activate cap reference Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch_id&&(*i)->data(0,Qt::UserRole+3).toString()=="part-sketch"){row=*i;break;}
            check(row,"Cap reference Sketch row unavailable");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");
            if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
            if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            if(open){open->click();flush();}
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Cannot enter cap reference Sketch");
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* action=window.findChild<QAction*>("sketchExternalReferenceAction");check(action&&action->isEnabled(),"External reference command unavailable");action->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Cap reference View unavailable");
            // View both rims obliquely; ordinary Reference must still offer
            // individual original edges; planar faces can additionally supply
            // their straight infinite support, never a curved whole-face outline.
            view->set_view_direction({1,-2,3},0);
            QEventLoop orbit;QTimer::singleShot(950,&orbit,&QEventLoop::quit);orbit.exec();
            view->fit_all();flush();
            std::optional<QPointF> hit;
            std::map<std::string,unsigned> offered;
            const auto is_cap=[&](const auto& candidate){
                const auto source=app::sketch_external_reference_source_candidate(candidate);
                return candidate.kind==viewer::CandidateKind::Edge&&source.owner_id==source_id&&
                    source.semantic_key.starts_with(end_cap?"end:":"start:")&&
                    source.semantic_key.find(curve_id)!=std::string::npos;};
            for(int y=15;y<view->height()-15&&!hit;y+=3)for(int x=15;x<view->width()-15&&!hit;x+=3) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                check(std::ranges::all_of(candidates,[&](const auto& candidate){return candidate.kind!=viewer::CandidateKind::Face||
                    app::sketch_external_reference_has_stable_geometry(candidate,false,false,&view->mesh());}),"Ordinary Reference offered an uncertified curved face");
                if(candidates.empty())continue;
                for(const auto& candidate:candidates)if(candidate.owner_id==source_id)++offered[std::to_string(int(candidate.kind))+":"+candidate.semantic_key.substr(0,60)];
                if(std::ranges::any_of(candidates,is_cap))hit=QPointF(x,y);
            }
            if(!hit) {
                std::cerr<<"Reference triangles="<<view->mesh().original_references.triangle_references.size()<<" display="<<view->mesh().triangle_references.size()<<" source="<<source_id<<std::endl;
                for(const auto& [key,count]:offered)std::cerr<<key<<" "<<count<<std::endl;
                window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"cap-failed.png")));
            }
            check(hit.has_value(),"Original source rim has no selectable individual edge");
            const auto mouse=[&](QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            mouse(QEvent::MouseMove,Qt::NoButton);
            for(unsigned cycle=0;cycle<32;++cycle){const auto offered=view->offered_candidate();if(offered&&is_cap(*offered))break;
                mouse(QEvent::MouseButtonPress,Qt::RightButton);mouse(QEvent::MouseButtonRelease,Qt::RightButton);}
            const auto cap=view->offered_candidate();check(cap&&is_cap(*cap),"Right-click cannot offer the individual original rim edge");
            check(cap->geometry==viewer::CandidateGeometry::OriginalReference,"Ordinary Reference did not retain the original edge source");
            mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
            const auto original_settings=app::ApplicationSettings::load(QString::fromStdString(document::path_to_utf8(directory)));
            QTemporaryDir languages;
            const auto prompt=QString::fromUtf8("Reference: vyberte hranu, obrysovou křivku, vrchol, osu nebo rovinu. Pravým tlačítkem lze přepínat kandidáty.");
            for(const auto* language:{"cs","en","de","fr","ru"}) {
                QSettings config(languages.filePath("config.ini"),QSettings::IniFormat);config.setValue("Application/Language",language);
                config.setValue("Paths/Localization",original_settings.resolved_paths.value("Localization"));config.sync();
                auto settings=app::ApplicationSettings::load(languages.path());app::apply_application_translations(application,settings);
                // Main-window actions are created in the selected language;
                // changing the translator alone does not rebuild existing actions.
                {
                    app::AssemblyWorkspaceWindow localized(languages.path());
                    const auto* translated=localized.findChild<QAction*>("sketchExternalReferenceAction");
                    check(translated&&translated->text()==settings.qt_translations.value("Reference"),"Reference command name did not follow the selected language");
                }
                for(const auto* theme:{"light","dark"}) {
                    settings.theme=theme;app::apply_application_appearance(application,settings);
                    action->trigger();flush();action->trigger();flush();
                    const auto* status=window.findChild<QLabel*>("workspaceState");
                    check(status&&!settings.qt_translations.value(prompt).isEmpty()&&status->text()==settings.qt_translations.value(prompt),"Individual reference guidance did not follow language/theme");
                }
            }
            app::apply_application_translations(application,original_settings);app::apply_application_appearance(application,original_settings);
            if(!user.isEmpty()) {
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())
                    if(auto* ok=buttons->button(QDialogButtonBox::Ok);ok&&ok->isEnabled())ok->click();
                flush();
            }
            const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
            const auto saved=snapshot();const auto& sketch=*std::ranges::find(saved.sketches,sketch_id,&sketcher::Sketch::id);
            const auto& original=*std::ranges::find(part.sketches,sketch_id,&sketcher::Sketch::id);
            check(sketch.external_references.size()==original.external_references.size()+1,"Mouse selection did not insert one edge reference");
            const auto& ref=sketch.external_references.back();check(ref.kind==sketcher::ExternalReferenceKind::Edge&&!ref.infinite&&!ref.broken,
                "Individual edge selection did not retain finite original support");
            check(ref.cached_paths.empty()||ref.cached_paths.size()==1,"Individual edge selection projected multiple face contours");
            check(std::equal(original.external_references.begin(),original.external_references.end(),sketch.external_references.begin()),
                "Adding an edge rewrote a stored face reference");
            const auto source=app::sketch_external_reference_source_candidate(*cap);
            check(ref.source_owner_id==source.owner_id&&ref.source_semantic_key==source.semantic_key,"Edge acceptance changed the exact offered source identity");
            check(saved.history==part.history,"Adding an edge reference changed feature parameters");
            window.findChild<QAction*>("undoAction")->trigger();flush();auto undone=snapshot();
            check(std::ranges::find(undone.sketches,sketch_id,&sketcher::Sketch::id)->external_references==original.external_references,"Edge reference Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();auto redone=snapshot();
            check(std::ranges::find(redone.sketches,sketch_id,&sketcher::Sketch::id)->external_references==sketch.external_references,"Edge reference Redo/reopen changed its identity");
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(fixture_name+".png"))));
            std::cout<<"Verified "<<fixture_name<<" source="<<source_id<<" cap="<<cap->semantic_key<<'\n';
        }
        std::cout<<"Individual source edge mouse selection, stored face preservation, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
