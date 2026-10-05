int verify_pending_arc_keypoints_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(int shape=0;shape<3;++shape) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            static_cast<void>(sketch.add_point(-40,-40));static_cast<void>(sketch.add_point(65,65));
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Own arc K");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("own-arc-K-"+std::to_string(shape)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Own K fixture cannot open");flush();
            check(activate_test_body(application,window,body),"Own K Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*it)->data(0,Qt::UserRole+3)=="part-sketch")row=*it;
            check(row,"Own K Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Own K View missing");
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Own K ray unavailable");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Own K camera misses Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto own_K=[&](std::array<double,2> p) {
                const auto pixel=screen(p)+QPointF(2,2);mouse(pixel,QEvent::MouseMove,Qt::NoButton);
                const auto offered=view->offered_candidate();
                check(offered&&offered->semantic_key.starts_with("sketch_curve_keypoint:preview:"),"Draft arc does not offer its own K through the common picker");
                const auto point=view->candidate_point(*offered);check(point.has_value(),"Own K lacks its exact offered point");
                const auto position=sketch.local_point(point->position);
                mouse(pixel,QEvent::MouseButtonPress,Qt::LeftButton);mouse(pixel,QEvent::MouseButtonRelease,Qt::LeftButton);
                return position;
            };
            auto* action=window.findChild<QAction*>(shape?"sketchEllipticalArcAction":"sketchArcAction");check(action&&action->isEnabled(),"Own K command unavailable");action->trigger();flush();
            const auto center_pixel=screen({13,17});const auto center=local(center_pixel);click(center_pixel);
            const auto major_pixel=screen({33,37});
            const auto major=shape==2?local(major_pixel):own_K({33,17});
            if(shape==2)click(major_pixel);
            std::array<double,2> start=major,end{},minor{};
            if(shape) {
                minor=own_K({center[0]-.5*(major[1]-center[1]),center[1]+.5*(major[0]-center[0])});
                start=own_K({2*center[0]-major[0],2*center[1]-major[1]});
                end=own_K(minor);
            } else {
                const double radius=std::hypot(major[0]-center[0],major[1]-center[1]);
                end=own_K({center[0],center[1]+radius});
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
            check(properties,"Own K did not return to Sketch Properties");const auto result=properties->pending_value().first;result.validate();
            const auto exact=[&](const std::string& id,std::array<double,2> p){const auto* point=result.find_point(id);const bool same=point&&std::hypot(point->x-p[0],point->y-p[1])<1e-7;
                if(!same&&point)std::cerr<<"Own K shape "<<shape<<" expected "<<p[0]<<','<<p[1]<<" received "<<point->x<<','<<point->y<<'\n';return same;};
            if(shape) {
                check(result.elliptical_arcs.size()==1,"Own K elliptical arc was not committed");const auto& arc=result.elliptical_arcs.front();
                check(exact(arc.center_point_id,center)&&exact(arc.major_point_id,major)&&exact(arc.minor_point_id,minor)&&
                    exact(arc.start_point_id,start)&&exact(arc.end_point_id,end),"Own K ellipse confirmation differs from the offered points");
            } else {
                check(result.arcs.size()==1,"Own K circular arc was not committed");const auto& arc=result.arcs.front();
                check(exact(arc.center_point_id,center)&&exact(arc.start_point_id,start)&&exact(arc.end_point_id,end),"Own K arc confirmation differs from the offered points");
            }
            check(result.serialized().find("sketch_preview_keypoint")==std::string::npos,"Own K leaked a draft self-reference into native Sketch data");
            properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);saved.sketches.front().validate();
            check(saved.sketches.front().arcs==result.arcs&&saved.sketches.front().elliptical_arcs==result.elliptical_arcs,"Own K geometry changed on native save/reopen");
            check(std::ranges::none_of(view->selection_candidates_at(screen(end)),[](const auto& candidate){return candidate.semantic_key.starts_with("sketch_curve_keypoint:preview:");}),
                "Finished arc command retained draft K candidates");
        }
        std::cout<<"Own arc K: center-relative radius/start/end, elliptical semi-axes, rotated ellipse, common hover/click points and native persistence passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
