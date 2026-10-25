static int verify_component_source_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1400,1000);window.show();flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            check(result.ok,result.message.c_str());flush();return result.data;
        };
        const auto editor=[&]()->app::ComponentPropertiesDialog* {
            for(auto* widget:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<app::ComponentPropertiesDialog*>(widget);dialog&&dialog->isVisible())return dialog;
            return nullptr;
        };
        const auto run_suffix=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        run("new",{{"type","part"},{"name","gui-empty-source-"+run_suffix}});run("save");
        run("new",{{"type","assembly"},{"name","gui-empty-owner-"+run_suffix}});
        auto* menu=window.findChild<QMenu*>("insertComponentMenu");check(menu,"Component insert menu missing");
        menu->aboutToShow();flush();QAction* insert{};
        for(auto* action:menu->actions())if(action->objectName()=="insertSourceAction"&&action->text().contains("gui-empty-source",Qt::CaseInsensitive))insert=action;
        check(insert&&insert->isEnabled(),"Empty Part is disabled in the actual insertion menu");
        insert->trigger();flush();check(editor(),"Empty Part did not open component Properties");
        editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(run("component.list").at("total")==0,"Canceled empty insertion retained a component");
        menu->aboutToShow();flush();insert=nullptr;
        for(auto* action:menu->actions())if(action->objectName()=="insertSourceAction"&&action->text().contains("gui-empty-source",Qt::CaseInsensitive))insert=action;
        check(insert&&insert->isEnabled(),"Empty insertion source disappeared after Cancel");
        insert->trigger();flush();check(editor(),"Empty Part retry failed");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor()&&run("component.list").at("total")==1,"Empty Part OK did not commit");
        run("undo");check(run("component.list").at("total")==0,"Empty insertion Undo failed");
        run("redo");run("save");

        kernel::OcctKernel kernel;
        auto source=document::PartDocument::create_default();source.history.clear();source.sketches.clear();source.constructions.clear();source.history_order.clear();
        auto stock=test::rectangular_feature(source,{20,10,4});auto cut=test::rectangular_feature(source,{2,20,8});cut.combine_mode=document::CombineMode::Subtract;
        source.history={stock,cut};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Split source"));
        graph.insert({document::PartHistoryKind::Feature,stock.id});graph.insert({document::PartHistoryKind::Feature,cut.id});source.set_body_history(graph);source.resolve_constructions();
        source.body_color="#4D1E88E5";source.appearance.body.color=source.body_color;
        const auto source_geometry=workspace::calculate_part(kernel,source);
        check(!source_geometry.back().calculation_errors.size(),"Split source calculation failed");
        check(std::ranges::any_of(source_geometry.back().mesh.triangle_references,[](const auto& face){return kernel::boolean_face_parent(face.semantic_key).has_value();}),
            "Real subtraction fixture did not produce explicit face fragments");
        const auto source_file=directory/"gui-fragment_skeleton.prtz";source.save(source_file,source_geometry);
        auto moving=document::PartDocument::create_default();auto box=test::rectangular_feature(moving,{8,6,4});moving.history={box};
        const auto center=moving.sketches.front().add_point(0,0);
        const auto contact=moving.sketches.front().add_point_reference_constraint(center,"sketch_origin");
        document::BodyHistoryGraph moving_graph;static_cast<void>(moving_graph.create_body("Moving Body"));
        moving_graph.insert({document::PartHistoryKind::Feature,box.id});moving.set_body_history(std::move(moving_graph));
        moving.resolve_constructions();
        const auto moving_geometry=workspace::calculate_part(kernel,moving);const auto moving_file=directory/"gui-fragment-moving.prtz";moving.save(moving_file,moving_geometry);
        auto group=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("gui-fragment_skeleton",source.document_id,source_file,source_geometry.back());fixed.grounded=true;
        auto placed=assembly::AssemblyDocument::create_part_occurrence("Moving",moving.document_id,moving_file,moving_geometry.back());placed.placement.x=30;placed.placement.z=15;
        group.components={fixed,placed};const auto file=directory/"gui-fragment-owner.asmz";group.save(file);
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"Fragment Assembly cannot open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Fragment View missing");view->fit_all();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==placed.occurrence_id&&
                (*i)->data(0,Qt::UserRole+3).toString().endsWith("-occurrence")){row=*i;break;}
        check(row,"Moving component Tree row missing");window.show_tree_item_properties(row);flush();auto* dialog=editor();check(dialog,"Fragment component Properties missing");
        auto* table=dialog->findChild<QTableWidget*>("componentPlacementTable");check(table,"Fragment placement table missing");
        const auto before=run("documents");viewer::ViewerCandidate selected_fragment;
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held){
            QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        for(int column:{1,2}) {
            table->cellClicked(0,column);flush();const auto path=assembly::InstancePath{}.child(column==1?placed.occurrence_id:fixed.occurrence_id).encoded();
            std::optional<QPointF> pixel;
            for(int y=15;y<view->height()&&!pixel;y+=12)for(int x=15;x<view->width()&&!pixel;x+=12) {
                const QPointF point(x,y);const auto candidates=view->selection_candidates_at(point);
                const auto offered=std::ranges::find_if(candidates,[&](const auto& candidate) {
                    return candidate.kind==viewer::CandidateKind::Face&&candidate.geometry==viewer::CandidateGeometry::Display&&candidate.instance_path==path&&
                        (column==1||kernel::boolean_face_parent(candidate.semantic_key).has_value());
                });
                if(offered==candidates.end())continue;
                mouse(point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                for(std::size_t cycle=0;cycle<candidates.size();++cycle) {
                    if(view->hovered_candidate()==std::optional{*offered})break;
                    mouse(point,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);
                    mouse(point,QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
                }
                check(view->hovered_candidate()==std::optional{*offered},"Actual mouse cycling changed the offered fragment");
                pixel=point;if(column==2)selected_fragment=*offered;
            }
            check(pixel.has_value(),"Actual component picker did not offer the required current face fragment");
            mouse(*pixel,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*pixel,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        }
        check(run("documents")==before,"Pending fragment placement changed the document");
        const auto canonical=app::placement_reference_source_candidate(selected_fragment);const auto& target=dialog->placement_references().front().target_reference;
        check(target.owner_id==canonical.owner_id&&target.semantic_key==canonical.semantic_key&&target.instance_path.encoded()==canonical.instance_path&&
            !kernel::boolean_face_parent(target.semantic_key),"Component placement did not store the oldest explicit original face");
        const auto pending=dialog->pending_value();auto solved=assembly::AssemblyDocument::load(file);*solved.find_occurrence(placed.occurrence_id)=pending;
        const auto& mate=pending.placement_references.front();const auto a=solved.resolve_plane(mate.component_reference),b=solved.resolve_plane(mate.target_reference);
        check(a.status==assembly::MateStatus::Valid&&b.status==assembly::MateStatus::Valid,"Canonical fragment mate cannot resolve");
        check(std::abs(kernel::dimension_dot(kernel::dimension_sub(a.plane.point,b.plane.point),b.plane.normal))<1e-6&&
            std::abs(std::abs(kernel::dimension_dot(a.plane.normal,b.plane.normal))-1)<1e-6,"Fragment placement violates its independent plane equation");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Fragment placement OK did not close");
        run("undo");run("redo");run("save");const auto saved=assembly::AssemblyDocument::load(file);
        check(saved.find_occurrence(placed.occurrence_id)->placement_references==pending.placement_references,"Fragment reference changed after native reopen/Undo/Redo");
        const auto* skeleton=saved.find_occurrence(fixed.occurrence_id);
        check(skeleton&&!skeleton->appearance_override&&!skeleton->body_color_override&&skeleton->appearance.body.color==source.appearance.body.color,
            "Assembly source color does not inherit the native Skeleton appearance");
        const auto active_path=assembly::InstancePath{}.child(placed.occurrence_id).encoded();
        run("component.activate",{{"instance_path",active_path}});
        const auto active_body=moving.body_history.active_body_id();check(!active_body.empty(),"Active source has no Body");
        run("body.activate",{{"body",active_body}});
        const auto feature_row=[&]()->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator item(tree);*item;++item)
                if((*item)->data(0,Qt::UserRole).toString().toStdString()==box.id&&
                   (*item)->data(0,Qt::UserRole+3)=="part-container") {
                    for(auto* parent=*item;parent;parent=parent->parent())
                        if(parent->data(0,Qt::UserRole+1).toString().toStdString()==active_path)return *item;
                }
            return nullptr;
        };
        check(feature_row(),"Activated Part feature row missing");
        feature_row()->setExpanded(true);for(auto* p=feature_row()->parent();p;p=p->parent())p->setExpanded(true);
        const auto expanded=[&] {
            auto* current=feature_row();check(current&&current->isExpanded(),"Active feature collapsed after an operation");
            for(auto* p=current->parent();p;p=p->parent())check(p->isExpanded(),"Active occurrence/Body collapsed after an operation");
        };
        run("construction.create",{{"kind","plane"},{"name","Tree refresh probe"},{"base_plane","xy"},{"offset_mm",12.5}});expanded();
        run("undo");expanded();run("redo");expanded();
        window.show_tree_item_properties(feature_row());flush();
        app::PrimitivePropertiesDialog* properties{};
        for(auto* widget:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);p&&p->isVisible())properties=p;
        check(properties,"Active source Properties missing");auto* sketch_button=properties->findChild<QPushButton*>("primitiveOwnSketchButton");
        check(sketch_button,"Active source Sketch button missing");sketch_button->click();flush();
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();
        const auto marker=std::ranges::find_if(view->mesh().constraint_markers,[&](const auto& value) {
            return value.reference.owner_id==moving.sketches.front().id&&value.reference.semantic_key=="constraint:"+contact;
        });
        check(marker!=view->mesh().constraint_markers.end()&&marker->label=="C"&&marker->reference.instance_path==active_path,
            "Activated Assembly Sketch lost its visible native point constraint");
        check(std::ranges::count_if(view->mesh().constraint_markers,[&](const auto& value){return value.reference==marker->reference;})==1,
            "Active Part override duplicated its point constraint marker");
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        for(auto* widget:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);p&&p->isVisible())p->reject();flush();
        std::cout<<"Component GUI: empty menu insertion/Cancel/OK/Undo/Redo, real split-face picking/original identity/plane equation, source color and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Component source GUI: "<<error.what()<<std::endl;return 1;}
}
