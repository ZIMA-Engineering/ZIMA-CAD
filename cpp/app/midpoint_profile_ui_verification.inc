static int verify_midpoint_profile_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        std::filesystem::create_directories(directory);
        for(double angle:{0.,14.,-14.}) {
            auto fixture=midpoint_profile_study::make(angle,false,false,0);
            auto& sketch=fixture.sketch;
            for(auto& d:sketch.dimensions)if(d.id==fixture.width)d.placement=std::array{0.,-2.};
                else if(d.id==fixture.height)d.placement=std::array{3.,0.};
            auto owner=document::PartDocument::create_sketch_container();owner.feature.sketch_id=sketch.id;sketch.owner_container_id=owner.id;
            auto doc=document::PartDocument::create_default();doc.sketches={sketch};doc.history={owner};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Midpoint profile");graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
            const auto path=directory/("midpoint-profile-"+std::to_string(static_cast<int>(angle))+".prtz");doc.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            const auto check=midpoint_profile_study::check;
            check(window.open_document_path(QString::fromStdString(path.string())),"Midpoint GUI fixture open failed");flush();
            check(activate_test_body(application,window,body),"Midpoint GUI Body activation failed");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            const auto enter_sketch=[&]{row=nullptr;
                for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"Midpoint GUI Sketch is missing");window.show_tree_item_properties(row);flush();
                auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Midpoint GUI Sketch editor is missing");open->click();flush();
                QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
            };
            const auto leave_sketch=[&]{window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dynamic_cast<app::SketchPropertiesDialog*>(dialog))dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();};
            enter_sketch();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Midpoint GUI View is missing");
            view->fit_points({{-4,-3,0},{4,3,0}});flush();
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto value=[&](const sketcher::Sketch& s,const std::string& id){return std::ranges::find(s.dimensions,id,&sketcher::SketchDimension::id)->value;};
            const auto verify=[&](const sketcher::Sketch& s){auto actual=fixture;actual.sketch=s;midpoint_profile_study::verify(actual,value(s,fixture.width),value(s,fixture.height));};
            const auto same_geometry=[&](const sketcher::Sketch& actual,const sketcher::Sketch& expected){
                verify(actual);check(actual.points.size()==expected.points.size(),"Midpoint GUI Undo/Redo point count differs");
                for(const auto& p:expected.points){const auto* q=actual.find_point(p.id);check(q,"Midpoint GUI Undo/Redo lost point identity");midpoint_profile_study::near(q->x,p.x);midpoint_profile_study::near(q->y,p.y);}
                for(const auto& d:expected.dimensions)midpoint_profile_study::near(value(actual,d.id),d.value);
            };
            const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            for(const auto& id:{fixture.width,fixture.height})for(double ratio:{1.01,1.02,1.}) {
                const auto before=stored();
                const double target=(id==fixture.width?2.9:.8)*ratio;
                std::optional<QPointF> hit;
                for(int y=5;y<view->height()-5&&!hit;y+=2)for(int x=5;x<view->width()-5;x+=2) {
                    const auto candidates=view->selection_candidates_at({double(x),double(y)});
                    if(!candidates.empty()&&candidates.front().owner_id==sketch.id&&candidates.front().semantic_key=="dimension:"+id){hit=QPointF(x,y);break;}
                }
                if(!hit)window.grab().save(QString::fromStdString((directory/"midpoint-label-failure.png").string()));
                check(hit.has_value(),"Midpoint GUI width label is not pickable");mouse(QEvent::MouseButtonDblClick,*hit,Qt::LeftButton,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor,"Midpoint GUI width editor did not open");
                editor->setText(QString::number(target,'f',9));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                auto s=stored();verify(s);midpoint_profile_study::near(value(s,id),target);
                window.findChild<QAction*>("undoAction")->trigger();flush();same_geometry(stored(),before);window.findChild<QAction*>("redoAction")->trigger();flush();same_geometry(stored(),s);
            }
            const auto set=[&](const std::string& id,bool locked,bool driving){const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command","sketch.dimension.set"},{"arguments",{{"sketch",sketch.id},{"dimension",id},{"locked",locked},{"driving",driving}}}}.dump()));if(!result.ok)throw std::runtime_error("Midpoint GUI dimension state: "+result.code+": "+result.message);flush();};
            leave_sketch();
            for(const auto& d:sketch.dimensions)set(d.id,true,true);
            enter_sketch();view->fit_points({{-4,-3,0},{4,3,0}});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Midpoint GUI ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Midpoint GUI plane missing");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto drag=[&](const sketcher::Sketch& s){const auto* p=s.find_point(fixture.vertices[2]);const double r=angle*std::numbers::pi/180.;const auto from=screen(p->x,p->y),to=screen(p->x-.3*std::sin(r),p->y+.3*std::cos(r));
                check(std::hypot(to.x()-from.x(),to.y()-from.y())>QApplication::startDragDistance(),"Midpoint GUI drag does not cross the system drag threshold");
                view->clear_selection();const auto candidates=view->selection_candidates_at(from);check(!candidates.empty()&&candidates.front().semantic_key=="point:"+p->id,"Midpoint GUI corner is not first in picker");
                mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);};
            const auto locked=stored();drag(locked);auto s=stored();verify(s);check(s.points==locked.points&&s.dimensions==locked.dimensions,"Locked midpoint GUI profile moved");
            leave_sketch();set(fixture.height,false,false);enter_sketch();view->fit_points({{-4,-3,0},{4,3,0}});flush();
            for(int step=0;step<3;++step) {
                const auto before=stored();drag(before);s=stored();verify(s);midpoint_profile_study::near(value(s,fixture.width),2.9);
                check(value(s,fixture.height)>value(before,fixture.height)+.01,"Free midpoint GUI profile did not move");
                window.findChild<QAction*>("undoAction")->trigger();flush();same_geometry(stored(),before);window.findChild<QAction*>("redoAction")->trigger();flush();same_geometry(stored(),s);
            }
            window.grab().save(QString::fromStdString((directory/("midpoint-profile-"+std::to_string(static_cast<int>(angle))+".png")).string()));
            leave_sketch();
            flush();verify(stored());check(window.open_document_path(QString::fromStdString(path.string())),"Midpoint GUI saved profile did not reopen");flush();
            std::cout<<"Midpoint profile GUI angle="<<angle<<": repeated width edits, locked/free drags, Undo/Redo and native reopen passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
