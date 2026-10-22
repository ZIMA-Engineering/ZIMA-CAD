int verify_sketch_feature_axis_reference_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(bool sweep:{false,true})for(bool perpendicular:{false,true}) {
            auto part=document::PartDocument::create_default();
            auto source_sketch=sketcher::Sketch::create_default();
            auto source=document::PartDocument::create_feature_container(source_sketch.id);
            source.feature.type=document::FeatureType::Axis;source_sketch.owner_container_id=source.id;
            if(sweep) {
                source=document::PartDocument::create_sweep3d_container();
                for(auto position:std::vector<kernel::Vec3>{{0,0,0},{0,0,30}}) {
                    auto p=document::PartDocument::create_construction(document::ConstructionKind::Point);
                    p.parent_construction_id=source.sweep3d.path.id;p.origin=position;source.sweep3d.path.curve_points.push_back(p);
                }
                auto profile=sketcher::Sketch::create_default();profile.owner_container_id=source.id;
                static_cast<void>(profile.add_circle(0,0,2));
                source.sweep3d.profiles.push_back({kernel::make_stable_id(),source.sweep3d.path.curve_points.front().id,profile.id,profile.serialized()});
            } else part.sketches.push_back(source_sketch);
            auto target=sketcher::Sketch::create_default();auto owner=document::PartDocument::create_sketch_container();
            target.owner_container_id=owner.id;target.plane_auto=false;
            target.plane=perpendicular?sketcher::SketchPlane::XY:sketcher::SketchPlane::XZ;target.refresh_default_frame();
            part.history={source,owner};part.sketches.push_back(target);
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Feature axis reference");
            for(const auto& f:part.history)graph.insert({document::PartHistoryKind::Feature,f.id});part.set_body_history(graph);part.resolve_constructions();
            const auto input=kernel::OcctKernel{}.evaluate_history(part.kernel_operations());
            const auto path=directory/(std::string(sweep?"sweep-":"feature-")+(perpendicular?"axis-point.prtz":"axis-line.prtz"));part.save(path,input);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Axis reference fixture cannot open");flush();
            check(activate_test_body(application,window,body),"Axis reference Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Axis reference Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* action=window.findChild<QAction*>("sketchExternalReferenceAction");check(action&&action->isEnabled(),"Axis Reference command unavailable");action->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Axis reference View missing");
            auto camera=view->camera_state();camera[4]=60;camera[5]=camera[6]=0;camera[7]=60;view->set_camera_state(camera);flush();
            const auto& axes=view->mesh().original_references.axes;
            const auto axis=std::ranges::find_if(axes,[&](const auto& a){return a.reference.owner_id==source.id&&(sweep?a.reference.semantic_key.starts_with("centerline:"):a.reference.semantic_key=="axis");});
            check(axis!=axes.end(),"Feature axis absent from persisted Sketch source packet");const auto reference=axis->reference;
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Axis reference ray missing");const auto q=target.intersect_ray(ray->first,ray->second);check(q.has_value(),"Axis reference camera is not normal");return *q;};
            const auto o=local({0,0}),u=local({100,0}),v=local({0,100});
            const double ux=(u[0]-o[0])/100,uy=(u[1]-o[1])/100,vx=(v[0]-o[0])/100,vy=(v[1]-o[1])/100,det=ux*vy-uy*vx;
            const auto point=target.local_point(axis->point);
            const QPointF at(((point[0]-o[0])*vy-(point[1]-o[1])*vx)/det,(ux*(point[1]-o[1])-uy*(point[0]-o[0]))/det);
            const auto mouse=[&](QEvent::Type type,Qt::MouseButton button){QMouseEvent e(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            mouse(QEvent::MouseMove,Qt::NoButton);
            const auto wanted=[&]{const auto c=view->offered_candidate();return c&&c->kind==viewer::CandidateKind::Axis&&c->owner_id==reference.owner_id&&c->semantic_key==reference.semantic_key;};
            for(int cycle=0;cycle<64&&!wanted();++cycle){mouse(QEvent::MouseButtonPress,Qt::RightButton);mouse(QEvent::MouseButtonRelease,Qt::RightButton);}
            check(wanted(),"Reference picker did not offer the persisted Feature/Sweep axis");
            mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            auto saved=document::PartDocument::load(path);const auto selected=std::ranges::find(saved.sketches,target.id,&sketcher::Sketch::id);
            if(selected==saved.sketches.end()||selected->external_references.empty()) {
                const auto* status=window.findChild<QLabel*>("workspaceState");
                throw std::runtime_error("Axis reference click failed: "+(status?status->text().toStdString():std::string{}));
            }
            const auto& imported=selected->external_references.back();
            check(imported.source_owner_id==reference.owner_id&&imported.source_semantic_key==reference.semantic_key&&!imported.broken,
                "Axis reference changed its stable source identity");
            check(imported.kind==(perpendicular?sketcher::ExternalReferenceKind::AxisPoint:sketcher::ExternalReferenceKind::Axis)&&imported.infinite==!perpendicular,
                "Axis reference lost its analytical line/point kind");
            check(saved.history==part.history,"Axis reference modified its read-only source history");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto undone=document::PartDocument::load(path);
            check(std::ranges::find(undone.sketches,target.id,&sketcher::Sketch::id)->external_references.empty(),"Axis reference Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(path);check(std::ranges::find(reopened.sketches,target.id,&sketcher::Sketch::id)->external_references==selected->external_references,"Axis reference Redo/reopen lost identity");
            std::cout<<"Axis reference GUI sweep="<<sweep<<" perpendicular="<<perpendicular<<" passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
