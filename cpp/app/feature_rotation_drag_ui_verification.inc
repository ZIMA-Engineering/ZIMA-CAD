static void verify_feature_rotation_drag(QApplication& application,AssemblyWorkspaceWindow& window,
                                        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    check(window.execute_console_command("save").ok,"Cannot save preceding reversal fixture");
    for(std::size_t side:{0u,1u})for(bool reversed:{false,true})for(bool reverse_axis:{false,true}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_rectangle(4,-3,7,3));
        const auto axis=reverse_axis?sketch.add_segment(0,10,0,-10):sketch.add_segment(0,-10,0,10);
        sketch.set_segment_centerline(axis,true);
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        auto& p=feature.feature;p.axis_segment_id=axis;p.reversed_sides=reversed;
        for(auto& row:p.sides)row.operation=document::FeatureSideOperation::Revolution;
        p.sides[0].angle_degrees=65;p.sides[1].angle_degrees=25;
        feature.value_locks={"side"+std::to_string(1-side)+"_angle"};
        sketch.owner_container_id=feature.id;part.history={feature};part.sketches={sketch};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Rotation drag");
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto path=directory/("rotation-"+std::to_string(side)+std::to_string(reversed)+std::to_string(reverse_axis)+".prtz");
        part.save(path,kernel::OcctKernel{}.evaluate_history(part.kernel_operations()));
        check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open rotation drag fixture");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate rotation Body");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"Rotation workspace missing");
        const auto open=[&] {
            QTreeWidgetItem* item{};for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole+3)=="part-container"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){item=*it;break;}
            check(item,"Rotation row missing");window.show_tree_item_properties(item);flush();
            auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));check(dialog,"Rotation Properties missing");return dialog;
        };
        auto* dialog=open();const auto before=dialog->pending_value();const auto state=window.execute_console_command("documents").data;
        const auto revision=view->base_mesh_revision();
        const auto verify=[&](double angle,bool direction_locked=false) {
            const auto next=dialog->pending_value();
            auto expected_locks=before.value_locks;if(direction_locked)expected_locks.insert("direction");
            check(std::abs(next.feature.sides[side].angle_degrees-std::min(std::abs(angle),360-p.sides[1-side].angle_degrees))<1e-3,"Rotation drag angle or opposite-end limit is wrong");
            check(next.feature.reversed_sides==(reversed!=(angle<0)),"Rotation drag lost its baseline direction");
            check(next.feature.sides[1-side]==p.sides[1-side]&&next.value_locks==expected_locks,"Rotation drag changed the opposite panel or locks");
            check(view->base_mesh_revision()==revision&&window.execute_console_command("documents").data==state,"Rotation draft published calculated geometry or committed a transaction");
        };
        view->set_projection_mode(viewer::ProjectionMode::Orthographic);view->set_standard_view(viewer::StandardView::Front);
        {QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();}view->fit_all();flush();
        const auto key="side"+std::to_string(side)+"_angle";
        const auto handle=std::ranges::find_if(view->extent_manipulators(),[&](const auto& h){return h.start_key==key;});
        check(handle!=view->extent_manipulators().end(),"Rotation grip missing");const auto h=*handle;
        const double norm=std::hypot(std::hypot(h.direction.x,h.direction.y),h.direction.z);
        const kernel::Vec3 d{h.direction.x/norm,h.direction.y/norm,h.direction.z/norm};
        const auto plane=[&](QPointF pixel) {
            const auto ray=view->ray_at(pixel);check(ray&&std::abs(ray->second.y)>1e-9,"Rotation drag ray unavailable");
            const double t=(h.origin.y-ray->first.y)/ray->second.y;
            return kernel::Vec3{ray->first.x+t*ray->second.x,h.origin.y,ray->first.z+t*ray->second.z};
        };
        const auto o=plane({0,0}),u=plane({100,0}),v=plane({0,100});
        const double ux=(u.x-o.x)/100,uz=(u.z-o.z)/100,vx=(v.x-o.x)/100,vz=(v.z-o.z)/100,det=ux*vz-uz*vx;
        check(std::abs(det)>1e-12,"Rotation drag projection is singular");
        const auto mouse=[&](double coordinate,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            const double x=h.origin.x+d.x*coordinate-o.x,z=h.origin.z+d.z*coordinate-o.z;
            const QPointF at((x*vz-z*vx)/det,(ux*z-uz*x)/det);
            QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        mouse(0,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(0,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
        for(double angle:{15.,-15.,-35.,450.,20.}) {
            mouse((angle-p.sides[side].angle_degrees)*25*std::numbers::pi/180,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);verify(angle);
        }
        mouse((20-p.sides[side].angle_degrees)*25*std::numbers::pi/180,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        auto* field=dialog->findChild<QDoubleSpinBox*>(QString("featureSideValue%1").arg(side));check(field,"Rotation angle field missing");
        field->findChild<QLineEdit*>()->setText("70-105");
        {QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);}flush();verify(-35);
        auto* direction=dialog->findChild<QDoubleSpinBox*>("featureDirection");
        auto* lock=direction?direction->findChild<QAction*>("valueLock:direction"):nullptr;check(lock,"Rotation direction lock missing");
        lock->setChecked(true);flush();field->setValue(-45);flush();verify(-35,true);
        check(!dialog->set_feature_angle_from_drag(side,-40,!reversed),"Rotation drag ignored the direction lock");
        lock->setChecked(false);flush();verify(-35);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(window.execute_console_command("save").ok,"Rotation Save failed");
        std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);
        const double volume=std::numbers::pi*(49-16)*6*(35+p.sides[1-side].angle_degrees)/360;
        check(!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().volume-volume)<1e-6,"Reversed rotation result violates independent sector volume");
        check(saved.find_container(feature.id)->feature.reversed_sides!=reversed&&saved.find_container(feature.id)->feature.sides[1-side]==p.sides[1-side],"Rotation native reopen lost direction or opposite side");
        check(window.execute_console_command("undo").ok,"Rotation Undo failed");flush();dialog=open();check(dialog->pending_value().feature==before.feature,"Rotation Undo did not restore parameters");dialog->reject();flush();
        check(window.execute_console_command("redo").ok,"Rotation Redo failed");flush();check(window.execute_console_command("save").ok,"Rotation Redo Save failed");
        check(window.open_document_path(QString::fromStdWString(path.wstring())),"Rotation reopen failed");flush();
        const auto reopened=window.execute_console_command("documents").data;dialog=open();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("documents").data==reopened,"Unchanged rotation OK committed a transaction");
        window.show_parameter_dimensions(feature.id);flush();
        const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==feature.id&&d.reference.semantic_key=="parameter:"+key;});
        check(dimension!=view->mesh().dimensions.end(),"Rotation View angle annotation missing");
        viewer::ViewerCandidate candidate{viewer::CandidateKind::Dimension,0,static_cast<std::size_t>(dimension-view->mesh().dimensions.begin()),feature.id,dimension->reference.semantic_key,{}};
        const auto pixel=view->candidate_dimension_label_position(candidate);check(pixel.has_value(),"Rotation View angle has no label position");
        QMouseEvent move(QEvent::MouseMove,QPointF(*pixel),QPointF(view->mapToGlobal(*pixel)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
        QMouseEvent edit(QEvent::MouseButtonDblClick,QPointF(*pixel),QPointF(view->mapToGlobal(*pixel)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&edit);flush();
        auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Rotation View angle editor did not open");
        editor->setText("-45");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
        check(!view->findChild<QLineEdit*>("inlineDimensionValueEdit"),"Negative rotation View angle was rejected");
        check(window.execute_console_command("save").ok,"Rotation View angle Save failed");cache.clear();saved=document::PartDocument::load(path,&cache);
        const auto& edited=saved.find_container(feature.id)->feature;
        check(edited.reversed_sides==reversed&&edited.sides[side].angle_degrees==45&&edited.sides[1-side]==p.sides[1-side],"Negative rotation View angle lost direction or panel identity");
        check(std::abs(cache.back().volume-std::numbers::pi*(49-16)*6*(45+p.sides[1-side].angle_degrees)/360)<1e-6,"Negative rotation View angle violates sector volume");
        check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Rotation View angle Undo/Redo failed");flush();
    }
    std::cout<<"Revolution: eight actual mouse matrices, zero crossing/back, opposite-end stop, negative field/View angles, locks, sector volume, Undo/Redo and reopen passed\n";
}
