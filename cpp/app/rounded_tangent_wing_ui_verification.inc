static int verify_rounded_tangent_wing_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        nlohmann::json fixture;std::ifstream input(std::filesystem::path(__FILE__).parent_path()/"../tests/fixtures/sketch/rounded-tangent-wing.json");input>>fixture;
        for(int mode=0;mode<3;++mode) {
            auto sketch=sketcher::Sketch::from_serialized_json(fixture);
            for(auto& d:sketch.dimensions)d.locked=mode!=0;
            if(mode==2)std::erase_if(sketch.dimensions,[](const auto& d){return d.id=="landing-x";});
            auto owner=document::PartDocument::create_sketch_container();owner.feature.sketch_id=sketch.id;sketch.owner_container_id=owner.id;
            auto doc=document::PartDocument::create_default();doc.sketches={sketch};doc.history={owner};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Rounded tangent wing");graph.insert({document::PartHistoryKind::Feature,owner.id});doc.set_body_history(graph);doc.resolve_constructions();
            const auto path=directory/("rounded-tangent-wing-"+std::to_string(mode)+".prtz");doc.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Rounded wing GUI open failed");flush();
            check(activate_test_body(application,window,body),"Rounded wing Body unavailable");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Rounded wing Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Rounded wing Sketch editor unavailable");open->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Rounded wing View unavailable");
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto verify=[&]{const auto value=stored();rounded_tangent_wing_study::equations(value);return value;};
            const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
            };
            for(const std::string driver:{"span","height","crown-radius","corner-radius","flank-angle"}) {
                const auto initial=*std::ranges::find(sketch.dimensions,driver,&sketcher::SketchDimension::id);
                for(double scale:{1.02,1.04,1.}) {
                    const auto previous=verify();std::optional<QPointF> hit;
                    for(int y=5;y<view->height()-5&&!hit;y+=2)for(int x=5;x<view->width()-5;x+=2) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(!candidates.empty()&&candidates.front().owner_id==sketch.id&&candidates.front().semantic_key=="dimension:"+driver){hit=QPointF(x,y);break;}
                    }
                    if(!hit)throw std::runtime_error("Rounded wing GUI dimension is not pickable: "+driver);
                    mouse(QEvent::MouseButtonDblClick,*hit,Qt::LeftButton,Qt::LeftButton);
                    auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor,"Rounded wing inline editor did not open");
                    editor->setText(QString::number(initial.value*scale,'f',9));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                    auto value=verify();const auto current=std::ranges::find(value.dimensions,driver,&sketcher::SketchDimension::id);
                    check(current!=value.dimensions.end()&&std::abs(current->value-initial.value*scale)<1e-8,"Rounded wing GUI edit did not reach requested value");
                    window.findChild<QAction*>("undoAction")->trigger();flush();const auto undone=verify();
                    check(undone.points==previous.points&&undone.arcs==previous.arcs,"Rounded wing GUI Undo changed input geometry");
                    window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=verify();
                    check(redone.points==value.points&&redone.arcs==value.arcs,"Rounded wing GUI Redo did not restore geometry");
                }
                std::cout<<"Rounded wing GUI mode="<<mode<<" driver="<<driver<<" passed"<<std::endl;
            }
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Rounded wing GUI ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Rounded wing GUI camera misses Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            for(double delta:{.2,-.2,.3}) {
                const auto previous=verify();const auto p=*previous.find_point("landing");const auto from=screen(p.x,p.y),to=screen(p.x+delta,p.y);
                view->clear_selection();const auto candidates=view->selection_candidates_at(from);
                check(!candidates.empty()&&candidates.front().semantic_key=="point:landing","Rounded wing handle is not first in ordinary picker");
                mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
                const auto moved=verify();const auto* q=moved.find_point("landing");
                if(mode==1)check(moved.points==previous.points&&moved.arcs==previous.arcs,"Fully locked GUI wing escaped its constraints");
                else {
                    check(std::hypot(q->x-p.x,q->y-p.y)>1e-3,"Free rounded wing GUI handle did not move");
                    window.findChild<QAction*>("undoAction")->trigger();flush();verify();window.findChild<QAction*>("redoAction")->trigger();flush();verify();
                }
            }
            window.grab().save(QString::fromStdString(document::path_to_utf8((directory/("rounded-tangent-wing-"+std::to_string(mode)+".png")))));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dynamic_cast<app::SketchPropertiesDialog*>(dialog))dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            flush();verify();check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Saved rounded wing GUI fixture did not reopen");flush();verify();
            std::cout<<"Rounded tangent wing GUI mode="<<mode<<": coordinate, crown/corner R and angle edits, drags, Undo/Redo and native save/reopen passed"<<std::endl;
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
