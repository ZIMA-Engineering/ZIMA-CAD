int verify_bolt_center_ui(QApplication& application,const std::filesystem::path& directory,const std::filesystem::path& source) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        std::vector<kernel::BodyResult> cache;const auto original=document::PartDocument::load(source,&cache);
        const auto feature=std::ranges::find_if(original.history,[](const auto& item){return item.name.ends_with("004");});
        check(feature!=original.history.end(),"Bolt feature 004 missing");
        const auto found=std::ranges::find(original.sketches,feature->feature.sketch_id,&sketcher::Sketch::id);
        check(found!=original.sketches.end()&&found->circles.size()==2,"Bolt relief sketch missing");const auto sketch=*found;
        const auto center=sketch.circles.front().center_point_id;
        for(unsigned scenario=0;scenario<6;++scenario) {
            const unsigned support=scenario/2;const bool reverse=scenario%2;
            const std::string target=support==2?"external_point:sketch_origin":support?"sketch_axis:y":"sketch_axis:x";
            const auto path=directory/("bolt-center-"+std::to_string(scenario)+".prtz");original.save(path,cache);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Bolt GUI fixture did not open");flush();
            check(activate_test_body(application,window,original.body_history.active_body_id()),"Bolt Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Bolt Sketch Tree row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");
            if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            if(open){open->click();flush();}
            check(window.findChild<QAction*>("sketchCoincidentAction")->isEnabled(),"Bolt Sketch did not enter editing");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Bolt View missing");
            view->fit_points({sketch.world_point(-7,-7),sketch.world_point(7,7)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Bolt camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Bolt camera not facing Sketch");return *q;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto pick=[&](const std::string& key) {
                const auto p=screen(0,0);view->clear_selection();mouse(p,QEvent::MouseMove,Qt::NoButton);
                const auto offered=view->selection_candidates_at(p);
                check(std::ranges::any_of(offered,[&](const auto& c){return c.owner_id==sketch.id&&c.semantic_key==key;}),"Bolt center/Origin/axis not offered");
                for(std::size_t i=0;i<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++i){mouse(p,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(p,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Bolt reference cycling failed");
                mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            const auto marker_keys=[&]{std::set<std::string> keys;for(const auto& marker:view->mesh().constraint_markers)if(marker.reference.owner_id==sketch.id)keys.insert(marker.reference.semantic_key);return keys;};
            const auto before=marker_keys();
            const auto verify_live=[&]{const auto& mesh=view->mesh();const auto point=std::ranges::find_if(mesh.points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+center;});check(point!=mesh.points.end(),"Bolt live center missing");const auto xy=sketch.local_point(point->position);check(std::hypot(xy[0],xy[1])<1e-7,"Bolt live center escaped Origin");};
            window.findChild<QAction*>("sketchCoincidentAction")->trigger();flush();
            pick(reverse?target:"point:"+center);pick(reverse?"point:"+center:target);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto verify=[&](sketcher::Sketch actual) {
                const auto* p=actual.find_point(center);check(p&&std::hypot(p->x,p->y)<1e-7,"Bolt center escaped its Origin");
                check(actual.circles[0].center_point_id==center&&actual.circles[1].center_point_id==center,"Bolt lost shared center identity");
                check(actual.external_references==sketch.external_references,"Bolt modified read-only external support");
                const auto solved=actual.solve();check(solved.maximum_residual<1e-7,"Bolt equations failed");
                check(solved.remaining_degrees_of_freedom==(support?0:1),"Bolt center has incorrect freedom after GUI C");
            };
            const auto created=marker_keys();check(created.size()==before.size()+(support?1:0),"GUI bolt C marker count incorrect");verify_live();
            if(support) {
                window.findChild<QAction*>("undoAction")->trigger();flush();check(marker_keys()==before,"Bolt C Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(marker_keys()==created,"Bolt C Redo failed");
                for(unsigned i=0;i<3;++i) {
                    const auto p=screen(0,0),to=p+QPointF(12,-8);pick("point:"+center);
                    mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);verify_live();check(marker_keys()==created,"Bolt drag lost C reference");
                }
            }else check(created==before,"Redundant axis C changed the bolt sketch");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* d:window.findChildren<QDialog*>())if(d->isVisible())if(auto* buttons=d->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok);ok&&ok->isEnabled())ok->click();
            flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(path);verify(*std::ranges::find(reopened.sketches,sketch.id,&sketcher::Sketch::id));
            view->grabFramebuffer().save(QString::fromStdString((directory/("bolt-center-"+std::to_string(scenario)+".png")).string()));
        }
        std::cout<<"Actual bolt center GUI: both orders, X/Y/Origin, redundant rejection, repeated drags, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
