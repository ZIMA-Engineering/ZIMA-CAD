int verify_sketch_symmetry_hover_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(int axis_kind=0;axis_kind<3;++axis_kind)for(bool reverse:{false,true}) {
            auto part=document::PartDocument::create_default();auto owner=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=owner.id;
            const std::array<double,2> a=axis_kind==1?std::array{-15.,10.}:axis_kind==2?std::array{25.,10.}:std::array{10.,-15.};
            const std::array<double,2> b=axis_kind==1?std::array{15.,10.}:axis_kind==2?std::array{10.,25.}:std::array{10.,15.};
            const auto first=sketch.add_point(a[0],a[1]),second=sketch.add_point(b[0],b[1]);
            std::string axis=axis_kind==1?"sketch_axis:y":"sketch_axis:x";
            if(axis_kind==2){axis=sketch.add_segment(-20,-20,30,30);sketch.set_geometry_construction(axis,true);}
            part.history={owner};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Symmetry hover");graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);
            const auto path=directory/("symmetry-hover-"+std::to_string(axis_kind)+(reverse?"-reverse":"-forward")+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Symmetry fixture cannot open");flush();
            check(activate_test_body(application,window,body),"Symmetry Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Symmetry Sketch row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Symmetry View missing");
            auto camera=view->camera_state();camera[4]=60;camera[5]=camera[6]=0;camera[7]=60;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Symmetry ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Symmetry camera is not normal");return *q;};
            const auto o=local({0,0}),u=local({100,0}),v=local({0,100});
            const double ux=(u[0]-o[0])/100,uy=(u[1]-o[1])/100,vx=(v[0]-o[0])/100,vy=(v[1]-o[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-o[0])*vy-(p[1]-o[1])*vx)/det,(ux*(p[1]-o[1])-uy*(p[0]-o[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto move=[&](std::array<double,2> p){mouse(screen(p),QEvent::MouseMove,Qt::NoButton);};
            const auto click=[&](std::array<double,2> p){move(p);mouse(screen(p),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(screen(p),QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* action=window.findChild<QAction*>("sketchSymmetricAction");check(action&&action->isEnabled(),"Symmetry command unavailable");action->trigger();flush();
            click(reverse?b:a);
            move(reverse?a:b);const auto offered=view->offered_candidate();
            check(!view->confirmed_candidate()&&offered&&offered->kind==viewer::CandidateKind::SketchPoint&&
                offered->semantic_key=="point:"+(reverse?first:second),"First Symmetry point suppressed second-point hover");
            static_cast<void>(view->grabFramebuffer());
            click(reverse?a:b);click(axis_kind==0?std::array{-12.,0.}:axis_kind==1?std::array{0.,-12.}:std::array{-12.,-12.});
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto result=stored();check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::Symmetric&&c.geometry_id==axis;}),"Symmetry hover transition lost the actual constraint");
            const auto equations=[&](const auto& s) {
                const auto* p=s.find_point(first);const auto* q=s.find_point(second);check(p&&q,"Symmetry lost native points");
                check(axis_kind==0?std::abs(p->x-q->x)<1e-7&&std::abs(p->y+q->y)<1e-7:
                    axis_kind==1?std::abs(p->y-q->y)<1e-7&&std::abs(p->x+q->x)<1e-7:
                    std::abs(p->x-q->y)<1e-7&&std::abs(p->y-q->x)<1e-7,"Symmetry equation failed");
            };
            equations(result);window.findChild<QAction*>("undoAction")->trigger();flush();
            check(stored().constraints.size()+1==result.constraints.size(),"Symmetry Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();equations(stored());
            for(int step=0;step<3;++step) {
                const auto before=stored();const auto* p=before.find_point(reverse?second:first);
                const auto from=screen({p->x,p->y}),to=screen({p->x+2,p->y+3});
                mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto changed=stored();equations(changed);const auto* moved=changed.find_point(p->id);
                check(std::hypot(moved->x-p->x,moved->y-p->y)>1e-5,"Underconstrained Symmetry point did not drag");
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&dynamic_cast<app::SketchPropertiesDialog*>(d))d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            equations(stored());check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Symmetry cannot reopen");flush();
        }
        std::cout<<"Symmetry hover GUI: X/Y/construction axes, both point orders, repeated drags, equations, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
