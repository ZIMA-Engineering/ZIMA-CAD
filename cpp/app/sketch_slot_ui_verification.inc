int verify_sketch_slot_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(int orientation:{0,1,2}) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            std::array<double,2> first{10.,-15.},second{10.,15.};
            std::string symmetry="sketch_axis:x";
            if(orientation==1){first={-15.,10.};second={15.,10.};symmetry="sketch_axis:y";}
            if(orientation==2) {
                first={25.,10.};second={10.,25.};
                symmetry=sketch.add_segment(-20,-20,30,30);sketch.set_geometry_construction(symmetry,true);
            }
            part.history={feature};part.sketches={sketch};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Slot test");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
            const auto path=directory/("slot-"+std::to_string(orientation)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open slot fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate slot Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Cannot locate slot Sketch");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Slot View missing");
            auto camera=view->camera_state();camera[4]=60;camera[5]=camera[6]=0;camera[7]=60;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Slot ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Slot camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](std::array<double,2> p){const auto at=screen(p);mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* slot=window.findChild<QAction*>("sketchSlotAction");check(slot&&slot->isEnabled(),"Slot unavailable");slot->trigger();flush();
            click(first);click(second);
            const double dx=second[0]-first[0],dy=second[1]-first[1],length=std::hypot(dx,dy);
            const std::array radius_point{second[0]-dy/length*5,second[1]+dx/length*5};
            mouse(screen(radius_point),QEvent::MouseMove,Qt::NoButton);
            view->grab().save(QString::fromStdString((directory/("slot-preview-"+std::to_string(orientation)+".png")).string()));
            click(radius_point);
            // Escape discards the next unfinished slot, without deleting the
            // already committed four-curve profile or creating a transaction.
            click({-22.,-17.});
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};
            for(auto* d:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::SketchPropertiesDialog*>(d);candidate&&candidate->isVisible())properties=candidate;
            check(properties,"Slot did not return to Sketch Properties");properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            auto saved=document::PartDocument::load(path);auto& result=saved.sketches.front();
            check(result.arcs.size()==2&&result.segments.size()==(orientation==2?3:2),"Slot did not commit exactly four profile curves");
            check(std::abs(result.arcs[0].radius-5)<1e-5&&std::abs(result.arcs[1].radius-5)<1e-5,"Slot cursor did not define its perpendicular radius");
            check(std::ranges::any_of(result.constraints,[&](const auto& value){return value.kind==sketcher::ConstraintKind::Symmetric&&value.geometry_id==symmetry;}),"Slot lost offered centre symmetry");
            check(result.solve().maximum_residual<1e-8,"Slot does not solve after save/reopen");
        }
        std::cout<<"Slot mouse input, radius preview, centre symmetry on both axes and an oblique construction line, Escape and save/reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
