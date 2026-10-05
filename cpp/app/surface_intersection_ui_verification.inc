int verify_surface_intersection_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Intersection GUI");part.set_body_history(graph);
        const std::array<kernel::Vec3,4> corners{{{0,0,0},{100,0,0},{100,80,0},{0,80,0}}};
        std::array<document::HistoryContainer,2> surfaces;
        for(unsigned side=0;side<2;++side) {
            auto feature=document::create_general_surface();feature.name=side?"Crossing surface":"Base surface";
            for(unsigned i=0;i<4;++i) {
                auto sketch=sketcher::Sketch::from_serialized(feature.general_surface.boundaries[i].sketch_serialized);
                static_cast<void>(sketch.add_segment(corners[i].x,corners[i].y,corners[(i+1)%4].x,corners[(i+1)%4].y));
                feature.general_surface.boundaries[i].sketch_serialized=sketch.serialized();
            }
            if(side){feature.placement.x=50;feature.placement.z=50;feature.placement.rotation_y=90;feature.placement.absolute_rotation_y=90;}
            part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);surfaces[side]=std::move(feature);
        }
        kernel::OcctKernel kernel;const auto input=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto path=directory/"surface-intersection-ui.prtz";part.save(path,input);
        window.resize(1280,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Intersection fixture");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate Intersection Body");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Intersection View missing");
        auto* action=window.findChild<QAction*>("surfaceIntersectionAction");check(action&&action->isEnabled(),"Intersection action unavailable");
        const auto editor=[&]{return dynamic_cast<SurfaceIntersectionDialog*>(window.findChild<QWidget*>("surfaceIntersectionDialog"));};
        const auto documents=window.execute_console_command("documents").data;
        const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button) {
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(widget,&event);flush();
            }
        };
        action->trigger();flush();check(editor()&&editor()->windowType()==Qt::SubWindow,"Intersection did not open common internal Properties");
        check(!editor()->findChild<QPushButton*>("containerOriginSelectionButton"),"Source-face command exposed unused placement");
        const auto id=editor()->pending.id;
        for(unsigned side=0;side<2;++side) {
            auto* table=editor()->findChild<QTableWidget*>("surfaceIntersectionReferences");check(table,"Intersection reference table missing");
            table->cellClicked(side,2);flush();view->fit_all();flush();
            const auto source=surfaces[side].id;const auto key="surface:from:"+surfaces[side].feature_id;
            std::optional<QPointF> position;
            for(int y=20;y<view->height()-20&&!position;y+=6)for(int x=20;x<view->width()-20&&!position;x+=6)
                if(std::ranges::any_of(view->selection_candidates_at(QPointF(x,y)),[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==source&&candidate.semantic_key==key;}))position=QPointF(x,y);
            check(position.has_value(),"Original intersection face absent from common picker");
            QMouseEvent move(QEvent::MouseMove,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            for(std::size_t i=0;i<view->selection_candidates_at(*position).size();++i) {
                const auto offered=view->offered_candidate();if(offered&&offered->owner_id==source&&offered->semantic_key==key)break;
                click(view,*position,Qt::RightButton);
            }
            const auto offered=view->offered_candidate();check(offered&&offered->owner_id==source&&offered->semantic_key==key,"Intersection cycling changed the candidate list");
            click(view,*position,Qt::LeftButton);check(editor()->pending.surface_intersection.faces[side]==kernel::FaceReference{source,key,{}},"Intersection lost selected native face");
        }
        check(window.execute_console_command("documents").data==documents,"Intersection picking committed history before OK");
        for(int row=0;row<2;++row) {
            auto* indicator=editor()->findChild<QTableWidget*>("surfaceIntersectionReferences")->cellWidget(row,1);
            check(!indicator->findChild<QLabel*>()->isVisible()&&!indicator->findChild<QPushButton*>()->isVisible(),
                "Populated required face exposed an unused entry or remove control");
        }
        window.grab().save(QString::fromStdString((directory/"surface-intersection-properties.png").string()));
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"Intersection failed on OK");
        check(std::ranges::count_if(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==id;})==1,"Intersection curve is missing or drawn twice");
        view->fit_all();flush();
        std::optional<QPointF> wire_position;
        for(int y=15;y<view->height()-15&&!wire_position;y+=4)for(int x=15;x<view->width()-15&&!wire_position;x+=4)
            if(std::ranges::any_of(view->selection_candidates_at(QPointF(x,y)),[&](const auto& candidate){
                return candidate.kind==viewer::CandidateKind::Container&&candidate.owner_id==id;
            }))wire_position=QPointF(x,y);
        check(wire_position.has_value(),"Intersection container is absent from ordinary View picking");
        QMouseEvent wire_move(QEvent::MouseMove,*wire_position,QPointF(view->mapToGlobal(wire_position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&wire_move);flush();
        for(std::size_t i=0;i<view->selection_candidates_at(*wire_position).size();++i) {
            const auto offered=view->offered_candidate();if(offered&&offered->owner_id==id)break;
            click(view,*wire_position,Qt::RightButton);
        }
        check(view->offered_candidate()&&view->offered_candidate()->owner_id==id,"Intersection wire cannot be offered by hover/cycling");
        const auto hover_candidate=*view->offered_candidate();
        check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==id&&viewer::candidate_recolors_wire_edge(hover_candidate,edge);}),
            "Intersection container hover does not highlight its exact wire");
        click(view,*wire_position,Qt::LeftButton);
        auto* selection_tree=window.findChild<QTreeWidget*>("documentTree");
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==id&&selection_tree->currentItem()&&
            selection_tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==id,"Intersection View selection did not synchronize the Tree");
        view->clear_selection();selection_tree->clearSelection();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
        check(saved.find_container(id)&&!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().surface_area-16000)<.02,"Intersection native result changed its surfaces");
        const auto before=window.execute_console_command("documents").data;
        const auto row=[&]()->QTreeWidgetItem* {auto* tree=window.findChild<QTreeWidget*>("documentTree");for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;return nullptr;};
        check(row(),"Intersection Tree row missing");const auto revision=view->base_mesh_revision();window.show_tree_item_properties(row());flush();
        check(editor()&&view->base_mesh_revision()==revision+1,"Intersection opening published an unused result scene");
        check(std::ranges::none_of(view->mesh().original_references.edges,[&](const auto& edge){return edge.reference.owner_id==id;}),"Intersection rollback offered its own result");
        editor()->end_entry();check(editor()->active_row()==-1&&!editor()->inspected(0)&&!editor()->inspected(1),"Intersection end-entry retained input or inspection");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor()&&window.execute_console_command("documents").data==before,"Intersection unchanged OK created a transaction");
        window.show_tree_item_properties(row());flush();editor()->findChild<QLineEdit*>("surfaceIntersectionName")->setText("Canceled name");
        editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(window.execute_console_command("documents").data==before,"Intersection Cancel changed history");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(!document::PartDocument::load(path).find_container(id),"Intersection Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();check(document::PartDocument::load(path).find_container(id),"Intersection Redo failed");
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
