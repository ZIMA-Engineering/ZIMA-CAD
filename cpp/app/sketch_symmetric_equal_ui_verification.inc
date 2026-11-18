int verify_sketch_symmetric_equal_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        int cases=0;
        for(int side=0;side<4;++side)for(bool reverse:{false,true}) {
            auto part=document::PartDocument::create_default();
            auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            const auto sides=sketch.add_rectangle(-33.322933197021484,29.633331298828125,
                33.322933197021484,-29.633331298828125);
            static_cast<void>(sketch.add_symmetric_constraint(sketch.segments[0].first_point_id,
                sketch.segments[0].second_point_id,"sketch_axis:y"));
            static_cast<void>(sketch.add_symmetric_constraint(sketch.segments[1].first_point_id,
                sketch.segments[1].second_point_id,"sketch_axis:x"));
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Symmetric equality");graph.insert({document::PartHistoryKind::Feature,feature.id});
            part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("symmetric-equal-"+std::to_string(cases++)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Symmetric equality fixture failed to open");flush();
            check(activate_test_body(application,window,body),"Symmetric equality Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Symmetric equality Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Sketch open button missing");open->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Symmetric equality View missing");
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);};
            const auto point=[&](const std::string& id){
                for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+id)return std::array{p.position.x,p.position.y};
                throw std::runtime_error("Symmetric equality point identity disappeared");
            };
            const auto equations=[&]{
                const auto p=point(sketch.points[0].id),q=point(sketch.points[1].id),r=point(sketch.points[2].id),s=point(sketch.points[3].id);
                const auto near=[&](double x,double y){check(std::abs(x-y)<1e-6,"GUI symmetric square equation failed");};
                near(p[1],q[1]);near(q[0],r[0]);near(r[1],s[1]);near(s[0],p[0]);
                near(p[0],-q[0]);near(q[1],-r[1]);near(std::abs(q[0]-p[0]),std::abs(r[1]-q[1]));
                check(std::ranges::any_of(view->mesh().constraint_markers,[](const auto& marker){return marker.label=="=";}),"GUI equality marker missing");
            };
            const auto pick_side=[&](int index){
                const auto& segment=sketch.segments[index];const auto p=point(segment.first_point_id),q=point(segment.second_point_id);
                const auto position=screen((p[0]+q[0])*.5,(p[1]+q[1])*.5);
                const auto candidates=view->selection_candidates_at(position);
                check(!candidates.empty()&&candidates.front().semantic_key=="segment:"+segment.id,"Common picker did not offer the equal side");click(position);
            };
            window.findChild<QAction*>("sketchEqualAction")->trigger();flush();
            pick_side(reverse?(side+1)%4:side);pick_side(reverse?side:(side+1)%4);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();equations();
            for(double factor:{1.1,.9,1.05}){
                const auto before=point(sketch.points[side].id);const auto begin=screen(before[0],before[1]),end=screen(before[0]*factor,before[1]*factor);
                mouse(begin,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(begin,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(end,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(end,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                const auto after=point(sketch.points[side].id);check(std::hypot(after[0]-before[0],after[1]-before[1])>.1,"Symmetric square GUI drag did not move");equations();
            }
            window.findChild<QAction*>("sketchDimensionAction")->trigger();flush();pick_side(0);
            const auto top=point(sketch.points[0].id);click(screen(0,top[1]+12));QApplication::sendEvent(view,&escape);flush();
            check(!view->mesh().dimensions.empty(),"Symmetric square GUI dimension was not created");
            for(double value:{72.,58.,66.}){
                std::optional<QPointF> hit;
                for(int y=8;y<view->height()-8&&!hit;y+=4)for(int x=8;x<view->width()-8&&!hit;x+=4){
                    const QPointF p(x,y);const auto candidates=view->selection_candidates_at(p);
                    if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Dimension)hit=p;
                }
                check(hit.has_value(),"Symmetric dimension was not pickable");
                mouse(*hit,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
                mouse(*hit,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field,"Symmetric dimension double-click did not open editor");
                field->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();equations();
                const auto p=point(sketch.points[0].id),q=point(sketch.points[1].id);check(std::abs(q[0]-p[0]-value)<1e-6,"Symmetric square GUI dimension did not drive width");
            }
            view->grabFramebuffer().save(QString::fromStdString((directory/("symmetric-equal-"+std::to_string(cases)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"Symmetric equal save failed");
            const auto saved=document::PartDocument::load(path);const auto& result=saved.sketches.front();
            check(result.points.size()==4&&result.segments==sketch.segments,"Symmetric equality changed source identities");
            check(result.constraints.size()==7&&result.dimensions.size()==1,"Symmetric equality persistence lost relations");
            check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Symmetric equality Undo/Redo failed");
            check(document::PartDocument::load(path).sketches.front().serialized()==result.serialized(),"Symmetric equality Undo/Redo changed persisted state");
        }
        std::cout<<"Eight symmetric rectangle GUI equality cases passed: both selection orders, all adjacent sides, repeated drags, dimension creation and double-click edits, native reopen and Undo/Redo\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
