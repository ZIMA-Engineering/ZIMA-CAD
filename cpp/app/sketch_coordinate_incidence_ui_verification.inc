static int verify_sketch_coordinate_incidence_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        for(int kind=0;kind<3;++kind) {
            auto sketch=sketcher::Sketch::create_default();std::string curve,contact,handle;
            const auto coordinate=[&](const std::string& point,bool x,double value) {
                auto d=sketch.create_axis_dimension(point,x?"sketch_axis:y":"sketch_axis:x");d.value=value;sketch.apply_dimension(d);return d.id;
            };
            if(kind==0) {
                curve=sketch.add_segment(20,20,60,80);const auto line=sketch.segments.front();handle=line.second_point_id;
                contact=sketch.add_point(40,50,1e-8,true);static_cast<void>(sketch.add_point_on_line_constraint(contact,curve));
                coordinate(line.first_point_id,true,20);coordinate(line.first_point_id,false,20);coordinate(handle,false,80);coordinate(contact,false,50);
            } else {
                if(kind==1)curve=sketch.add_circle(40,40,15);
                else curve=sketch.add_arc(40,40,40,25,25,40,false,1e-8,false);
                handle=kind==1?sketch.circles.front().center_point_id:sketch.arcs.front().center_point_id;
                if(kind==2) {
                    const auto arc=sketch.arcs.front();
                    static_cast<void>(sketch.add_point_pair_constraint(handle,arc.start_point_id,sketcher::ConstraintKind::Vertical));
                    static_cast<void>(sketch.add_point_pair_constraint(handle,arc.end_point_id,sketcher::ConstraintKind::Horizontal));
                }
                contact=sketch.add_point(55,40,1e-8,true);static_cast<void>(sketch.add_point_on_circle_constraint(contact,curve));
                static_cast<void>(sketch.add_point_pair_constraint(handle,contact,sketcher::ConstraintKind::Horizontal));
                sketch.apply_dimension(kind==1?sketch.create_circle_radius_dimension(curve):sketch.create_arc_radius_dimension(curve));
                coordinate(handle,false,40);
            }
            auto driver=sketch.create_axis_dimension(contact,"sketch_axis:y");driver.placement=std::array{45.,5.};sketch.apply_dimension(driver);
            auto owner=document::PartDocument::create_sketch_container();owner.feature.sketch_id=sketch.id;sketch.owner_container_id=owner.id;
            auto doc=document::PartDocument::create_default();doc.sketches={sketch};doc.history={owner};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Coordinate incidence");graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
            const auto path=directory/("coordinate-incidence-"+std::to_string(kind)+".prtz");doc.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            check(window.open_document_path(QString::fromStdString(path.string())),"Coordinate incidence GUI open failed");flush();
            check(activate_test_body(application,window,body),"Coordinate incidence Body unavailable");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Coordinate incidence Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Coordinate incidence Sketch editor unavailable");open->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Coordinate incidence View unavailable");
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto verify=[&](const auto& value) {
                auto solved=value;check(solved.solve().maximum_residual<1e-8,"GUI incidence residual failed");
                const auto* p=value.find_point(contact);check(p,"GUI lost contact point identity");
                for(const auto& d:value.dimensions)if(d.kind==sketcher::DimensionKind::DistanceX||d.kind==sketcher::DimensionKind::DistanceY) {
                    const auto* point=value.find_point(d.first_point_id);check(point&&std::abs((d.kind==sketcher::DimensionKind::DistanceX?point->x:point->y)-d.value)<1e-8,"GUI coordinate equation failed");
                }
                if(kind==0) {const auto& line=value.segments.front();const auto* a=value.find_point(line.first_point_id);const auto* b=value.find_point(line.second_point_id);
                    check(std::abs((p->x-a->x)*(b->y-a->y)-(p->y-a->y)*(b->x-a->x))<1e-6,"GUI point left its line");}
                else {const auto* c=value.find_point(handle);const auto radius=kind==1?value.circles.front().radius:value.arcs.front().radius;
                    check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-radius)<1e-8&&std::abs(p->y-c->y)<1e-8,"GUI point left its circular support");}
            };
            const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
            };
            for(double delta:{.5,1.,0.}) {
                std::optional<QPointF> hit;
                for(int y=5;y<view->height()-5&&!hit;y+=2)for(int x=5;x<view->width()-5;x+=2) {
                    const auto candidates=view->selection_candidates_at({double(x),double(y)});
                    if(!candidates.empty()&&candidates.front().owner_id==sketch.id&&candidates.front().semantic_key=="dimension:"+driver.id){hit=QPointF(x,y);break;}
                }
                check(hit.has_value(),"GUI coordinate dimension is not pickable");mouse(QEvent::MouseButtonDblClick,*hit,Qt::LeftButton,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor,"GUI coordinate editor did not open");
                editor->setText(QString::number(driver.value+delta,'f',9));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                auto value=stored();verify(value);check(std::abs(value.find_point(contact)->x-driver.value-delta)<1e-8,"GUI coordinate edit did not move its support");
                window.findChild<QAction*>("undoAction")->trigger();flush();verify(stored());window.findChild<QAction*>("redoAction")->trigger();flush();verify(stored());
            }
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"GUI Sketch ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"GUI camera misses Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            for(int step=0;step<3;++step) {
                const auto previous=stored();const auto* p=previous.find_point(handle);const auto from=screen(p->x,p->y),to=screen(p->x+2,p->y);
                view->clear_selection();const auto candidates=view->selection_candidates_at(from);
                check(!candidates.empty()&&candidates.front().semantic_key=="point:"+handle,"GUI native support handle is not first in ordinary picker");
                mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                const auto moved=stored();verify(moved);check(std::hypot(moved.find_point(handle)->x-p->x,moved.find_point(handle)->y-p->y)>1e-3,"GUI support drag did not move");
                window.findChild<QAction*>("undoAction")->trigger();flush();verify(stored());window.findChild<QAction*>("redoAction")->trigger();flush();verify(stored());
            }
            window.grab().save(QString::fromStdString((directory/("coordinate-incidence-"+std::to_string(kind)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dynamic_cast<app::SketchPropertiesDialog*>(dialog))dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();verify(stored());check(window.open_document_path(QString::fromStdString(path.string())),"GUI saved coordinate fixture did not reopen");flush();
            std::cout<<"Coordinate incidence GUI kind="<<kind<<": repeated edits, drags, Undo/Redo and native save/reopen passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
