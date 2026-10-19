int verify_socket_axis_merge_ui(QApplication& application,const std::filesystem::path& directory,const std::filesystem::path& source) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::vector<kernel::BodyResult> cache;const auto original=document::PartDocument::load(source,&cache);
        const auto sketch=original.sketches.back();
        const auto axis=std::ranges::find_if(sketch.segments,[](const auto& s){return s.centerline;});
        check(axis!=sketch.segments.end(),"Socket construction axis missing");
        const auto endpoint=axis->second_point_id;const auto* end=sketch.find_point(endpoint);
        std::string profile;double distance=1e100;
        for(const auto& line:sketch.segments)if(!line.construction)for(const auto& id:{line.first_point_id,line.second_point_id}) {
            const auto* p=sketch.find_point(id);const double d=std::hypot(p->x-end->x,p->y-end->y);
            if(d<distance){distance=d;profile=id;}
        }
        check(!profile.empty()&&profile!=endpoint,"Socket profile endpoint missing");
        for(const bool reverse:{false,true}) {
            const auto path=directory/(reverse?"socket-axis-reverse.prtz":"socket-axis-forward.prtz");original.save(path,cache);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Socket fixture did not open");flush();
            check(activate_test_body(application,window,original.body_history.active_body_id()),"Socket Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Socket Sketch Tree row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            if(open){open->click();flush();}
            check(window.findChild<QAction*>("sketchCoincidentAction")->isEnabled(),"Socket Sketch did not enter editing");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Socket View missing");
            view->fit_points({sketch.world_point(-.4,2),sketch.world_point(2,4.4)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Socket camera ray missing");const auto xy=sketch.intersect_ray(ray->first,ray->second);check(xy.has_value(),"Socket camera not facing Sketch");return *xy;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto pick=[&](const std::string& id) {
                const auto* point=sketch.find_point(id);const auto p=screen(point->x,point->y);const auto key="point:"+id;
                mouse(p,QEvent::MouseMove,Qt::NoButton);const auto offered=view->selection_candidates_at(p);
                check(std::ranges::any_of(offered,[&](const auto& c){return c.owner_id==sketch.id&&c.semantic_key==key;}),"Socket endpoint not offered by mouse picking");
                for(std::size_t i=0;i<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++i){mouse(p,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Socket endpoint hover cycling failed");
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            const auto point_count=[&]{std::set<std::string> ids;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id&&p.reference.semantic_key.starts_with("point:"))ids.insert(p.reference.semantic_key);return ids.size();};
            const auto before=point_count();window.findChild<QAction*>("sketchCoincidentAction")->trigger();flush();
            pick(reverse?endpoint:profile);pick(reverse?profile:endpoint);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(point_count()+1==before,"Socket GUI C did not merge the two native points");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(point_count()==before,"Socket merge Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(point_count()+1==before,"Socket merge Redo failed");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible())if(auto* buttons=d->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok);ok&&ok->isEnabled())ok->click();
            flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(path);auto actual=*std::ranges::find(reopened.sketches,sketch.id,&sketcher::Sketch::id);
            check(actual.points.size()+1==sketch.points.size()&&actual.external_references==sketch.external_references,"Socket saved merge lost topology or external support");
            const auto survivor=reverse?endpoint:profile;const auto* point=actual.find_point(survivor);check(point&&std::hypot(point->x-end->x,point->y-end->y)<1e-8,"Socket saved merge moved off the axis endpoint");
            const auto solved=actual.solve();check(solved.remaining_degrees_of_freedom==0&&solved.maximum_residual<1e-8,"Socket saved merge equations failed");
            view->grabFramebuffer().save(QString::fromStdString((directory/(reverse?"socket-axis-reverse.png":"socket-axis-forward.png")).string()));
        }
        std::cout<<"Socket axis GUI merge: both mouse selection orders, one native point, Undo/Redo, native save/reopen and unchanged external support passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
