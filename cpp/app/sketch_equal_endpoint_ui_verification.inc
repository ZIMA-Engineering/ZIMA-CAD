int verify_sketch_equal_endpoint_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        int cases=0;
        for(bool on_axis:{true,false})for(bool vertical:{false,true})for(double sign:{1.,-1.}) {
            std::cout<<"Equal endpoint case axis="<<on_axis<<" vertical="<<vertical<<" sign="<<sign<<std::endl;
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            const std::array<double,2> start=on_axis?std::array{0.,0.}:std::array{10.,-15.};
            const auto support=sketch.add_segment(start[0],start[1],start[0]+(vertical?40.:0.),start[1]+(vertical?0.:40.));
            static_cast<void>(sketch.add_segment_constraint(support,vertical?sketcher::ConstraintKind::Horizontal:sketcher::ConstraintKind::Vertical));
            const auto corner=sketch.segments.front().first_point_id;
            const auto contact=on_axis?sketch.add_point_reference_constraint(corner,"sketch_origin"):std::string{};
            static_cast<void>(sketch.add_point(-80,-80));static_cast<void>(sketch.add_point(90,90));
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Equal endpoint inference");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("equal-endpoint-"+std::to_string(cases++)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Equal endpoint fixture failed to open");flush();
            check(activate_test_body(application,window,body),"Equal endpoint Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Equal endpoint Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Equal endpoint open button missing");open->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Equal endpoint View missing");
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();click(screen(start[0],start[1]));
            const auto endpoint=screen(start[0]+(vertical?0.:40.2*sign),start[1]+(vertical?40.2*sign:0.));
            mouse(endpoint,QEvent::MouseMove,Qt::NoButton);
            const auto offers_equal=[&]{return std::ranges::any_of(view->transient_labels(),[](const auto& label){return label.second.find('=')!=std::string::npos;});};
            // A higher-priority M may require a different length. Keep that
            // valid alternative and exercise the ordinary inference cycle.
            for(int cycle=0;cycle<32&&!offers_equal();++cycle) {
                mouse(endpoint,QEvent::MouseButtonPress,Qt::RightButton);
                mouse(endpoint,QEvent::MouseButtonRelease,Qt::RightButton);
            }
            view->grabFramebuffer().save(QString::fromStdString((directory/("equal-preview-"+std::to_string(cases)+".png")).string()));
            check(offers_equal(),"Second endpoint did not offer Equal alongside H/V or axis C");
            check(!view->transient_edges().empty(),"Equal endpoint preview missing");
            const auto preview=view->transient_edges().front().points;
            check(preview.size()==2&&std::abs(std::hypot(preview[1].x-preview[0].x,preview[1].y-preview[0].y)-40)<1e-7,"Equal preview did not snap to exact length");
            // Confirm the currently displayed variant without injecting a
            // new pointer move, which deliberately resets inference cycling.
            mouse(endpoint,QEvent::MouseButtonPress,Qt::LeftButton);
            mouse(endpoint,QEvent::MouseButtonRelease,Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(std::ranges::any_of(view->mesh().constraint_markers,[](const auto& marker){return marker.label=="=";}),"Confirmed segment lost Equal marker");
            window.findChild<QAction*>("sketchCornerFilletAction")->trigger();flush();
            click(screen(start[0]+(vertical?20.:0.),start[1]+(vertical?0.:20.)));
            click(screen(start[0]+(vertical?0.:20.*sign),start[1]+(vertical?20.*sign:0.)));
            bool accepted=false;
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();accepted=true;break;}
            check(accepted,"Corner fillet dialog was not offered");
            check(std::ranges::any_of(view->mesh().constraint_markers,[](const auto& marker){return marker.label=="=";}),"Corner fillet hid Equal marker");
            if(on_axis)check(std::ranges::any_of(view->mesh().constraint_markers,[&](const auto& marker){return marker.reference.semantic_key=="constraint:"+contact;}),"Corner fillet hid original origin contact marker");
            const auto point_position=[&](const std::string& id) {
                for(const auto& point:view->mesh().points)if(point.reference.semantic_key=="point:"+id)return point.position;
                throw std::runtime_error("Rounded design point disappeared from GUI");
            };
            for(double length:{44.,46.,42.}) {
                const auto before=point_position(sketch.segments.front().second_point_id);
                const auto begin=screen(before.x,before.y);
                const auto end=screen(start[0]+(vertical?length:0.),start[1]+(vertical?0.:length));
                mouse(begin,QEvent::MouseMove,Qt::NoButton);mouse(begin,QEvent::MouseButtonPress,Qt::LeftButton);
                QMouseEvent move(QEvent::MouseMove,end,QPointF(view->mapToGlobal(end.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
                mouse(end,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=point_position(sketch.segments.front().second_point_id);
                check(std::hypot(after.x-before.x,after.y-before.y)>.1,"Permitted rounded design endpoint did not move in GUI");
                check(std::ranges::any_of(view->mesh().constraint_markers,[](const auto& marker){return marker.label=="=";}),"Dragging a rounded design hid Equal marker");
                if(on_axis)check(std::hypot(point_position(corner).x,point_position(corner).y)<1e-7,"Rounded design GUI drag escaped its origin contact");
            }
            view->grabFramebuffer().save(QString::fromStdString((directory/("equal-corner-"+std::to_string(cases)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"Equal corner save failed");
            const auto saved=document::PartDocument::load(path);const auto& result=saved.sketches.front();
            check(result.segments.size()==2&&result.corner_radii.size()==1,"Equal corner topology was not committed");
            check(std::ranges::any_of(result.constraints,[](const auto& relation){return relation.kind==sketcher::ConstraintKind::EqualLength;}),"Equal relation did not survive native reopen");
            check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Equal corner Undo/Redo failed");
            check(document::PartDocument::load(path).sketches.front().serialized()==result.serialized(),"Equal corner Undo/Redo changed source identities");
        }
        std::cout<<"Eight perpendicular shared-point Equal endpoint and corner marker GUI cases passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
