int verify_polyline_snapped_tangent_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    for(int kind:{0,1,2,3,4,5,6}) {
        auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        const double end_y=kind==0?0:-60;
        const double target_y=end_y+(kind==4?15:0);
        // Stable context extent keeps the full gesture inside the View.
        static_cast<void>(sketch.add_segment(0,end_y-100,140,end_y-100,1e-6,true));
        static_cast<void>(sketch.add_point(0,end_y+100));
        std::string support;
        if(kind==1)support=sketch.add_segment(100,-90,100,-30);
        if(kind==2||kind==4)support=sketch.add_point(100,target_y);
        if(kind==3||kind>=5)support=sketch.add_circle(100,-40,20);
        part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Polyline endpoint tangent");graph.insert({document::PartHistoryKind::Feature,feature.id});
        part.set_body_history(graph);part.resolve_constructions();
        const auto path=directory/("polyline-endpoint-tangent-"+std::to_string(kind)+".prtz");part.save(path);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Polyline tangent fixture failed to open");flush();
        check(activate_test_body(application,window,body),"Polyline tangent Body unavailable");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&
            (*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
        check(row,"Polyline tangent Sketch row missing");window.show_tree_item_properties(row);flush();
        window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Polyline tangent View missing");
        const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Polyline camera ray missing");
            const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Polyline camera not facing Sketch");return *q;};
        const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
        const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
        const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,
            QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();};
        const auto click=[&](QPointF p,Qt::MouseButton button=Qt::LeftButton){mouse(p,QEvent::MouseMove,Qt::NoButton);
            mouse(p,QEvent::MouseButtonPress,button);mouse(p,QEvent::MouseButtonRelease,button);};
        if(kind>=5) {
            window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();
            click(screen(40,-40));
            const double side=kind==5?1:-1;
            const double angle=side*std::acos(-1.0/3.0);
            const auto move_contact=[&](double delta) {
                mouse(screen(100+20*std::cos(angle+delta),-40+20*std::sin(angle+delta)),QEvent::MouseMove,Qt::NoButton);
                check(!view->transient_edges().empty()&&view->transient_edges().front().points.size()==2,
                    "Segment circle contact preview missing");
                const auto endpoint=view->transient_edges().front().points.back();
                const auto before=view->grabFramebuffer();
                view->set_sketch_cursor(endpoint,true,{});flush();
                check(before==view->grabFramebuffer(),"Segment cursor differs from its resolved contact or duplicates its marker");
                const auto local_endpoint=sketch.intersect_ray(endpoint,view->ray_at(screen(100,-40))->second);
                check(local_endpoint.has_value(),"Segment contact endpoint not on Sketch");
                check(std::abs(std::hypot((*local_endpoint)[0]-100,(*local_endpoint)[1]+40)-20)<1e-6,
                    "Segment contact escaped circle equation");
                return *local_endpoint;
            };
            const auto held=move_contact(0);
            for(double delta:{0.002,-0.002,0.004,0.0}) {
                const auto p=move_contact(delta);
                check(std::hypot(p[0]-held[0],p[1]-held[1])<1e-6,"Tangent contact not held inside capture band");
                const auto start=sketch.intersect_ray(view->transient_edges().front().points.front(),
                    view->ray_at(screen(100,-40))->second);
                check(start.has_value()&&std::abs((p[0]-(*start)[0])*(p[0]-100)+
                    (p[1]-(*start)[1])*(p[1]+40))<1e-5,
                    "Held segment violates tangent equation");
            }
            const auto released=move_contact(side*0.25);
            check(std::hypot(released[0]-held[0],released[1]-held[1])>1,
                "Circle-only contact remained held outside tangent band");
            static_cast<void>(move_contact(0.002));
            click(screen(100+20*std::cos(angle+0.002),-40+20*std::sin(angle+0.002)));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(dialog);p&&p->isVisible())properties=p;
            check(properties,"Segment contact did not return to Sketch Properties");
            const auto result=properties->pending_value().first;
            check(result.segments.size()==2,"Circle tangent segment was not committed");
            const auto& segment=result.segments.back();
            check(std::ranges::any_of(result.constraints,[&](const auto& constraint) {
                return !constraint.suppressed&&constraint.kind==sketcher::ConstraintKind::Tangent&&
                    (constraint.geometry_id==segment.id||constraint.second_geometry_id==segment.id);
            }),"Circle tangent segment lost its committed tangent constraint");
            auto reopened=sketcher::Sketch::from_serialized(result.serialized());
            check(reopened.solve().status!=sketcher::SolveStatus::Conflicting,"Circle tangent segment failed reopen/solve");
            properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            continue;
        }
        window.findChild<QAction*>("sketchPolylineAction")->trigger();flush();
        click(screen(20,end_y+80));click(screen(20,end_y+40));
        // Switch the Polyline mode away from candidates; RMB over a candidate
        // belongs to ordinary pre-confirmation cycling.
        click(screen(60,end_y+75),Qt::RightButton);click(screen(60,end_y));
        click(screen(120,end_y+75),Qt::RightButton);
        const auto endpoint=screen(100,target_y);check(view->rect().contains(endpoint.toPoint()),"Polyline endpoint outside View");
        mouse(endpoint,QEvent::MouseMove,Qt::NoButton);
        check(view->hovered_candidate().has_value(),"Polyline endpoint support not offered by common picker");
        // The starting C T label must survive while the other end offers C/K.
        const auto before=view->grabFramebuffer();view->set_transient_labels({});flush();const auto after=view->grabFramebuffer();
        const auto contact=screen(60,end_y);const double ratio=before.devicePixelRatio();
        const QRect region(qRound((contact.x()+6)*ratio),qRound((contact.y()-28)*ratio),qRound(40*ratio),qRound(23*ratio));
        int changed{};for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x)
            if(before.valid(x,y)&&before.pixel(x,y)!=after.pixel(x,y))++changed;
        if(!(kind==4?changed==0:changed>3)) {
            before.save(QString::fromStdString((directory/("polyline-tangent-preview-"+std::to_string(kind)+".png")).string()));
            throw std::runtime_error("Snapped polyline endpoint has an incorrect start tangent preview; case="+
                std::to_string(kind)+"; changed="+std::to_string(changed));
        }
        click(endpoint);
        const auto state_before_finish=window.findChildren<QLabel*>();
        std::string state_text;for(auto* label:state_before_finish)if(label->isVisible())state_text+=label->text().toStdString()+" | ";
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        app::SketchPropertiesDialog* properties{};
        for(auto* dialog:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(dialog);p&&p->isVisible())properties=p;
        check(properties,"Polyline did not return to Sketch Properties");const auto result=properties->pending_value().first;
        if(!(result.arcs.size()==1&&result.segments.size()==(kind==1?4:3))) {
            std::ofstream(directory/("polyline-tangent-result-"+std::to_string(kind)+".json"))<<result.serialized();
            throw std::runtime_error("Polyline did not commit line/arc/line geometry; case="+std::to_string(kind)+
                "; arcs="+std::to_string(result.arcs.size())+"; segments="+std::to_string(result.segments.size())+"; "+state_text);
        }
        const auto& arc=result.arcs.front();const auto& line=result.segments.back();
        check(line.first_point_id==arc.end_point_id,"Polyline tangent junction lost point identity");
        const auto tangent=std::ranges::find_if(result.constraints,[&](const auto& value){return !value.suppressed&&
            value.kind==sketcher::ConstraintKind::Tangent&&value.first_point_id==arc.end_point_id&&
            (value.geometry_id==arc.id||value.second_geometry_id==arc.id)&&
            (value.geometry_id==line.id||value.second_geometry_id==line.id);});
        check(kind==4?tangent==result.constraints.end():tangent!=result.constraints.end(),"Snapped polyline endpoint has incorrect persisted start tangency");
        const auto mesh=result.viewer_mesh();
        if(kind!=4)check(std::ranges::any_of(mesh.constraint_markers,[&](const auto& marker){return marker.label=="T"&&
            marker.reference.semantic_key=="constraint:"+tangent->id;}),"Persisted polyline tangent has no T marker");
        auto reopened=sketcher::Sketch::from_serialized(result.serialized());const auto solved=reopened.solve();
        check(solved.status!=sketcher::SolveStatus::Conflicting&&solved.status!=sketcher::SolveStatus::Invalid,
            "Snapped tangent polyline failed serialization/solve");
        properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    }
    return 0;
}
