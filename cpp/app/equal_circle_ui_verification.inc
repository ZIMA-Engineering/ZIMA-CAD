static int verify_equal_circle_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto near=[&](double a,double b){check(std::abs(a-b)<1e-7,"Equal-circle GUI equation differs");};
        const auto source=std::getenv("ZIMA_VERIFY_EQUAL_CIRCLE_SOURCE")
            ?std::filesystem::u8path(std::getenv("ZIMA_VERIFY_EQUAL_CIRCLE_SOURCE"))
            :std::filesystem::path(QCoreApplication::applicationDirPath().toStdString())/"equal-circle-fixture.prtz";
        auto doc=document::PartDocument::load(source);check(doc.sketches.size()==1,"Equal-circle fixture must have one Sketch");
        auto sketch=doc.sketches.front();check(sketch.circles.size()==3,"Equal-circle fixture must have three circles");
        std::filesystem::create_directories(directory);const auto path=directory/"equal-circles.prtz";doc.save(path);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        check(window.open_document_path(QString::fromStdString(path.string())),"Equal-circle GUI open failed");flush();
        check(activate_test_body(application,window,doc.body_history.active_body_id()),"Equal-circle GUI Body activation failed");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto enter_sketch=[&]{QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Equal-circle GUI Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* button=window.findChild<QPushButton*>("sketchOpenButton");if(!button)button=window.findChild<QPushButton*>("featureSketchButton");
            if(!button)button=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            if(button){button->click();flush();}
            if(!window.findChild<QAction*>("finishSketchAction")->isEnabled())window.grab().save(QString::fromStdString((directory/"equal-circle-entry-failure.png").string()));
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Equal-circle GUI Sketch did not enter editing");
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();flush();
        };
        const auto leave_sketch=[&]{window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dialog->findChild<QDialogButtonBox*>())
                if(auto* ok=dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);ok&&ok->isEnabled())ok->click();flush();};
        enter_sketch();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Equal-circle GUI View missing");
        view->fit_points({{-280,-210,0},{280,150,0}});flush();
        const auto stored=[&]{
            if(window.findChild<QAction*>("finishSketchAction")->isEnabled()) {
                // An owned primitive Sketch is a transient properties draft.
                // Read its actual published geometry until OK commits it.
                auto current=sketch;
                for(auto& point:current.points) {
                    const auto shown=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+point.id;});
                    check(shown!=view->mesh().points.end(),"Equal-circle GUI draft point missing");
                    const auto local=sketch.local_point(shown->position);point.x=local[0];point.y=local[1];
                }
                for(auto& d:current.dimensions) {
                    const auto shown=std::ranges::find_if(view->mesh().dimensions,[&](const auto& value){return value.reference.owner_id==sketch.id&&value.reference.semantic_key=="dimension:"+d.id;});
                    check(shown!=view->mesh().dimensions.end(),"Equal-circle GUI draft dimension missing");d.value=shown->value;d.driving=shown->driving;d.locked=shown->locked;
                }
                return current;
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();
        };
        const auto verify=[&](const sketcher::Sketch& s){
            check(s.points.size()==sketch.points.size()&&s.circles.size()==3,"Equal-circle GUI identity count differs");
            for(const auto& circle:sketch.circles)check(std::ranges::find(s.circles,circle.id,&sketcher::SketchCircle::id)!=s.circles.end(),"Equal-circle GUI lost circle identity");
            near(s.circles[0].radius,s.circles[1].radius);near(s.circles[1].radius,s.circles[2].radius);
            for(const auto& d:s.dimensions)if(d.driving&&!d.suppressed) {
                const auto* a=s.find_point(d.first_point_id);const auto* b=s.find_point(d.second_point_id);
                if(d.kind==sketcher::DimensionKind::Distance)near(std::hypot(b->x-a->x,b->y-a->y),d.value);
                if(d.kind==sketcher::DimensionKind::DistanceX)near(b->x-a->x,d.value);
            }
            const auto* a=s.find_point(s.circles[0].center_point_id);const auto* b=s.find_point(s.circles[1].center_point_id);
            near(a->y,0);near(b->y,0);near(a->x,-b->x);
        };
        const auto same=[&](const sketcher::Sketch& a,const sketcher::Sketch& b){verify(a);
            for(const auto& p:b.points){const auto* q=a.find_point(p.id);check(q,"Equal-circle GUI lost point identity");near(p.x,q->x);near(p.y,q->y);}
            for(const auto& d:b.dimensions)near(std::ranges::find(a.dimensions,d.id,&sketcher::SketchDimension::id)->value,d.value);};
        const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        for(const auto& original:sketch.dimensions)for(double factor:{1.01,1.02,1.}) {
            const auto before=stored();std::optional<QPointF> hit;
            for(int y=5;y<view->height()-5&&!hit;y+=2)for(int x=5;x<view->width()-5;x+=2) {
                const auto candidates=view->selection_candidates_at({double(x),double(y)});
                if(!candidates.empty()&&candidates.front().owner_id==sketch.id&&candidates.front().semantic_key=="dimension:"+original.id){hit=QPointF(x,y);break;}
            }
            check(hit.has_value(),"Equal-circle GUI dimension label not pickable");mouse(QEvent::MouseButtonDblClick,*hit,Qt::LeftButton,Qt::LeftButton);
            auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor,"Equal-circle GUI value editor missing");
            editor->setText(QString::number(original.value*factor,'f',9));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            const auto after=stored();verify(after);near(std::ranges::find(after.dimensions,original.id,&sketcher::SketchDimension::id)->value,original.value*factor);
            window.findChild<QAction*>("undoAction")->trigger();flush();same(stored(),before);window.findChild<QAction*>("redoAction")->trigger();flush();same(stored(),after);
        }
        leave_sketch();
        for(const auto& d:sketch.dimensions) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command","sketch.dimension.set"},{"arguments",{{"sketch",sketch.id},{"dimension",d.id},{"locked",true}}}}.dump()));
            check(result.ok,"Equal-circle GUI dimension locking failed");flush();
        }
        enter_sketch();view->fit_points({{-280,-210,0},{280,150,0}});flush();
        const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Equal-circle GUI ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Equal-circle GUI plane missing");return *q;};
        const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
        const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
        const auto handle=sketch.circles[2].center_point_id;
        for(int step=0;step<3;++step) {
            const auto before=stored();const auto p=*before.find_point(handle);const auto from=screen(p.x,p.y),to=screen(p.x+20,p.y+10);
            view->clear_selection();const auto candidates=view->selection_candidates_at(from);check(!candidates.empty()&&candidates.front().semantic_key=="point:"+handle,"Equal-circle GUI centre not first in picker");
            mouse(QEvent::MouseMove,from,Qt::NoButton,Qt::NoButton);mouse(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,to,Qt::NoButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,to,Qt::LeftButton,Qt::NoButton);
            const auto after=stored();verify(after);const auto* q=after.find_point(handle);check(std::hypot(q->x-p.x,q->y-p.y)>1e-4,"Equal-circle GUI available centre did not move");
            window.findChild<QAction*>("undoAction")->trigger();flush();same(stored(),before);window.findChild<QAction*>("redoAction")->trigger();flush();same(stored(),after);
        }
        window.grab().save(QString::fromStdString((directory/"equal-circles.png").string()));leave_sketch();verify(stored());
        check(window.open_document_path(QString::fromStdString(path.string())),"Equal-circle GUI reopen failed");flush();verify(stored());
        std::cout<<"Equal-circle GUI: six dimension edits, three locked-distance centre drags, equations, identities, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
