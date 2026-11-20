int verify_point_circle_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
        window.resize(1450,1050);window.show();flush();
        auto part=document::PartDocument::create_default();part.history.clear();part.sketches.clear();
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Circle picking");part.set_body_history(graph);
        const auto path=directory/"point-circle.prtz";part.save(path);
        check(window.open_document_path(QString::fromStdString(path.string())),"Circle fixture cannot open");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Circle Body cannot activate");flush();
        window.findChild<QAction*>("loftAction")->trigger();flush();
        auto* parent=dynamic_cast<LoftDialog*>(window.findChild<QWidget*>("loftDialog"));check(parent,"Circle Loft dialog missing");
        for(unsigned i=0;i<2;++i) {
            auto owned=document::create_general_surface_sketch(parent->pending);
            auto sketch=sketcher::Sketch::from_serialized(owned.sketch_serialized);sketch.plane_auto=false;
            owned.sketch_feature->feature.profile_plane_offset=65.*i;sketch.plane_offset=65.*i;
            static_cast<void>(sketch.add_circle(0,0,56.4));owned.sketch_serialized=sketch.serialized();
            parent->pending.loft.profiles.push_back(owned.sketch_feature->id);parent->pending.loft.sketches.push_back(std::move(owned));
        }
        document::resolve_loft_sketches(parent->pending,{});parent->refresh();parent->changed();flush();
        const auto fixture=qEnvironmentVariable("ZIMA_POINT_CIRCLE_FIXTURE");
        if(!fixture.isEmpty()) {
            const auto source=document::PartDocument::load(std::filesystem::path(fixture.toStdWString()));
            const auto loft=std::ranges::find_if(source.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Loft;});
            check(loft!=source.history.end(),"User fixture has no Loft");
            parent->pending=*loft;parent->refresh();parent->changed();flush();
        }
        const auto initial=sketcher::Sketch::from_serialized(parent->pending.loft.sketches[1].sketch_serialized);
        const auto initial_point_count=initial.points.size();
        const auto& source_circle=initial.circles.front();const auto* source_center=initial.find_point(source_circle.center_point_id);
        const auto circle_x=source_center->x,circle_y=source_center->y,circle_radius=source_circle.radius;
        auto* table=parent->findChild<QTableWidget*>("loftProfiles");
        const auto field=table->visualItemRect(table->item(1,3)).center();
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent e(type,field,QPointF(table->viewport()->mapToGlobal(field)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(table->viewport(),&e);flush();
        }
        auto* child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));check(child,"Circle child dialog missing");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Circle View missing");
        const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){
            QMouseEvent e(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
        };
        const unsigned cycles=fixture.isEmpty()?4:8;
        for(unsigned cycle=0;cycle<cycles;++cycle) {
            check(std::ranges::none_of(view->transient_edges(),[](const auto& edge){return edge.reference.semantic_key.starts_with("external_");}),"Owned Loft preview contains an external reference edge");
            if(cycle==0) {
                const auto expected=document::loft_definition_mesh(part,parent->pending,{});
                check(view->transient_edges().size()==expected.edges.size(),"Owned Loft preview differs from the parent profile preview");
                window.grab().save(QString::fromStdString((directory/"point-circle-owned-preview.png").string()));
            }
            child->findChild<QPushButton*>("featureSketchButton")->click();flush();settle();
            const auto sketch=document::general_surface_display_sketch(document::loft_sketch_editor_carrier(parent->pending),parent->pending.loft.sketches[1]);
            view->set_view_direction(sketch.resolved_normal);settle();
            view->fit_points({sketch.world_point(-85,-75),sketch.world_point(85,75)});flush();
            if(cycle>=4) {
                view->fit_points({sketch.world_point(circle_x-25,circle_y+circle_radius-15),sketch.world_point(circle_x+25,circle_y+circle_radius+15)});flush();
            }
            if(cycle&&cycle<4) {
                window.findChild<QAction*>("cursorOrbitAction")->setChecked(true);
                const QPointF p(view->width()*.61,view->height()*.42);
                mouse(p,QEvent::MouseButtonPress,Qt::MiddleButton,Qt::MiddleButton);
                mouse(p+QPointF(30,15),QEvent::MouseMove,Qt::NoButton,Qt::MiddleButton);
                mouse(p+QPointF(30,15),QEvent::MouseButtonRelease,Qt::MiddleButton);
                if(cycle==2){view->set_view_direction(sketch.resolved_normal);settle();}
            }
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Circle ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Circle plane missing");return *q;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            window.findChild<QAction*>("sketchPointAction")->trigger();flush();
            for(unsigned n=0;n<4;++n) {
                const double angle=(cycle<4?13.+cycle*7.+n*90.:78.+(cycle-4)*1.4+n*7.)*std::numbers::pi/180.;
                const auto at=screen(circle_x+circle_radius*std::cos(angle),circle_y+circle_radius*std::sin(angle))+QPointF(2,-1);
                const auto offered=view->selection_candidates_at(at);
                std::cout<<"Circle click cycle="<<cycle<<" n="<<n<<" screen="<<at.x()<<","<<at.y()<<" candidates=";
                for(const auto& c:offered)std::cout<<c.owner_id<<":"<<c.semantic_key<<";";std::cout<<std::endl;
                mouse(at,QEvent::MouseMove,Qt::NoButton);
                // External supports retain their explicit common-picker priority.
                // Select the circle through the same RMB cycle available to users.
                for(std::size_t attempt=0;attempt<offered.size();++attempt) {
                    const auto selected=view->hovered_candidate();
                    if(selected&&selected->semantic_key=="circle:"+source_circle.id)break;
                    mouse(at,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(at,QEvent::MouseButtonRelease,Qt::RightButton);
                }
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="circle:"+source_circle.id,"Circle unavailable at its projected cursor position");
                mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);
                window.grab().save(QString::fromStdString((directory/("point-circle-"+std::to_string(cycle)+"-"+std::to_string(n)+".png")).string()));
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            child->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            const auto result=sketcher::Sketch::from_serialized(parent->pending.loft.sketches[1].sketch_serialized);
            const auto& circle=result.circles.front();const auto* center=result.find_point(circle.center_point_id);
            for(const auto& point:result.points)if(point.id!=center->id) {
                const auto error=std::abs(std::hypot(point.x-center->x,point.y-center->y)-circle.radius);
                std::cout<<"Circle point "<<point.id<<" radial error="<<error<<std::endl;
                check(error<1e-7,"Point tool placed a point off the native circle");
                check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.first_point_id==point.id&&c.kind==sketcher::ConstraintKind::PointOnCircle;}),"Point tool lost circle incidence");
            }
            check(result.points.size()==initial_point_count+4*(cycle+1),"Point tool lost a successive point");
            if(cycle+1<cycles) {
                const auto at=table->visualItemRect(table->item(1,3)).center();
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {QMouseEvent e(type,at,QPointF(table->viewport()->mapToGlobal(at)),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&e);flush();}
                child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));check(child,"Circle child did not reopen");
            }
        }
        parent->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        std::cout<<"Point-circle GUI: successive native circle snaps, cursor orbit and repeated owned-Sketch entry passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
