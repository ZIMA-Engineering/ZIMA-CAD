int verify_sketch_intersections_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for (int scenario : {11,12,0,1,2,3,4,5,6,7,8,9,10}) {
            const int mode=scenario>=11?scenario:(scenario>=7?5:scenario);
            const QString language=QStringList{"cs","en","de","fr","ru"}[scenario>=7&&scenario<=10?scenario-6:0];
            QSettings settings(QString::fromStdString((directory/"config.ini").string()),QSettings::IniFormat);
            settings.setValue("Application/Language",language);settings.sync();
            std::cout<<"External intersection UI mode "<<mode<<std::endl;
            auto part=document::PartDocument::create_default();
            auto source_feature=document::PartDocument::create_sketch_container();
            auto feature=document::PartDocument::create_sketch_container();
            auto source=sketcher::Sketch::create_default();source.owner_container_id=source_feature.id;
            const auto horizontal=source.add_segment(-40,20,40,20);
            const auto vertical=source.add_segment(27,-40,27,40);
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            std::vector<std::string> references;
            if(mode>=11) {
                sketch.add_segment(-40,-20,0,-20);
                sketch.add_segment(30,10,30,40);
            }
            for (const auto& edge : source.viewer_mesh().edges) {
                if(mode>=4)break;
                if (edge.reference.semantic_key!="segment:"+horizontal && edge.reference.semantic_key!="segment:"+vertical) continue;
                if (mode==2 && edge.reference.semantic_key=="segment:"+vertical) continue;
                auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
                reference.source_document_id=part.document_id;reference.source_owner_id=source.id;
                reference.source_semantic_key=edge.reference.semantic_key;
                for(const auto& p:edge.points)reference.cached_points.push_back(sketch.local_point(p));
                references.push_back(reference.id);sketch.add_external_reference(reference);
            }
            if(mode>=4)source_feature=test::rectangular_feature(part,{40,30,20});
            part.history={source_feature,feature};
            if(mode>=4)part.sketches.push_back(sketch);else part.sketches={source,sketch};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Intersection priority");
            graph.insert({document::PartHistoryKind::Feature,source_feature.id});graph.insert({document::PartHistoryKind::Feature,feature.id});
            part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("external-crossing-"+std::to_string(mode)+".prtz");
            const auto calculated=mode>=4?workspace::calculate_part_with_resolved_references(kernel::OcctKernel{},part):std::vector<kernel::BodyResult>{};
            part.save(path,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open crossing fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate crossing Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Crossing Sketch row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Crossing View missing");
            auto camera=view->camera_state();camera[4]=50;camera[5]=camera[6]=0;camera[7]=50;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Crossing ray missing");return *sketch.intersect_ray(ray->first,ray->second);};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            if(mode>=11) {
                window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();
                const auto start=mode==11?QPointF(-20,-20):QPointF(-17,-13);
                click(screen(start.x(),start.y()));
                const auto end=screen(30,25);mouse(end,QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="sketch_midpoint:"+sketch.segments[1].id,"Midpoint endpoint was not offered");
                click(end);
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                app::SketchPropertiesDialog* properties{};
                for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
                check(properties,"Midpoint test did not return to properties");
                const auto result=properties->pending_value().first;
                check(result.segments.size()==3,"Midpoint-to-midpoint/free-point line was rejected");
                const auto& line=result.segments.back();
                const auto* first=result.find_point(line.first_point_id);
                const auto* last=result.find_point(line.second_point_id);
                check(first&&last&&std::hypot(first->x-start.x(),first->y-start.y())<1e-3&&std::hypot(last->x-30,last->y-25)<1e-6,"Midpoint confirmation moved a requested endpoint");
                for(const auto& before:sketch.points) {
                    const auto* after=result.find_point(before.id);
                    check(after&&std::hypot(after->x-before.x,after->y-before.y)<1e-6,"Midpoint confirmation moved support geometry");
                }
                check(std::ranges::count_if(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Midpoint;})==(mode==11?2:1),"Midpoint relations missing");
                properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
                check(document::PartDocument::load(path).sketches.back().constraints==result.constraints,"Midpoint relations changed on reopen");
                continue;
            }
            if(mode>=4) {
                auto* command=window.findChild<QAction*>(mode!=5?"sketchExternalReferenceAction":"sketchExternalProfileAction");
                check(command&&command->isEnabled(),"External source command unavailable");command->trigger();flush();
                if(mode==5) {
                    const auto loaded=app::ApplicationSettings::load(QString::fromStdString(directory.string()));
                    check(window.findChild<QLabel*>("workspaceState")->text()==loaded.qt_translations.value("Reference → obrys: vyberte hranu tělesa."),"Body edge command status is not localized");
                }
                std::optional<QPointF> hit;
                if(mode==4) {
                    view->set_selection_filter(viewer::SelectionFilter::Faces);
                    const auto& geometry=view->mesh().original_references;
                    std::cout<<"Original face triangles: "<<geometry.triangle_references.size()<<std::endl;
                    for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
                        if(geometry.triangle_references[i].owner_id!=source_feature.id)continue;
                        const auto a=geometry.vertices[geometry.triangles[3*i]],b=geometry.vertices[geometry.triangles[3*i+1]],c=geometry.vertices[geometry.triangles[3*i+2]];
                        const auto p=screen((a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3);const auto candidates=view->selection_candidates_at(p);
                        if(candidates.empty())continue;
                        for(const auto& candidate:candidates)check(candidate.geometry==viewer::CandidateGeometry::OriginalReference,"External Face command offered a result-body face");
                        hit=p;
                    }
                } else if(mode==6) {
                    view->set_selection_filter(viewer::SelectionFilter::Points);
                    for(const auto& point:view->mesh().original_references.points) {
                        if(point.reference.owner_id!=source_feature.id)continue;
                        const auto p=screen(point.position.x,point.position.y);
                        const auto candidates=view->selection_candidates_at(p);
                        if(candidates.empty())continue;
                        for(const auto& candidate:candidates)check(candidate.geometry==viewer::CandidateGeometry::OriginalReference,"External Point command offered a result-body point");
                        hit=p;break;
                    }
                } else {
                    for(const auto& edge:view->mesh().edges) {
                        if(edge.reference.owner_id!=source_feature.id||edge.points.size()<2)continue;
                        const auto a=edge.points.front(),b=edge.points.back();
                        if(std::hypot(a.x-b.x,a.y-b.y)<1)continue;
                        const auto p=screen((a.x+b.x)*.5,(a.y+b.y)*.5);
                        const auto candidates=view->selection_candidates_at(p);
                        if(candidates.empty())continue;
                        for(const auto& candidate:candidates)check(candidate.geometry==viewer::CandidateGeometry::Display,"External Geometry command offered an original edge");
                        hit=p;break;
                    }
                }
                check(hit.has_value(),"External source command offered no geometry");
                if(mode==5)click(*hit);
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
                check(properties,"Source selection did not return to Sketch Properties");
                if(mode==5) {
                    const auto result=properties->pending_value().first;
                    check(!result.segments.empty()&&!result.external_references.empty()&&std::ranges::all_of(result.external_references,[](const auto& r){return r.body_edge;}),"External Geometry did not persist body-edge projection and endpoints");
                }
                properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
                continue;
            }
            auto* action=window.findChild<QAction*>(mode==0?"sketchPointAction":"sketchRectangleAction");check(action&&action->isEnabled(),"Crossing command unavailable");action->trigger();flush();
            if(mode>=2)click(screen(7,-19.5));
            const auto crossing=screen(27,20);mouse(crossing,QEvent::MouseMove,Qt::NoButton);
            const auto offered=view->hovered_candidate();
            check(offered&&offered->semantic_key.starts_with(mode==2?"external_edge:":"sketch_intersection:"),"Common picker did not offer external C/CC");
            if(mode!=2) {
                check(offered->semantic_key.find(references[0])!=std::string::npos&&offered->semantic_key.find(references[1])!=std::string::npos,"Intersection lost one support");
                const auto p=view->candidate_point(*offered);check(p&&std::hypot(p->position.x-27,p->position.y-20)<1e-8,"Intersection candidate has wrong coordinates");
                mouse(crossing,QEvent::MouseButtonPress,Qt::RightButton);mouse(crossing,QEvent::MouseButtonRelease,Qt::RightButton);
                check(view->hovered_candidate()&&view->hovered_candidate()!=offered,"RMB did not cycle the common crossing candidates");
                mouse(screen(34,32),QEvent::MouseMove,Qt::NoButton);mouse(crossing,QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==offered->semantic_key,"Hover did not restore the CC candidate");
            }
            if(mode>=2) {
                const auto before=view->grabFramebuffer();view->set_transient_labels({});flush();const auto after=view->grabFramebuffer();
                check(before==after,"Rectangle endpoint C/CC still displayed lower-priority midpoint labels");
            }
            click(crossing);
            if(mode==1)click(screen(39,34));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
            check(properties,"Crossing test did not return to Sketch Properties");const auto result=properties->pending_value().first;result.validate();
            // A single C constrains y only; x retains the screen-ray precision.
            const auto point=std::ranges::find_if(result.points,[&](const auto& p){return std::abs(p.y-20)<1e-6&&std::abs(p.x-27)<(mode==2?1e-3:1e-6);});
            check(point!=result.points.end(),"Confirmation moved the offered intersection/endpoint");
            for(const auto& id:references)check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.first_point_id==point->id&&c.geometry_id==id&&c.kind==sketcher::ConstraintKind::PointOnLine;}),"Confirmation did not persist every offered reference");
            check(std::ranges::none_of(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::MidpointOnLine||c.kind==sketcher::ConstraintKind::Midpoint;}),"Endpoint C/CC committed a competing M constraint");
            if(mode!=2)check(std::ranges::any_of(result.viewer_mesh().constraint_markers,[](const auto& m){return m.label=="CC";}),"Persisted intersection has no CC marker");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);const auto& reopened=saved.sketches.back();
            check(reopened.constraints==result.constraints,"Intersection constraints changed during native save/reopen");
        }
        std::cout<<"External intersections: first point, both rectangle corners, C/CC priority, RMB and native persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
