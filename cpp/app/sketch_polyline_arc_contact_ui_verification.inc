int verify_polyline_arc_contact_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    for(bool external:{false,true})for(bool incoming_arc:{false,true})for(double mirror:{-1.,1.}) {
        std::cout<<"Polyline arc C external="<<external<<" incoming_arc="<<incoming_arc<<" mirror="<<mirror<<std::endl;
        auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        std::string junction,incoming,support;
        if(incoming_arc) {
            incoming=sketch.add_arc(0,mirror*20,-20,mirror*20,0,0,false,1e-6,mirror<0);
            junction=mirror<0?sketch.arcs.back().start_point_id:sketch.arcs.back().end_point_id;
        } else {incoming=sketch.add_segment(-40,0,0,0);junction=sketch.segments.back().second_point_id;}
        static_cast<void>(sketch.add_point(-70,-70));static_cast<void>(sketch.add_point(100,70));
        auto source=sketcher::Sketch::create_default();auto source_feature=document::PartDocument::create_sketch_container();source.owner_container_id=source_feature.id;
        const auto source_circle=source.add_circle(50,mirror*20,10);
        if(external) {
            auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
            ref.source_document_id=part.document_id;ref.source_owner_id=source.id;ref.source_semantic_key="circle:"+source_circle;
            const auto source_geometry=source.placement_reference_geometry();
            ref.exact_spline=source_geometry.edges.front().exact_spline;
            for(const auto& p:source_geometry.edges.front().points)ref.cached_points.push_back(sketch.local_point(p));
            support=ref.id;sketch.add_external_reference(ref);
        } else support=sketch.add_circle(50,mirror*20,10);
        part.history={source_feature,feature};part.sketches={source,sketch};document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Polyline arc contact");graph.insert({document::PartHistoryKind::Feature,source_feature.id});graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto path=directory/("polyline-arc-contact-"+std::to_string(external)+std::to_string(incoming_arc)+std::to_string(mirror)+".prtz");part.save(path);
        const auto load_sketch=[&](const std::string& id) {
            const auto loaded=document::PartDocument::load(path);
            const auto found=std::ranges::find(loaded.sketches,id,&sketcher::Sketch::id);
            check(found!=loaded.sketches.end(),"Polyline saved Sketch identity missing");return *found;
        };
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Polyline arc fixture open failed");flush();
        check(activate_test_body(application,window,body),"Polyline arc Body activation failed");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
        check(row,"Polyline arc Sketch row missing");window.show_tree_item_properties(row);flush();
        window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Polyline arc View missing");
        // Opening resolves the authored reference cache from the real source.
        // Measure read-only behavior against that resolved editing state.
        const auto resolved_sketch=*window.active_sketch_for_test();
        const auto resolved_references=resolved_sketch.external_references;
        const auto source_before=load_sketch(source.id).serialized();
        const auto screen=[&](double x,double y) {
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(bool(ray),"Polyline arc ray missing");return *sketch.intersect_ray(ray->first,ray->second);};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);
        };
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        const auto click=[&](QPointF p,Qt::MouseButton button=Qt::LeftButton){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,button,button);mouse(p,QEvent::MouseButtonRelease,button);};
        const auto pick=[&](QPointF p,const std::string& key,bool confirm=true) {
            view->clear_selection();
            mouse(p,QEvent::MouseMove,Qt::NoButton);const auto candidates=view->selection_candidates_at(p);
            auto at=std::ranges::find_if(candidates,[&](const auto& c){return c.owner_id==sketch.id&&c.semantic_key==key;});
            check(at!=candidates.end(),"Polyline arc picker support missing");
            for(auto i=candidates.begin();i!=at;++i){mouse(p,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
            check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Polyline arc hover selection mismatch");
            if(confirm){mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);}
        };
        window.findChild<QAction*>("sketchPolylineAction")->trigger();flush();pick(screen(0,0),"point:"+junction);
        click(screen(-40,mirror*45),Qt::RightButton);
        const double end_x=50+10*std::cos(2.4),end_y=mirror*(20+10*std::sin(2.4));
        pick(screen(end_x,end_y),(external?"external_edge:":"circle:")+support);
        const auto* current=window.active_sketch_for_test();check(current&&current->arcs.size()==sketch.arcs.size()+1,"Polyline tangent arc was not committed");
        const auto created=current->arcs.back();const auto finish=created.start_point_id==junction?created.end_point_id:created.start_point_id;
        const auto verify=[&](const sketcher::Sketch& value) {
            const auto* p=value.find_point(finish);check(p,"Polyline finish identity lost");
            const auto circle=external?*sketcher::external_reference_circle(value.external_references.front()):std::array{value.find_point(value.circles.front().center_point_id)->x,value.find_point(value.circles.front().center_point_id)->y,value.circles.front().radius};
            check(std::abs(std::hypot(p->x-circle[0],p->y-circle[1])-circle[2])<1e-6,"Polyline finish C left its support");
            check(std::ranges::any_of(value.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnCircle&&c.first_point_id==finish&&c.geometry_id==support;}),"Polyline C attached to the wrong end");
            if(value.external_references!=resolved_references) {
                std::ofstream(directory/"external-before.json")<<resolved_sketch.serialized();
                std::ofstream(directory/"external-after.json")<<value.serialized();
                throw std::runtime_error("Polyline modified external geometry");
            }
            if(incoming_arc)test::verify_circle_contacts(value);else verification::tangency::arc_equations(value);
        };verify(*current);
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
        window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
        for(const auto& id:{created.center_point_id,finish})for(unsigned repeat=0;repeat<3;++repeat) {
            const auto initial=*window.active_sketch_for_test();const auto p=*initial.find_point(id);const auto from=screen(p.x,p.y),to=from+QPointF(18,-12);
            pick(from,"point:"+id,false);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
            const auto* moved=window.active_sketch_for_test();verify(*moved);const auto* q=moved->find_point(id);
            if(std::hypot(q->x-p.x,q->y-p.y)<=1e-6) {
                auto trial=initial;const auto ray=view->ray_at(to);const auto target=initial.intersect_ray(ray->first,ray->second);
                std::cout<<"Polyline failed handle "<<id<<" native movement="<<trial.move_point(id,(*target)[0],(*target)[1])<<std::endl;
                std::ofstream(directory/"failed-drag.json")<<initial.serialized();
                window.grab().save(QString::fromStdString((directory/"failed-drag.png").string()));
                throw std::runtime_error("Polyline GUI drag did not move");
            }
        }
        const auto edited=*window.active_sketch_for_test();window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
        check(properties,"Polyline arc did not return to Properties");properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Polyline arc Save failed");
        const auto saved=load_sketch(sketch.id);verify(saved);check(saved.serialized()==edited.serialized(),"Polyline arc native reopen differs");
        check(load_sketch(source.id).serialized()==source_before,"Polyline modified its source Sketch");
        check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Polyline arc Undo/Redo failed");
        check(load_sketch(sketch.id).serialized()==saved.serialized(),"Polyline arc Undo/Redo changed identities");
        view->grabFramebuffer().save(QString::fromStdString((directory/("polyline-arc-"+std::to_string(external)+std::to_string(incoming_arc)+std::to_string(mirror)+".png")).string()));
    }
    std::cout<<"Polyline arc contact GUI: eight cases passed"<<std::endl;return 0;
}
