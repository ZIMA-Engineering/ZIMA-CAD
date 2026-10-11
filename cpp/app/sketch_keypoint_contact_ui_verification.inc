int verify_sketch_keypoint_contact_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const char* names[]{"circle","arc","ellipse","elliptical_arc"};
        for(int kind=0;kind<4;++kind)for(bool reverse_order:{false,true}) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;std::string curve;
            if(kind==0)curve=sketch.add_circle(0,0,8);
            if(kind==1)curve=sketch.add_arc(0,0,8*std::cos(.2),8*std::sin(.2),8*std::cos(6.1),8*std::sin(6.1));
            if(kind==2)curve=sketch.add_ellipse(0,0,8,0,0,4);
            if(kind==3)curve=sketch.add_elliptical_arc(0,0,8,0,0,4,8*std::cos(.2),4*std::sin(.2),8*std::cos(6.1),4*std::sin(6.1));
            for(auto& point:sketch.points)point.fixed=true;
            if(kind<2){auto radius=kind==0?sketch.create_circle_radius_dimension(curve):sketch.create_arc_radius_dimension(curve);radius.locked=true;sketch.apply_dimension(radius);}
            static_cast<void>(sketch.add_segment(-10,2,-15,9));const auto point=sketch.segments.back().first_point_id;
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("C+K");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("keypoint-contact-"+std::to_string(kind)+"-"+std::to_string(reverse_order)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"C+K fixture did not open");flush();
            check(activate_test_body(application,window,body),"C+K Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto open=[&] {
                QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)
                    if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"C+K Sketch Tree row missing");window.show_tree_item_properties(row);flush();
                window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
                QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            };open();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"C+K model view missing");
            const auto mouse=[&](QPointF position,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
                QMouseEvent e(type,position,QPointF(view->mapToGlobal(position.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
            };
            const auto pick=[&](const std::string& key) {
                view->clear_selection();
                std::optional<QPointF> pos;unsigned active{};
                for(int y=16;y<view->height()-16&&!pos;y+=4)for(int x=16;x<view->width()-16;x+=4) {
                    const auto offered=view->selection_candidates_at(QPointF(x,y));
                    for(unsigned n=0;n<offered.size();++n)if(offered[n].owner_id==sketch.id&&offered[n].semantic_key==key){pos=QPointF(x,y);active=n;break;}
                    if(pos)break;
                }
                if(!pos)throw std::runtime_error("C+K candidate missing: "+key);
                mouse(*pos,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                for(unsigned n=0;n<active;++n){mouse(*pos,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);}
                if(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key)
                    throw std::runtime_error("C+K common hover/cycling disagrees: wanted="+key+" actual="+
                        (view->hovered_candidate()?view->hovered_candidate()->semantic_key:"none"));
                mouse(*pos,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                return *pos;
            };
            const std::string key="sketch_curve_keypoint:"+std::string(names[kind])+":"+curve+":2",point_key="point:"+point;
            auto* coincident=window.findChild<QAction*>("sketchCoincidentAction");coincident->trigger();flush();
            if(reverse_order){pick(key);pick(point_key);}else {pick(point_key);pick(key);}
            const auto markers=[&](const char* label){std::vector<std::string> ids;for(const auto& m:view->mesh().constraint_markers)if(m.label==label)ids.push_back(m.reference.semantic_key);return ids;};
            check(markers("K").size()==1&&markers("C").size()==1,"GUI contact did not create separate C and K");
            const auto k=markers("K").front(),c=markers("C").front();
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto equation=[&](bool exact) {
                const auto found=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key==point_key;});
                check(found!=view->mesh().points.end(),"C+K lost its native endpoint");const double x=found->position.x,y=found->position.y,b=kind<2?8:4;
                check(std::abs(x*x/64+y*y/(b*b)-1)<1e-6&&(!exact||std::hypot(x+8,y)<1e-6),"GUI endpoint escaped C/K equations");return found->position;
            };equation(true);
            view->clear_selection();pick(k);QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&window,&remove);flush();
            check(markers("K").empty()&&markers("C")==std::vector<std::string>{c},"GUI Delete K removed C");equation(false);
            window.findChild<QAction*>("undoAction")->trigger();flush();check(markers("K")==std::vector<std::string>{k}&&markers("C")==std::vector<std::string>{c},"C+K Undo lost reference identities");equation(true);
            window.findChild<QAction*>("redoAction")->trigger();flush();check(markers("K").empty()&&markers("C")==std::vector<std::string>{c},"C+K Redo lost independent C");equation(false);
            const auto before=equation(false);auto at=pick(point_key);
            for(int i=0;i<3;++i){mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);const auto to=at+QPointF(4,-8);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);equation(false);at=pick(point_key);}
            const auto after=equation(false);check(std::hypot(after.x-before.x,after.y-before.y)>1e-5,"GUI C-only endpoint did not slide");
            const auto finish=[&]{window.findChild<QAction*>("finishSketchAction")->trigger();flush();for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();};
            finish();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);const auto& result=saved.sketches.front();
            check(std::ranges::none_of(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference;})&&
                std::ranges::any_of(result.constraints,[&](const auto& contact){return "constraint:"+contact.id==c&&contact.geometry_id==curve&&contact.first_point_id==point;}),"C-only contact did not persist");
            open();equation(false);check(markers("C")==std::vector<std::string>{c},"Reopened Sketch lost its retained C marker");finish();
        }
        std::cout<<"C+K GUI: four conics, both selection orders, common hover/cycling, Delete K, identity-preserving Undo/Redo, three endpoint drags, save and reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
