int verify_form_sweep_sketch_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::load("cpp/tests/fixtures/sweep2d/form-dimension-edit.prtz");
        kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
        check(!calculated.empty()&&calculated.back().volume>0,"FORM fixture calculation failed");
        const auto feature=part.history.back();
        const auto original=sketcher::Sketch::from_serialized(feature.sweep2d.profiles.front().sketch_serialized);
        const auto path=directory/"form-sweep-edit.prtz";part.save(path,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"FORM failed to open");flush();
        check(activate_test_body(application,window,part.body_history.bodies().front().scope.id),"FORM Body unavailable");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"FORM View missing");
        const auto read=[&]{const auto saved=document::PartDocument::load(path);return sketcher::Sketch::from_serialized(saved.find_container(feature.id)->sweep2d.profiles.front().sketch_serialized);};
        const auto verify=[&](const sketcher::Sketch& s){
            check(s.external_references==original.external_references&&s.constraints==original.constraints,"FORM references or relations changed");
            const auto& arc=s.arcs.front();const auto* c=s.find_point(arc.center_point_id);const auto* a=s.find_point(arc.start_point_id);
            const auto* e=s.find_point(arc.end_point_id);const auto* q=s.find_point(s.segments.back().first_point_id);
            const double dx=a->x-q->x,dy=a->y-q->y;
            check(std::abs(c->x)<1e-7&&std::abs(c->y+arc.radius)<1e-7&&std::hypot(e->x,e->y)<1e-7,"FORM axis tangency failed");
            check(std::abs(std::hypot(a->x-c->x,a->y-c->y)-arc.radius)<1e-7&&std::abs((a->x-c->x)*dx+(a->y-c->y)*dy)<1e-6,"FORM line contact failed");
            check(std::abs(s.dimensions[0].value-arc.radius)<1e-7&&std::abs(s.dimensions[1].value-std::acos(std::clamp(-dx/std::hypot(dx,dy),-1.,1.))*180/std::acos(-1.))<1e-6,"FORM dimensions disagree with geometry");
        };
        const auto open=[&]{
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*i;break;}
            check(row,"FORM Sweep Tree row missing");window.show_tree_item_properties(row);flush();
            app::Sweep2DDialog* dialog{};for(auto* candidate:window.findChildren<QDialog*>())if(candidate->objectName()=="sweep2dDialog")dialog=dynamic_cast<app::Sweep2DDialog*>(candidate);check(dialog&&dialog->isVisible(),"FORM Sweep Properties missing");
            dialog->findChild<QPushButton*>("sweep2dStationSketch0")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();return dialog;
        };
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held){
            QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        const auto edit=[&](std::size_t index,double value){
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=original.id;candidate.semantic_key="dimension:"+original.dimensions[index].id;
            for(std::size_t i=0;i<view->mesh().dimensions.size();++i)if(view->mesh().dimensions[i].reference.owner_id==candidate.owner_id&&view->mesh().dimensions[i].reference.semantic_key==candidate.semantic_key)candidate.geometry_index=i;
            const auto p=view->candidate_dimension_label_position(candidate);check(p.has_value(),"FORM dimension label missing");
            mouse(*p,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(*p,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*p,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
            auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field,"FORM inline editor did not open by double click");
            field->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
            check(view->candidate_dimension_value(candidate).has_value()&&std::abs(*view->candidate_dimension_value(candidate)-value)<1e-7,"FORM mouse dimension edit failed");
        };
        auto* dialog=open();
        for(double value:{2.,4.,3.})edit(0,value);
        for(double value:{30.,50.,40.})edit(1,value);
        const auto drag=[&](const std::string& id,double x,double y){
            const auto s=sketcher::Sketch::from_serialized(dialog->pending.sweep2d.profiles.front().sketch_serialized);
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"FORM ray missing");const auto q=s.intersect_ray(ray->first,ray->second);check(q.has_value(),"FORM camera not facing profile");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double px,double py){return QPointF(((px-a[0])*vy-(py-a[1])*vx)/det,(ux*(py-a[1])-uy*(px-a[0]))/det);};
            const auto* point=s.find_point(id);const auto from=screen(point->x,point->y),to=screen(x,y);
            mouse(from,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        };
        // Finish returns the current embedded draft to its single owning dialog.
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        auto pending=sketcher::Sketch::from_serialized(dialog->pending.sweep2d.profiles.front().sketch_serialized);verify(pending);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"FORM Save failed");const auto changed=read();verify(changed);
        check(std::abs(changed.dimensions[0].value-3)<1e-7&&std::abs(changed.dimensions[1].value-40)<1e-7,"FORM OK did not persist dimensions");
        check(window.execute_console_command("undo").ok&&window.execute_console_command("save").ok,"FORM Undo failed");verify(read());
        check(read().serialized()==original.serialized(),"FORM Undo did not restore profile");
        check(window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"FORM Redo failed");check(read().serialized()==changed.serialized(),"FORM Redo differed");
        dialog=open();edit(0,4);edit(1,50);window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("save").ok&&read().serialized()==changed.serialized(),"FORM Cancel changed document");
        dialog=open();drag(original.arcs.front().center_point_id,0,-4);
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        pending=sketcher::Sketch::from_serialized(dialog->pending.sweep2d.profiles.front().sketch_serialized);verify(pending);
        check(std::abs(pending.arcs.front().radius-4)<.15,"FORM center did not move through mouse grip");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"FORM drag Save failed");verify(read());
        // Check the user's retained-face conversion, including the following
        // surface Fillet, against native saved viewer data.
        std::vector<kernel::BodyResult> skin_bodies;
        const auto skin=document::PartDocument::load("cpp/tests/fixtures/sweep2d/form-surface-shell.prtz",&skin_bodies);
        check(!skin_bodies.empty(),"FORM surface fixture lost persisted viewer packets");
        for(int state=0;state<4;++state) {
            auto scoped=skin;auto sweep=*scoped.find_container(feature.id);
            auto shell=*std::ranges::find_if(scoped.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Shell;});
            if(state==1)shell.shell.thickness=1;
            if(state==2)shell.suppressed=true;
            auto other=sweep;other.id="independent-sweep";
            scoped.history={sweep,shell,other};
            document::BodyHistoryGraph graph;const auto first=graph.create_body("Converted solid");
            graph.insert({document::PartHistoryKind::Feature,sweep.id});graph.insert({document::PartHistoryKind::Feature,shell.id});
            if(state==3)graph.set_history_cursor(first,1);
            static_cast<void>(graph.create_body("Independent solid"));graph.insert({document::PartHistoryKind::Feature,other.id});
            scoped.set_body_history(graph);
            kernel::ViewerMesh packet;
            for(const auto& owner:{sweep.id,other.id,std::string("authored-axis")})
                packet.axes.push_back({{0,0,0},{0,0,1},100,{owner,"axis:primary",{}}});
            packet.original_references.axes=packet.axes;
            app::filter_surface_shell_datums(packet,scoped);
            check(packet.axes.size()==(state==0?2:3)&&packet.original_references.axes.size()==3,"Surface datum filter crossed a Body, authoring, suppression, thickness or rollback boundary");
        }
        const auto skin_path=directory/"form-surface-shell.prtz";skin.save(skin_path,skin_bodies);
        window.findChild<QAction*>("closeDocumentAction")->trigger();flush();
        check(window.open_document_path(QString::fromStdString(skin_path.string())),"FORM surface fixture did not open");flush();
        const auto datum=[&](const auto& value){return value.reference.owner_id==feature.id&&
            (value.reference.semantic_key.starts_with("centerline:from:")||value.reference.semantic_key.starts_with("sweep:path-point:"));};
        check(std::ranges::any_of(view->mesh().original_references.edges,datum),"FORM surface conversion removed persisted Sweep reference data");
        check(std::ranges::none_of(view->mesh().edges,datum)&&std::ranges::none_of(view->mesh().axes,datum)&&std::ranges::none_of(view->mesh().points,datum),"FORM surface result still displays retired Sweep datums");
        check(activate_test_body(application,window,skin.body_history.bodies().front().scope.id),"FORM surface Body unavailable");flush();
        const auto shell=std::ranges::find_if(skin.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Shell;});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* shell_row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==shell->id){shell_row=*i;break;}
        check(shell_row,"FORM surface Shell Tree row missing");window.show_tree_item_properties(shell_row);flush();
        check(std::ranges::any_of(view->mesh().edges,datum),"FORM surface rollback lost the original solid Sweep axis");
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())if(auto* buttons=candidate->findChild<QDialogButtonBox*>())if(auto* cancel=buttons->button(QDialogButtonBox::Cancel)){cancel->click();flush();break;}
        check(std::ranges::none_of(view->mesh().edges,datum),"FORM surface Cancel left input Sweep axis visible");
        std::cout<<"FORM owned Sweep profile mouse dimensions, center grip, references, OK/Cancel, Undo/Redo and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
