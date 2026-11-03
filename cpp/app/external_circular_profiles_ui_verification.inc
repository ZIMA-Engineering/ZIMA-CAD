int verify_external_circular_profiles_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    for(bool closed:{false,true}) {
        auto part=document::PartDocument::create_default();
        auto source=sketcher::Sketch::create_default();
        if(closed)static_cast<void>(source.add_circle(0,0,5));
        else {
            static_cast<void>(source.add_arc(0,0,5,0,0,5));
            static_cast<void>(source.add_segment(0,5,0,0));
            static_cast<void>(source.add_segment(0,0,5,0));
        }
        auto feature=document::PartDocument::create_extrusion_container(source.id);
        feature.extrusion.length_forward=3;source.owner_container_id=feature.id;
        part.sketches.push_back(source);part.history.push_back(feature);
        auto target=sketcher::Sketch::create_default();
        static_cast<void>(target.add_circle(12,8,2));
        auto owner=document::PartDocument::create_sketch_container();target.owner_container_id=owner.id;
        part.sketches.push_back(target);part.history.push_back(owner);
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Source"));graph.insert({document::PartHistoryKind::Feature,feature.id});
        const auto body=graph.create_body("Profile");graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);part.resolve_constructions();
        kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto file=directory/(closed?"circular-contour-ui.prtz":"arc-contour-ui.prtz");part.save(file,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1100,850);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        if(!verify(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Cannot open circular contour fixture"))return 1;
        flush();if(!verify(activate_test_body(application,window,body),"Cannot activate circular contour Body"))return 1;
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target.id &&
            (*i)->data(0,Qt::UserRole+3).toString()=="part-sketch"){row=*i;break;}
        if(!verify(row,"Circular contour target Sketch missing"))return 1;window.show_tree_item_properties(row);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");if(!verify(open,"Cannot edit circular contour Sketch"))return 1;open->click();flush();
        QEventLoop settled;QTimer::singleShot(950,&settled,&QEventLoop::quit);settled.exec();flush();
        auto* action=window.findChild<QAction*>("sketchExternalProfileAction");
        if(!verify(action&&action->isEnabled(),"Reference contour command is unavailable"))return 1;action->trigger();flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());std::optional<QPointF> hit;
        for(int y=15;y<view->height()-15&&!hit;y+=3)for(int x=15;x<view->width()-15&&!hit;x+=3) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Edge||candidates.front().owner_id!=feature.id)continue;
            const auto& candidate=candidates.front();
            const auto edge=std::ranges::find_if(view->mesh().original_references.edges,[&](const auto& e){return e.reference.owner_id==candidate.owner_id&&e.reference.semantic_key==candidate.semantic_key;});
            if(edge==view->mesh().original_references.edges.end()||!edge->exact_spline||edge->exact_spline->degree!=2)continue;
            const auto& a=edge->exact_spline->poles.front();const auto& b=edge->exact_spline->poles.back();
            if((std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-8)!=closed)continue;
            hit=QPointF(x,y);
        }
        if(!verify(hit.has_value(),"Circular body edge cannot be selected for contour"))return 1;
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        }
        const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
        const auto stored=snapshot();
        const auto& result=*std::ranges::find(stored.sketches,target.id,&sketcher::Sketch::id);
        const bool analytic_type=result.external_references.size()==(closed?1u:3u)&&result.import_blocks.size()==1&&
            result.bsplines.empty()&&result.circles.size()==(closed?2u:1u)&&
            result.arcs.size()==(closed?0u:1u);
        if(!analytic_type)std::cerr<<"External circular profile mismatch closed="<<closed
            <<" references="<<result.external_references.size()
            <<" imports="<<result.import_blocks.size()<<" splines="<<result.bsplines.size()
            <<" circles="<<result.circles.size()<<" arcs="<<result.arcs.size()<<'\n';
        if(!verify(analytic_type,"Mouse contour insertion did not retain the analytic type"))return 1;
        if(!verify(result.import_blocks.front().external_profile.has_value(),"Mouse contour insertion lost native projection metadata"))return 1;
        const auto points=result.points;const auto curves=result.circles;const auto arcs=result.arcs;
        window.findChild<QAction*>("undoAction")->trigger();flush();const auto undone=snapshot();
        const auto& old=*std::ranges::find(undone.sketches,target.id,&sketcher::Sketch::id);
        if(!verify(old.external_references.empty()&&old.import_blocks.empty()&&old.circles==target.circles&&old.arcs.empty(),"Mouse contour Undo lost the original Sketch"))return 1;
        window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=snapshot();
        const auto& restored=*std::ranges::find(redone.sketches,target.id,&sketcher::Sketch::id);
        if(!verify(restored.points==points&&restored.circles==curves&&restored.arcs==arcs&&restored.import_blocks==result.import_blocks,
            "Mouse contour Redo/save/reopen changed geometry or projection identity"))return 1;
        const auto current_serialized=[&] {
            const auto current=snapshot();
            const auto found=std::ranges::find(current.sketches,target.id,&sketcher::Sketch::id);
            if(found==current.sketches.end())throw std::runtime_error("Reference Profile Sketch disappeared");
            return found->serialized();
        };
        const auto reference_id=result.external_references.front().id;
        const auto contour_id=result.import_blocks.front().geometry_ids.front();
        const auto reference_key="external_edge:"+reference_id;
        const auto contour_key=(closed?"circle:":"arc:")+contour_id;
        const auto mouse=[&](QPointF position,QEvent::Type type,Qt::MouseButton button) {
            QMouseEvent event(type,position,QPointF(view->mapToGlobal(position.toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        const auto select_exact=[&](const std::string& key,viewer::CandidateKind kind) {
            mouse(*hit,QEvent::MouseMove,Qt::NoButton);
            const auto offered=view->selection_candidates_at(*hit);
            if(!verify(std::ranges::any_of(offered,[&](const auto& candidate){return candidate.kind==kind&&candidate.semantic_key==key;}),
                "Overlapping external reference and Reference Profile contour are not independently selectable"))return false;
            for(std::size_t cycle=0;cycle<offered.size()&&
                (!view->hovered_candidate()||view->hovered_candidate()->kind!=kind||view->hovered_candidate()->semantic_key!=key);++cycle) {
                mouse(*hit,QEvent::MouseButtonPress,Qt::RightButton);
                mouse(*hit,QEvent::MouseButtonRelease,Qt::RightButton);
            }
            if(!verify(view->hovered_candidate()&&view->hovered_candidate()->kind==kind&&
                view->hovered_candidate()->semantic_key==key,
                "RMB cycling cannot select the exact external reference or Reference Profile contour"))return false;
            mouse(*hit,QEvent::MouseButtonPress,Qt::LeftButton);
            mouse(*hit,QEvent::MouseButtonRelease,Qt::LeftButton);
            return verify(view->confirmed_candidate()&&view->confirmed_candidate()->kind==kind&&
                view->confirmed_candidate()->semantic_key==key,
                "Click confirmation changed the selected external reference or Reference Profile contour");
        };
        const auto erase_selection=[&] {
            QKeyEvent remove(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);
            QApplication::sendEvent(&window,&remove);flush();
        };
        if(!select_exact(reference_key,viewer::CandidateKind::SketchExternalReference))return 1;
        erase_selection();const auto detached_document=snapshot();
        const auto& detached=*std::ranges::find(detached_document.sketches,target.id,&sketcher::Sketch::id);
        const bool detached_contour_exists=closed
            ? std::ranges::any_of(detached.circles,[&](const auto& value){return value.id==contour_id;})
            : std::ranges::any_of(detached.arcs,[&](const auto& value){return value.id==contour_id;});
        if(!verify(detached.external_references.empty()&&detached.import_blocks.empty()&&
            detached_contour_exists,
            "Deleting the external reference did not retain the detached native contour"))return 1;
        window.findChild<QAction*>("undoAction")->trigger();flush();
        if(!verify(current_serialized()==result.serialized(),
            "Undo after external-reference deletion did not restore the linked contour"))return 1;
        window.findChild<QAction*>("redoAction")->trigger();flush();
        if(!verify(current_serialized()==detached.serialized(),
            "Redo after external-reference deletion changed the detached contour"))return 1;
        window.findChild<QAction*>("undoAction")->trigger();flush();
        if(!verify(current_serialized()==result.serialized(),
            "Second Undo did not restore the linked Reference Profile"))return 1;
        if(!select_exact(contour_key,viewer::CandidateKind::SketchCurve))return 1;
        erase_selection();const auto contour_deleted_document=snapshot();
        const auto& contour_deleted=*std::ranges::find(contour_deleted_document.sketches,target.id,&sketcher::Sketch::id);
        if(!verify(contour_deleted.external_references==result.external_references&&
            contour_deleted.import_blocks.empty()&&
            std::ranges::none_of(contour_deleted.circles,[&](const auto& value){return value.id==contour_id;})&&
            std::ranges::none_of(contour_deleted.arcs,[&](const auto& value){return value.id==contour_id;}),
            "Deleting the Reference Profile contour changed its external reference or left owned geometry"))return 1;
        window.findChild<QAction*>("undoAction")->trigger();flush();
        if(!verify(current_serialized()==result.serialized(),
            "Undo after Reference Profile deletion did not restore its exact identities"))return 1;
        window.findChild<QAction*>("redoAction")->trigger();flush();
        if(!verify(current_serialized()==contour_deleted.serialized(),
            "Redo/save/reopen after Reference Profile deletion changed the retained reference"))return 1;
        window.grab().save(QString::fromStdString(document::path_to_utf8((directory/(closed?"circular-contour-ui.png":"arc-contour-ui.png")))));
    }
    std::cout<<"Circular body contour mouse insertion, native type, Undo/Redo and persistence passed\n";return 0;
}
