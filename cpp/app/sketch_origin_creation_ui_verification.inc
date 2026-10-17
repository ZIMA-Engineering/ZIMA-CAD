int verify_sketch_origin_creation_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        struct Case {const char* action;std::vector<std::array<double,2>> points;unsigned polygon{};bool spline{};};
        const std::vector<Case> cases{
            {"sketchPointAction",{{0,0}}},
            {"sketchSegmentAction",{{0,0},{5,3}}},{"sketchSegmentAction",{{-5,-3},{0,0}}},
            {"sketchConstructionAction",{{0,0},{5,3}}},
            {"sketchPolylineAction",{{0,0},{5,3},{8,1}}},
            {"sketchRectangleAction",{{0,0},{5,3}}},{"sketchRectangleAction",{{-5,-3},{0,0}}},
            {"sketchSlotAction",{{0,0},{8,0},{8,2}}},{"sketchSlotAction",{{-8,0},{0,0},{0,2}}},
            {"sketchPolygonAction",{{0,0},{5,0}},4},{"sketchPolygonAction",{{-5,0},{0,0}},4},
            {"sketchPolygonAction",{{0,0},{5,0}},6},{"sketchPolygonAction",{{-5,0},{0,0}},6},
            {"sketchPolygonAction",{{0,0},{5,0}},8},{"sketchPolygonAction",{{-5,0},{0,0}},8},
            {"sketchCircleAction",{{0,0},{5,0}}},{"sketchCircleAction",{{-5,0},{0,0}}},
            {"sketchArcAction",{{0,0},{5,0},{0,5}}},{"sketchArcAction",{{-5,0},{0,0},{-5,5}}},{"sketchArcAction",{{-5,0},{-5,5},{0,0}}},
            {"sketchEllipseAction",{{0,0},{5,0},{0,3}}},{"sketchEllipseAction",{{-5,0},{0,0},{-5,3}}},{"sketchEllipseAction",{{0,-3},{5,-3},{0,0}}},
            {"sketchEllipticalArcAction",{{0,0},{5,0},{0,3},{5,0},{0,3}}},
            {"sketchEllipticalArcAction",{{-5,0},{0,0},{-5,3},{0,0},{-5,3}}},
            {"sketchEllipticalArcAction",{{0,-3},{5,-3},{0,0},{5,-3},{0,0}}},
            {"sketchEllipticalArcAction",{{0,-3},{5,-3},{0,0},{0,0},{-5,-3}}},
            {"sketchEllipticalArcAction",{{0,-3},{5,-3},{0,0},{-5,-3},{0,0}}},
            {"sketchBSplineAction",{{0,0},{3,2},{6,1},{9,3}},0,true},
            {"sketchBSplineAction",{{-3,-2},{0,0},{3,1},{6,3}},0,true},
            {"sketchBSplineAction",{{-9,-3},{-6,-1},{-3,-2},{0,0}},0,true},
            {"sketchInterpolatingSplineAction",{{0,0},{3,2},{6,1},{9,3}},0,true},
            {"sketchInterpolatingSplineAction",{{-3,-2},{0,0},{3,1},{6,3}},0,true},
            {"sketchInterpolatingSplineAction",{{-9,-3},{-6,-1},{-3,-2},{0,0}},0,true}
        };
        unsigned verified=0;
        const auto start=unsigned(std::max(0,qEnvironmentVariableIntValue("ZIMA_ORIGIN_CASE_START")));
        const auto limit=qEnvironmentVariableIsSet("ZIMA_ORIGIN_CASE_LIMIT")?unsigned(qEnvironmentVariableIntValue("ZIMA_ORIGIN_CASE_LIMIT")):unsigned(cases.size()*3);
        for(unsigned scenario=start;scenario<std::min(limit,unsigned(cases.size()*3));++scenario) {
            const auto& test=cases[scenario%cases.size()];const unsigned plane=scenario/unsigned(cases.size());
            std::cout<<"Origin GUI "<<scenario<<" "<<test.action<<" plane="<<plane<<std::endl;
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();auto sketch=sketcher::Sketch::create_default();
            sketch.owner_container_id=feature.id;sketch.plane_auto=false;
            sketch.plane=plane==0?sketcher::SketchPlane::XY:plane==1?sketcher::SketchPlane::XZ:sketcher::SketchPlane::YZ;
            if(std::string_view(test.action)=="sketchConstructionAction")
                static_cast<void>(sketch.add_segment(0,0,5,3));
            feature.feature.sketch_id=sketch.id;part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Origin creation");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();sketch=part.sketches.front();
            const auto path=directory/("origin-"+std::to_string(scenario)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Origin fixture did not open");flush();check(activate_test_body(application,window,body),"Origin Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Origin Sketch missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Origin View missing");view->fit_points({sketch.world_point(-12,-9),sketch.world_point(12,9)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Origin camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Origin camera not facing Sketch");return *q;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            auto* action=window.findChild<QAction*>(test.action);check(action&&action->isEnabled(),"Origin tool disabled");action->trigger();flush();
            for(unsigned i=0;i<test.points.size();++i) {
                const auto p=test.points[i];auto target=screen(p[0],p[1]);
                if(p[0]==0&&p[1]==0)target+=QPointF(7*view->devicePixelRatioF(),7*view->devicePixelRatioF());
                mouse(target,QEvent::MouseMove,Qt::NoButton);mouse(target,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(target,QEvent::MouseButtonRelease,Qt::LeftButton);
                if(test.polygon&&i==0)for(unsigned sides=4;sides<test.polygon;sides+=2){mouse(screen(5,2),QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(screen(5,2),QEvent::MouseButtonRelease,Qt::RightButton);}
            }
            if(test.spline){const auto p=screen(test.points.back()[0],test.points.back()[1]);mouse(p,QEvent::MouseButtonDblClick,Qt::MiddleButton,Qt::MiddleButton);mouse(p,QEvent::MouseButtonRelease,Qt::MiddleButton);}
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto created=snapshot();
            if(created.points.empty()) {
                if(const auto* state=window.findChild<QLabel*>("workspaceState"))std::cerr<<state->text().toStdString()<<std::endl;
            }
            if(std::string_view(test.action)=="sketchConstructionAction") {
                const auto green_pixels=[](const QImage& image){unsigned count=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)if(image.pixelColor(x,y)==QColor("#4DD811"))++count;return count;};
                view->clear_selection();mouse(screen(-10,-8),QEvent::MouseMove,Qt::NoButton);
                check(green_pixels(view->grabFramebuffer())>10,"Construction wire did not cover coincident ordinary wire in green");
                for(const auto mode:{QIcon::Normal,QIcon::Active})for(const auto state:{QIcon::Off,QIcon::On})
                    check(green_pixels(action->icon().pixmap(QSize(24,24),mode,state).toImage())>3,"Construction icon lost semantic green");
            }
            const auto anchor=std::ranges::find_if(created.constraints,[](const auto& c){return !c.suppressed&&c.kind==sketcher::ConstraintKind::PointReference&&c.second_point_id=="sketch_origin";});
            check(anchor!=created.constraints.end(),"Origin click did not persist a two-coordinate point reference");const auto point_id=anchor->first_point_id;
            const auto verify=[&](sketcher::Sketch actual) {
                const auto* p=actual.find_point(point_id);check(p&&std::hypot(p->x,p->y)<1e-7,"Origin anchor moved");
                for(const auto& c:actual.circles)check(c.radius>0&&actual.find_point(c.center_point_id),"Origin snap broke circle ownership");
                for(const auto& a:actual.arcs){const auto* c=actual.find_point(a.center_point_id);for(const auto& id:{a.start_point_id,a.end_point_id}){const auto* e=actual.find_point(id);check(c&&e&&std::abs(std::hypot(e->x-c->x,e->y-c->y)-a.radius)<1e-6,"Origin snap broke arc incidence");}}
                check(actual.solve().maximum_residual<1e-7,"Origin geometry equations failed");
                check(std::ranges::any_of(actual.viewer_mesh().constraint_markers,[&](const auto& marker){return marker.label=="C"&&marker.reference.semantic_key=="constraint:"+anchor->id;}),"Origin C marker is missing");
            };verify(created);
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();check(snapshot().serialized()==created.serialized(),"Origin creation Undo/Redo lost identity");
            if(scenario%cases.size()==0) {
                const auto label=screen(0,0)+QPointF(10,-12);view->clear_selection();mouse(label,QEvent::MouseMove,Qt::NoButton);
                const auto key="constraint:"+anchor->id;const auto offered=view->selection_candidates_at(label);
                for(std::size_t n=0;n<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++n){mouse(label,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(label,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Origin C label cannot be selected");
                mouse(label,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(label,QEvent::MouseButtonRelease,Qt::LeftButton);
                QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(view,&remove);flush();
                auto free=snapshot();check(std::ranges::none_of(free.constraints,[&](const auto& c){return c.id==anchor->id;})&&free.solve().remaining_degrees_of_freedom==2,"Deleting Origin C did not release the point");
                window.findChild<QAction*>("undoAction")->trigger();flush();verify(snapshot());
                window.findChild<QAction*>("redoAction")->trigger();flush();check(snapshot().constraints.empty(),"Origin C deletion Redo failed");
                window.findChild<QAction*>("undoAction")->trigger();flush();verify(snapshot());
            }
            for(unsigned repeat=0;repeat<3;++repeat) {
                view->clear_selection();const auto p=screen(0,0);mouse(p,QEvent::MouseMove,Qt::NoButton);
                const auto offered=view->selection_candidates_at(p);const auto key="point:"+point_id;
                for(std::size_t n=0;n<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++n){mouse(p,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Origin point cannot be picked for dragging");
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p+QPointF(12,-9),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(p+QPointF(12,-9),QEvent::MouseButtonRelease,Qt::LeftButton);verify(snapshot());
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();for(auto* d:window.findChildren<QDialog*>())if(auto* props=dynamic_cast<app::SketchPropertiesDialog*>(d);props&&props->isVisible())props->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();verify(snapshot());++verified;
        }
        std::cout<<"Origin GUI matrix: "<<verified<<" creation cases, all defining point roles, XY/XZ/YZ, repeated drags, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
