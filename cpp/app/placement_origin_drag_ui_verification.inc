static void verify_property_plane_and_rotation_grips(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
    };
    auto part=document::PartDocument::create_default();
    auto support=document::PartDocument::create_construction(document::ConstructionKind::Plane);support.origin={25,18,9};support.name="Picker support";
    part.constructions={support};document::BodyHistoryGraph graph;const auto body=graph.create_body("Property controls");
    graph.insert({document::PartHistoryKind::Construction,support.id});
    auto sketch=sketcher::Sketch::create_default();auto feature=document::PartDocument::create_feature_container(sketch.id);
    sketch.owner_container_id=feature.id;static_cast<void>(sketch.add_rectangle(10,-5,20,5));feature.feature.axis_segment_id=sketch.add_segment(0,-10,0,10);sketch.set_segment_centerline(feature.feature.axis_segment_id,true);
    feature.feature.sides[0].operation=document::FeatureSideOperation::Revolution;feature.feature.sides[0].angle_degrees=60;
    part.history={feature};part.sketches={sketch};graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
    kernel::OcctKernel kernel;const auto file=directory/"property-controls.prtz";part.save(file,kernel.evaluate_history(part.kernel_operations()));
    run("open",{{"path",file.string()}});run("body.activate",{{"body",body}});
    auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"Property controls workspace missing");
    const auto mouse=[&](QWidget* target,QPointF point,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,point,QPointF(target->mapToGlobal(point.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(target,&event);flush();
    };
    const auto click=[&](QWidget* target,QPointF point,Qt::MouseButton button=Qt::LeftButton) {
        mouse(target,point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        mouse(target,point,QEvent::MouseButtonPress,button,button);mouse(target,point,QEvent::MouseButtonRelease,button,Qt::NoButton);
    };
    const auto open=[&] {
        QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
        check(row,"Rotation Feature row missing");window.show_tree_item_properties(row);flush();
        auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));check(dialog,"Rotation Properties missing");return dialog;
    };
    const auto settle=[&] {QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
    const auto select_support=[&] {
        view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();settle();std::optional<QPointF> hit;
        for(int y=12;y<view->height()-12&&!hit;y+=5)for(int x=12;x<view->width()-12&&!hit;x+=5) {
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Plane&&candidates.front().owner_id==support.entity_id)hit=QPointF(x,y);
        }
        check(hit.has_value(),"View did not offer the earlier authored plane");click(view,*hit);
    };
    // Both arrow and text must arm the actual picker, after a short middle
    // click ended the previous entry. Exercise Feature and Sweep consumers.
    for(bool sweep:{false,true})for(int column:{0,1}) {
        QDialog* dialog{};
        if(sweep){window.findChild<QAction*>("sweep2dAction")->trigger();flush();dialog=dynamic_cast<Sweep2DDialog*>(window.findChild<QWidget*>("sweep2dDialog"));}
        else dialog=open();
        check(dialog,"Plane picker Properties missing");
        click(view,{15,15},Qt::MiddleButton);
        const auto state=run("documents");
        auto* field=dialog->findChild<QTableWidget*>(sweep?"sweep2dPathPlaneView":"featureProfilePlaneView");check(field&&field->isVisible(),"Work plane View field missing");
        const auto point=QPointF(field->columnViewportPosition(column)+field->columnWidth(column)/2.,field->rowViewportPosition(0)+field->rowHeight(0)/2.);
        click(field->viewport(),point);
        check(tree->property("commandSelectionActive").toBool(),"Work plane arrow/text did not arm reference entry");
        select_support();
        if(sweep) {
            const auto selected=dynamic_cast<Sweep2DDialog*>(dialog)->pending.sweep2d.path_plane;
            check(selected&&selected->owner_id==support.entity_id,"Sweep View click did not store its exact plane reference");
        }else {
            const auto pending=dynamic_cast<PrimitivePropertiesDialog*>(dialog)->pending_value();
            check(!pending.placement.references.empty()&&pending.placement.references.front().owner_id==support.entity_id,"Feature View click did not store its exact plane reference");
        }
        dialog->reject();flush();check(run("documents")==state,"Plane picker Cancel modified the document");
    }
    // Local datum picks must change only the Sketch plane, never attach the
    // container to itself or change its numeric placement/references.
    for(int kind:{0,1,2,3})for(int column:{0,1})for(const auto* key:{"xy","xz","yz"}) {
        const bool sweep=kind==1;
        QDialog* editor{};
        if(kind) {
            const auto action=kind==1?"sweep2dAction":kind==2?"helicalSweepAction":"createSectionAction";
            const auto name=kind==1?"sweep2dDialog":kind==2?"helicalSweepDialog":"sectionProperties";
            auto* command=window.findChild<QAction*>(action);check(command&&command->isEnabled(),"Plane consumer command unavailable");
            command->trigger();flush();editor=dynamic_cast<QDialog*>(window.findChild<QWidget*>(name));
        }else editor=open();
        check(editor,"Local-plane Properties missing");
        auto* feature_editor=dynamic_cast<PrimitivePropertiesDialog*>(editor);
        auto* sweep_editor=dynamic_cast<Sweep2DDialog*>(editor);
        auto* placed_editor=dynamic_cast<SweepPlacementDialog*>(editor);
        const auto before=kind?placed_editor->pending:feature_editor->pending_value();
        const auto combo_name=kind==1?"sweep2dPathPlane":kind==2?"helicalBasePlane":kind==3?"sectionSketchPlane":"featureProfilePlane";
        auto* combo=editor->findChild<QComboBox*>(combo_name);
        auto* field=editor->findChild<QTableWidget*>(combo->objectName()+"View");
        check(field->verticalScrollBarPolicy()==Qt::ScrollBarAlwaysOff&&field->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff,"Work-plane entry exposes scrollbars");
        check(field->columnWidth(1)>=field->fontMetrics().horizontalAdvance(field->item(0,1)->text())+6,"Work-plane prompt is clipped");
        check(field->viewport()->height()>=field->fontMetrics().height()+4,"Work-plane entry is too low for its text");
        if(kind==2&&column==0&&std::string_view(key)=="xy")editor->grab().save(QString::fromStdWString((directory/"helical-plane-properties.png").wstring()));
        click(view,{15,15},Qt::MiddleButton);
        click(field->viewport(),{field->columnViewportPosition(column)+field->columnWidth(column)/2.,field->rowHeight(0)/2.});
        view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();settle();
        std::optional<QPointF> hit;const std::string semantic=std::string("origin:plane:")+key;
        for(int y=12;y<view->height()-12&&!hit;y+=4)for(int x=12;x<view->width()-12&&!hit;x+=4) {
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Plane&&
                    candidates.front().owner_id==before.container_origin.id&&candidates.front().semantic_key==semantic)hit=QPointF(x,y);
        }
        if(!hit)std::cerr<<"Missing local plane: kind="<<kind<<", key="<<key<<", owner="<<before.container_origin.id<<'\n';
        check(hit.has_value(),"Own XY/XZ/YZ plane was not offered by the View field");click(view,*hit);
        const auto after=kind?placed_editor->pending:feature_editor->pending_value();
        check(after.placement==before.placement,"Local plane pick changed container placement");
        check(combo->currentText()==QString::fromLatin1(key).toUpper(),"Own plane click did not select the matching preset");
        if(sweep)check(after.sweep2d.path_plane&&after.sweep2d.path_plane->owner_id==after.container_origin.id&&after.sweep2d.path_plane->semantic_key==semantic,"Sweep own-plane reference did not match the selected datum");
        editor->reject();flush();
    }
    auto* dialog=open();click(view,{15,15},Qt::MiddleButton);
    view->set_projection_mode(viewer::ProjectionMode::Orthographic);view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();settle();
    const auto project=[&](kernel::Vec3 p) {
        const auto a=view->ray_at({0,0}),b=view->ray_at({100,0}),c=view->ray_at({0,100});check(a&&b&&c,"No camera rays");
        const auto delta=kernel::dimension_sub(p,a->first),x=kernel::dimension_sub(b->first,a->first),y=kernel::dimension_sub(c->first,a->first);
        return QPointF(100*kernel::dimension_dot(delta,x)/kernel::dimension_dot(x,x),100*kernel::dimension_dot(delta,y)/kernel::dimension_dot(y,y));
    };
    const auto initial=dialog->pending_value();const auto revision=run("documents");
    const auto origin=view->placement_origin_handle_position();check(origin.has_value(),"Feature origin grip missing");
    mouse(view,*origin,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
    mouse(view,*origin+QPointF(40,25),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
    mouse(view,*origin+QPointF(40,25),QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
    check(dialog->pending_value().placement!=initial.placement,"Feature origin grip did not move");
    check(dialog->pending_value().feature.sides[0].angle_degrees==initial.feature.sides[0].angle_degrees,"Origin grip changed the rotation angle");
    for(double amount:{2.,-1.,1.5}) {
        const auto handles=view->extent_manipulators();const auto found=std::ranges::find(handles,std::string("side0_angle"),&viewer::ExtentManipulator::start_key);
        check(found!=handles.end()&&!found->replaced_by_placement_origin,"Rotation grip was removed with the offset grip");
        check(view->placement_origin_handle_position().has_value(),"Rotation editor lost the origin grip");
        const auto point=project(found->origin);const auto pixels=view->grabFramebuffer();const double dpr=double(pixels.width())/view->width();bool purple=false;
        for(int dy=-4;dy<=4;++dy)for(int dx=-4;dx<=4;++dx) {
            const QPoint at(qRound(dpr*(point.x()+dx)),qRound(dpr*(point.y()+dy)));
            if(pixels.rect().contains(at)){const auto color=pixels.pixelColor(at);purple|=color.red()>170&&color.blue()>220&&color.green()<160;}
        }
        if(!purple) {
            pixels.save(QString::fromStdWString((directory/"rotation-grip-failure.png").wstring()));
            std::cerr<<"Rotation pixel="<<point.x()<<","<<point.y()<<" dpr="<<dpr<<" view="<<view->width()<<","<<view->height()<<" origin="<<found->origin.x<<","<<found->origin.y<<","<<found->origin.z<<'\n';
        }
        check(purple,"Rotation grip is not painted purple alongside the origin");
        const auto before=dialog->pending_value();const auto target=project(kernel::dimension_add(found->origin,kernel::dimension_scale(kernel::dimension_unit(found->direction),amount)));
        mouse(view,point,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(view,target,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(view,target,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        const auto after=dialog->pending_value();check(std::abs(after.feature.sides[0].angle_degrees-before.feature.sides[0].angle_degrees)>.01,"Purple rotation grip did not change the angle");
        check(after.placement==before.placement,"Rotation grip moved the origin instead");check(run("documents")==revision,"Rotation drag committed before OK");
    }
    dialog->reject();flush();dialog=open();check(dialog->pending_value()==initial,"Rotation Cancel changed the Feature");dialog->reject();flush();
    std::cout<<"Property controls: Feature/Sweep plane arrow and text clicks, exact View reference, Cancel, simultaneous purple rotation/origin grips and repeated rotation passed\n";
}

static int verify_placement_origin_drag_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& root) {
    try {
        const auto directory=root/("placement_drag-"+QUuid::createUuid().toString(QUuid::Id128).toStdString());std::filesystem::create_directories(directory);
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();
        auto plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);
        plane.origin={4,6,2.5};plane.offset=3.25;plane.value_locks={"placement:x"};
        plane.definition=document::ConstructionDefinition::PointReference;
        plane.references={{{},part.document_id+":origin","origin:plane:xy",2.5}};
        part.constructions={plane};document::BodyHistoryGraph graph;const auto body=graph.create_body("Origin drag");
        graph.insert({document::PartHistoryKind::Construction,plane.id});part.set_body_history(graph);part.resolve_constructions();plane=*part.find_construction(plane.id);
        const auto file=directory/"origin-drag.prtz";part.save(file);
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"Cannot open origin drag fixture");window.showMaximized();flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate origin drag Body");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(tree&&view,"Origin drag workspace missing");
        const auto open=[&] {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-construction"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==plane.id){row=*it;break;}
            check(row,"Origin drag Plane row missing");window.show_tree_item_properties(row);flush();
            for(auto* candidate:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<ConstructionPropertiesDialog*>(candidate);dialog&&dialog->isVisible())return dialog;
            throw std::runtime_error("Origin drag Properties missing");
        };
        auto* dialog=open();const auto before=dialog->pending_value();const auto state=window.execute_console_command("documents").data;
        view->set_projection_mode(viewer::ProjectionMode::Orthographic);view->set_standard_view(viewer::StandardView::Top);
        {QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();}view->fit_all();flush();
        const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        const auto drag=[&](QPointF delta,bool cancel) {
            const auto point=view->placement_origin_handle_position();check(point.has_value(),"Purple container origin is missing");
            mouse(*point,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
            for(double t:{.3,.6,1.})mouse(*point+delta*t,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
            if(cancel){QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();}
            mouse(*point+delta,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        };
        const auto verify=[&](bool moved) {
            const auto pending=dialog->pending_value();
            check(std::abs(pending.origin.x-before.origin.x)<1e-7&&std::abs(pending.origin.z-before.origin.z)<1e-7,"Origin drag changed a lock or support offset");
            check(pending.offset==before.offset&&pending.references==before.references&&pending.value_locks==before.value_locks,"Origin drag changed plane offset, references or locks");
            check((std::abs(pending.origin.y-before.origin.y)>1e-4)==moved,"Origin drag did not follow the available degree of freedom");
            check(window.execute_console_command("documents").data==state,"Origin drag committed before OK");
        };
        drag({60,40},true);verify(false);
        drag({60,40},false);verify(true);drag({-30,-70},false);verify(true);
        dialog->reject();flush();dialog=open();check(dialog->pending_value().origin==before.origin,"Cancel committed origin movement");
        drag({35,55},false);const auto accepted=dialog->pending_value();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Origin drag save failed");flush();
        const auto saved=document::PartDocument::load(file);check(saved.find_construction(plane.id)->origin==accepted.origin&&saved.find_construction(plane.id)->offset==before.offset,"Origin drag native reopening lost position or offset");
        check(window.execute_console_command("undo").ok,"Origin drag Undo failed");flush();dialog=open();check(dialog->pending_value().origin==before.origin,"Origin drag Undo changed the initial position");dialog->reject();flush();
        check(window.execute_console_command("redo").ok,"Origin drag Redo failed");flush();dialog=open();check(dialog->pending_value().origin==accepted.origin,"Origin drag Redo lost position");
        const auto unchanged=window.execute_console_command("documents").data;dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();const auto after=window.execute_console_command("documents").data;if(after!=unchanged)std::cerr<<"Unchanged document delta: "<<commands::Json::diff(unchanged,after).dump()<<'\n';check(after==unchanged,"Unchanged origin OK committed a transaction");
        // Reuse the mouse path on an exact circle inside a transformed Body.
        // Only the derived display polyline is visible; attachment uses the
        // persisted analytic curve and local Body coordinates.
        part=document::PartDocument::create_default();auto support=sketcher::Sketch::create_default();
        const auto circle=support.add_circle(0,0,10);auto sketch_owner=document::PartDocument::create_sketch_container();support.owner_container_id=sketch_owner.id;
        plane=document::PartDocument::create_construction(document::ConstructionKind::Plane);plane.origin={10,0,0};plane.offset=3.25;
        plane.definition=document::ConstructionDefinition::PointReference;plane.references={{{},support.id,"circle:"+circle}};
        part.history={sketch_owner};part.sketches={support};part.constructions={plane};
        document::BodyHistoryGraph curve_graph;const auto curve_body=curve_graph.create_body("Curve origin drag");
        curve_graph.insert({document::PartHistoryKind::Feature,sketch_owner.id});curve_graph.insert({document::PartHistoryKind::Construction,plane.id});
        auto transformed=*curve_graph.find(curve_body);auto& frame=transformed.scope.placement;frame.x=30;frame.y=-20;frame.z=9;frame.rotation_z=frame.absolute_rotation_z=35;curve_graph.update_body(std::move(transformed));
        part.set_body_history(curve_graph);part.resolve_constructions();const auto curve_file=directory/"curve-origin-drag.prtz";part.save(curve_file);
        check(window.open_document_path(QString::fromStdWString(curve_file.wstring())),"Cannot open curve drag fixture");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",curve_body}}}}.dump())).ok,"Cannot activate curve Body");flush();dialog=open();
        view->set_standard_view(viewer::StandardView::Top);{QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();}view->fit_all();flush();
        const auto curve_start=dialog->pending_value();
        const auto clock=std::chrono::steady_clock::now();
        for(const auto delta:{QPointF{-20,40},QPointF{-45,-20},QPointF{35,-45},QPointF{20,35}}) {
            const auto previous=dialog->pending_value();drag(delta,false);const auto pending=dialog->pending_value();
            check(std::abs(std::hypot(pending.origin.x,pending.origin.y)-10)<1e-7&&std::abs(pending.origin.z)<1e-7,"GUI drag left the exact curve in the transformed Body");
            check(std::hypot(pending.origin.x-previous.origin.x,pending.origin.y-previous.origin.y)>1e-4,"GUI curve drag did not move");
            check(pending.references==curve_start.references&&pending.offset==curve_start.offset,"GUI curve drag changed the attachment or plane offset");
        }
        std::cout<<"Four complete GUI curve drags: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clock).count()<<" ms\n";
        const auto curve_end=dialog->pending_value();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(window.execute_console_command("save").ok,"Curve drag save failed");flush();
        const auto curve_saved=document::PartDocument::load(curve_file);check(curve_saved.find_construction(plane.id)->origin==curve_end.origin,"Curve drag save/reopen lost its parameter");
        check(window.execute_console_command("undo").ok,"Curve drag Undo failed");flush();dialog=open();check(dialog->pending_value().origin==curve_start.origin,"Curve drag Undo lost the initial point");dialog->reject();flush();
        check(window.execute_console_command("redo").ok,"Curve drag Redo failed");flush();dialog=open();check(dialog->pending_value().origin==curve_end.origin,"Curve drag Redo lost its point");dialog->reject();flush();
        verify_property_plane_and_rotation_grips(application,window,directory);
        std::cout<<"Purple container origin: actual mouse drags, locks, support/plane offsets, Escape, Cancel, OK, Undo/Redo and native persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Container origin GUI: "<<error.what()<<'\n';return 1;}
}
