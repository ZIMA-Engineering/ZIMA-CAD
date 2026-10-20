int verify_external_curve_centers_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        unsigned scenarios=0;
        for(unsigned kind=0;kind<4;++kind)for(unsigned mode=0;mode<3;++mode) {
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_EXTERNAL_CENTER_KIND")&&qEnvironmentVariableIntValue("ZIMA_VERIFY_EXTERNAL_CENTER_KIND")!=int(kind))continue;
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_EXTERNAL_CENTER_MODE")&&qEnvironmentVariableIntValue("ZIMA_VERIFY_EXTERNAL_CENTER_MODE")!=int(mode))continue;
            std::cout<<"External center GUI kind="<<kind<<" mode="<<mode<<std::endl;
            ++scenarios;
            auto part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
            const std::array<std::string,4> prefixes{"circle:","arc:","ellipse:","elliptical_arc:"};
            std::string curve;
            if(kind==0)curve=source.add_circle(2,3,5);
            if(kind==1)curve=source.add_arc(2,3,7,3,2,8);
            if(kind==2)curve=source.add_ellipse(2,3,9,3,2,6);
            if(kind==3)curve=source.add_elliptical_arc(2,3,9,3,2,6,9,3,2,6);
            auto source_owner=document::PartDocument::create_sketch_container();source.owner_container_id=source_owner.id;source_owner.feature.sketch_id=source.id;
            auto target=sketcher::Sketch::create_default();std::string circle,center;
            if(mode<2){circle=target.add_circle(15,14,2);center=target.circles.front().center_point_id;}
            std::string radius_dimension;
            if(kind==2&&mode==0){auto d=target.create_circle_radius_dimension(circle);d.driving=true;radius_dimension=d.id;target.apply_dimension(d);}
            auto owner=document::PartDocument::create_sketch_container();target.owner_container_id=owner.id;owner.feature.sketch_id=target.id;
            auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
            reference.source_document_id=part.document_id;reference.source_owner_id=source.id;reference.source_semantic_key=prefixes[kind]+curve;
            workspace::populate_external_reference_cache(target,reference,source.placement_reference_geometry());target.add_external_reference(reference);
            const auto center_id=sketcher::external_center_point_id(reference.id);
            part.history={source_owner,owner};part.sketches={source,target};
            document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Source"));graph.insert({document::PartHistoryKind::Feature,source_owner.id});
            const auto body=graph.create_body("Center test");graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
            kernel::OcctKernel kernel;const auto file=directory/("external-center-"+std::to_string(kind)+"-"+std::to_string(mode)+".prtz");
            part.save(file,kernel.evaluate_history(part.kernel_operations()));
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(file.string())),"Cannot open center GUI fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate center test Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==target.id&&(*it)->data(0,Qt::UserRole+3).toString()=="part-sketch"){row=*it;break;}
            check(row,"Center test Sketch missing from Tree");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Cannot enter center Sketch");open->click();flush();
            QEventLoop settled;QTimer::singleShot(950,&settled,&QEventLoop::quit);settled.exec();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Center test View missing");
            view->fit_points({target.world_point(-12,-8),target.world_point(23,22)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Center camera ray missing");const auto q=target.intersect_ray(ray->first,ray->second);check(q.has_value(),"Center camera is not facing Sketch");return *q;};
            const auto screen=[&](double x,double y){const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,d=ux*vy-uy*vx;return QPointF(((x-a[0])*vy-(y-a[1])*vx)/d,(ux*(y-a[1])-uy*(x-a[0]))/d);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](double x,double y){const auto p=screen(x,y);mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto escape=[&]{QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&event);flush();};
            const auto read=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();const auto doc=document::PartDocument::load(file);const auto found=std::ranges::find(doc.sketches,target.id,&sketcher::Sketch::id);check(found!=doc.sketches.end(),"Saved center Sketch missing");return *found;};
            if(mode==2) {
                click(2,3);
                check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==reference.id,"Selecting center did not select owning external reference in Tree");
                QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(&window,&remove);flush();
                check(read().external_references.empty(),"Deleting center selection did not remove its owning reference");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(read().external_references==target.external_references,"Undo did not restore center's owning reference");
                escape();
            }
            if(mode<2) {
                window.findChild<QAction*>("sketchCoincidentAction")->trigger();flush();
                mouse(screen(2,3),QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="external_point:"+center_id,"Common picker does not offer conic center cross");
                if(mode==0){click(15,14);click(2,3);}else{click(2,3);click(15,14);}escape();
                auto result=read();const auto* p=result.find_point(center);check(p&&std::hypot(p->x-2,p->y-3)<1e-7&&result.circles.front().radius==2,"Mouse C did not join only the two centers");
                if(kind<2) {
                    window.findChild<QAction*>("sketchEqualAction")->trigger();flush();
                    const double q=5/std::sqrt(2.);
                    if(mode==0){click(4,3);click(2+q,3+q);}else{click(2+q,3+q);click(4,3);}escape();
                    result=read();check(std::abs(result.circles.front().radius-5)<1e-7,"Mouse = did not copy external radius in both selection orders");
                }
            } else {
                window.findChild<QAction*>("sketchCircleAction")->trigger();flush();click(2,3);
                if(kind<2){const double q=5/std::sqrt(2.);click(2+q,3+q);}else click(4,3);
                escape();const auto result=read();check(result.circles.size()==1,"Center/rim snapping did not create one circle");
                const auto* p=result.find_point(result.circles.front().center_point_id);check(p&&std::hypot(p->x-2,p->y-3)<1e-7,"Automatic circle center did not snap");
                check(std::ranges::count_if(result.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointReference&&c.second_point_id==center_id;})==1,"Automatic center lost or duplicated its point binding");
                if(kind<2) {
                    check(std::ranges::count_if(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::EqualRadius;})==1,"Concentric rim snap did not create =");
                    check(result.points.size()==1,"Concentric equality created an unnecessary contact point");
                }
            }
            const auto completed=read();const unsigned transactions=mode==2?1:kind<2?2:1;
            for(unsigned i=0;i<transactions;++i){window.findChild<QAction*>("undoAction")->trigger();flush();}
            const auto undone=read();check(undone.circles.size()==target.circles.size()&&undone.constraints==target.constraints,"Center GUI Undo did not restore original Sketch");
            for(unsigned i=0;i<transactions;++i){window.findChild<QAction*>("redoAction")->trigger();flush();}
            const auto restored=read();check(restored.points==completed.points&&restored.circles==completed.circles&&restored.constraints==completed.constraints,"Center GUI Redo/save changed persisted identities");
            if(!radius_dimension.empty()) {
                for(double value:{2.5,1.5,2.}) {
                    viewer::ViewerCandidate dimension{viewer::CandidateKind::Dimension,0,0,target.id,"dimension:"+radius_dimension};
                    bool found=false;for(std::size_t i=0;i<view->mesh().dimensions.size();++i){dimension.geometry_index=i;if(view->candidate_dimension_value(dimension)){found=true;break;}}
                    check(found,"Centered circle radius dimension is absent from View");
                    window.edit_dimension_inline(dimension);flush();auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                    check(editor&&editor->isVisible(),"Centered circle radius editor did not open");editor->setText(QString::number(value));
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                    const auto result=read();check(std::abs(result.circles.front().radius-value)<1e-8,"GUI radius edit did not change centered circle");
                    const auto* p=result.find_point(center);check(p&&std::hypot(p->x-2,p->y-3)<1e-8,"GUI radius edit moved source-bound center");
                    const auto start=screen(2,3),end=screen(3,4);mouse(start,QEvent::MouseMove,Qt::NoButton);
                    check(view->hovered_candidate()&&view->hovered_candidate()->kind==viewer::CandidateKind::SketchPoint,"Bound native center cannot be picked for drag");
                    mouse(start,QEvent::MouseButtonPress,Qt::LeftButton);
                    QMouseEvent drag(QEvent::MouseMove,end,QPointF(view->mapToGlobal(end.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&drag);flush();
                    mouse(end,QEvent::MouseButtonRelease,Qt::LeftButton);
                    const auto after=read();check(after.points==result.points&&after.circles==result.circles&&after.constraints==result.constraints,"Mouse drag escaped source-bound circle center");
                }
                for(unsigned i=0;i<3;++i){window.findChild<QAction*>("undoAction")->trigger();flush();}
                check(std::abs(read().circles.front().radius-2)<1e-8,"GUI radius Undo lost original value");
                for(unsigned i=0;i<3;++i){window.findChild<QAction*>("redoAction")->trigger();flush();}
                check(read().points==completed.points&&read().circles==completed.circles,"GUI radius Redo changed center or final radius");
            }
            window.grab().save(QString::fromStdString((directory/("external-center-"+std::to_string(kind)+"-"+std::to_string(mode)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* candidate:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<app::SketchPropertiesDialog*>(candidate);dialog&&dialog->isVisible()){dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();}
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            window.close();flush();
        }
        check(scenarios>0,"No external center GUI scenario selected");
        std::cout<<scenarios<<" conic center mouse C/equality/both orders/automatic snapping/Undo/Redo/native save scenarios passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"External center GUI: "<<error.what()<<'\n';return 1;}
}
