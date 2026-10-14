int verify_sketch_circle_endpoint_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        for(bool upper:{false,true}) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            static_cast<void>(sketch.add_rectangle(0,0,120,-80));
            const auto endpoint=std::ranges::find_if(sketch.points,[&](const auto& p){return std::abs(p.x-120)<1e-8&&std::abs(p.y-(upper?0:-80))<1e-8;})->id;
            const auto original_points=sketch.points.size();feature.feature.sketch_id=sketch.id;
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Circle endpoint");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/(upper?"circle-upper.prtz":"circle-lower.prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Circle endpoint fixture did not open");flush();
            check(activate_test_body(application,window,body),"Circle endpoint Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Circle endpoint Sketch missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Circle endpoint View missing");
            view->fit_points({sketch.world_point(-30,-110),sketch.world_point(190,30)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Circle camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Circle camera not facing Sketch");return *q;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            window.findChild<QAction*>("sketchCircleAction")->trigger();flush();click(screen(120,-40));
            const auto rim=screen(120,upper?0:-80);mouse(rim,QEvent::MouseMove,Qt::NoButton);
            const auto offered=view->selection_candidates_at(rim);const auto point=std::ranges::find_if(offered,[&](const auto& c){return c.semantic_key=="point:"+endpoint;});
            check(point!=offered.end(),"Circle rim did not offer existing native endpoint");
            for(std::size_t i=0;i<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!="point:"+endpoint);++i){mouse(rim,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(rim,QEvent::MouseButtonRelease,Qt::RightButton);}
            check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="point:"+endpoint,"Circle endpoint was not selected for confirmation");
            mouse(rim,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(rim,QEvent::MouseButtonRelease,Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::SketchPropertiesDialog*>(dialog);properties&&properties->isVisible())properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto loaded=document::PartDocument::load(path);const auto& result=loaded.sketches.front();
            check(result.circles.size()==1,"Circle endpoint confirmation failed");
            check(result.points.size()==original_points+1&&result.find_point(endpoint),"Circle creation duplicated the snapped native point");
            check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnCircle&&c.first_point_id==endpoint&&c.geometry_id==result.circles.front().id;}),"Circle does not own its C at the original endpoint");
            for(const auto& c:result.constraints)if(c.kind==sketcher::ConstraintKind::Tangent)check(c.first_point_id==endpoint,"Circle T uses another contact point");
            check(std::ranges::none_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnLine&&c.first_point_id==endpoint&&!c.geometry_id.starts_with("sketch_axis:");}),"Circle persisted redundant C on its own line endpoint");
            // Continue the user's rectangle -> circle -> Trim gesture in the
            // real View, then exercise draft Undo/Redo and native reopening.
            row=nullptr;
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Circle Sketch missing after save");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop trim_animation;QTimer::singleShot(950,&trim_animation,&QEventLoop::quit);trim_animation.exec();
            auto* trim=window.findChild<QAction*>("sketchTrimAction");trim->trigger();flush();
            const auto cut=[&](QPointF p) {
                mouse(p,QEvent::MouseMove,Qt::NoButton);
                const auto candidates=view->selection_candidates_at(p);
                const auto piece=std::ranges::find_if(candidates,[](const auto& c){return c.kind==viewer::CandidateKind::SketchTrimPiece;});
                check(piece!=candidates.end(),"Circle/rectangle trim piece is not selectable");
                for(std::size_t i=0;i<candidates.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=piece->semantic_key);++i){mouse(p,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            cut(screen(80,-40));cut(screen(120,-40));trim->trigger();flush();
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* properties=dynamic_cast<app::SketchPropertiesDialog*>(dialog);properties&&properties->isVisible())properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto trimmed=document::PartDocument::load(path);const auto& final=trimmed.sketches.front();
            check(final.circles.empty()&&final.arcs.size()==1&&final.segments.size()==3,"Mouse Trim did not retain the rounded rectangle profile");
            check(final.points.size()==original_points+1&&final.find_point(endpoint),"Mouse Trim duplicated or replaced the original endpoint");
            check(final.arcs.front().start_point_id==endpoint||final.arcs.front().end_point_id==endpoint,"Trimmed Arc is not incident to the original point");
            for(const auto& c:final.constraints)if(c.kind==sketcher::ConstraintKind::Tangent)check(c.first_point_id==endpoint,"Trim lost tangent contact identity");
            verification::tangency::arc_equations(final);
            view->grabFramebuffer().save(QString::fromStdString((directory/(upper?"circle-upper.png":"circle-lower.png")).string()));
        }
        std::cout<<"Circle creation GUI: upper/lower native endpoint mouse snaps reuse one point; C/T identity and native save/reopen passed"<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
