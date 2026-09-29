int verify_boundary_surface_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        window.show();window.raise();window.activateWindow();flush();
        auto part=document::PartDocument::create_default();part.history.clear();part.sketches.clear();part.history_order.clear();
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Boundary UI");
        const std::array<std::array<double,4>,4> lines{{{0,0,100,0},{100,0,100,80},{100,80,0,80},{0,80,0,0}}};
        std::array<std::string,4> sources;
        for(int i=0;i<4;++i){auto source=document::PartDocument::create_sketch_container();source.name="Boundary "+std::to_string(i+1);
            source.feature_kind=document::FeatureKind::Feature;source.feature.type=document::FeatureType::Sketch;
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;
            source.feature.sketch_id=sketch.id;
            sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3]);
            sources[i]=source.id;graph.insert({document::PartHistoryKind::Feature,source.id});part.history.push_back(source);part.sketches.push_back(sketch);}
        part.set_body_history(graph);part.resolve_constructions();
        const auto path=directory/"boundary-surface-ui.prtz";part.save(path);
        check(window.open_document_path(QString::fromStdString(path.string())),"Boundary source document cannot open");flush();
        const auto activated=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump()));check(activated.ok,"Boundary Body cannot activate");flush();
        auto* action=window.findChild<QAction*>("boundarySurfaceAction");check(action&&action->isEnabled()&&!action->icon().isNull(),"Boundary command or icon missing");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const std::string& id)->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)!="part-sketch-plane")return *it;return nullptr;};
        const auto dialog=[&]{return dynamic_cast<BoundarySurfaceDialog*>(window.findChild<QWidget*>("boundarySurfaceDialog"));};
        action->trigger();flush();check(dialog(),"Boundary creation did not open");
        for(int i=0;i<4;++i){
            // Use the Sketch child as well as its history-container parent.
            // Re-arm by a real cell click after ending reference entry.
            dialog()->end_entry();
            auto* table=dialog()->findChild<QTableWidget*>("boundarySurfaceReferences");
            const auto p=table->visualItemRect(table->item(i,2)).center();
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,QPointF(p),QPointF(table->viewport()->mapToGlobal(p)),Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(table->viewport(),&event);flush();
            }
            check(dialog()->active_row()==i,"Boundary reference cell did not re-arm entry");
            auto* item=row(i%2?part.sketches[i].id:sources[i]);check(item,"Boundary Sketch tree row missing");
            tree->setCurrentItem(item);flush();
            check(dialog()->inspected(i),"Selected boundary was not inspected by default");
        }
        for(int i=0;i<4;++i)check(dialog()->pending.boundary_surface.boundaries[i].owner_id==sources[i],"Tree pick lost perimeter order");
        const auto id=dialog()->pending.id;
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Boundary View missing");
        const QPointF position(view->width()/2,view->height()/2);
        QMouseEvent accept(QEvent::MouseButtonDblClick,position,QPointF(view->mapToGlobal(position.toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&accept);flush();
        check(!dialog(),"Middle double-click did not commit valid boundary surface");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> calculated;auto saved=document::PartDocument::load(path,&calculated);
        check(saved.find_container(id)&&!calculated.empty()&&std::abs(calculated.back().surface_area-8000)<.01,"GUI result did not persist surface");
        auto* result=row(id);check(result,"Boundary result tree row missing");
        window.show_tree_item_properties(result);flush();check(dialog(),"Boundary edit did not reopen common dialog");
        for(int i=0;i<4;++i)check(dialog()->inspected(i),"Reopened boundary did not inspect its saved curves");
        for(const auto& sketch:part.sketches)
            check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==sketch.id;}),
                "Boundary edit hid an input Sketch");
        check(std::ranges::none_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;}),"Boundary editing retained its final surface instead of rolling back");
        dialog()->findChild<QTableWidget*>("boundarySurfaceReferences")->cellWidget(0,1)->findChild<QPushButton*>()->click();
        check(dialog()->pending.boundary_surface.boundaries[0].owner_id.empty(),"Edit did not clear the boundary reference");
        view->fit_all();flush();std::optional<QPointF> edit_pick;
        for(int y=20;y<view->height()-20&&!edit_pick;y+=4)for(int x=20;x<view->width()-20&&!edit_pick;x+=4) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::SketchSegment&&
                candidates.front().owner_id==part.sketches[0].id)edit_pick=QPointF(x,y);
        }
        check(edit_pick.has_value(),"Boundary edit picker offered no input Sketch curve");
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*edit_pick,QPointF(view->mapToGlobal(edit_pick->toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        check(dialog()->pending.boundary_surface.boundaries[0].owner_id==sources[0]&&
            !dialog()->pending.boundary_surface.boundaries[0].curve_id.empty(),"Boundary edit did not replace source from View");
        dialog()->findChild<QTableWidget*>("boundarySurfaceReferences")->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(!dialog()->inspected(0)&&dialog()->inspected(1),"Boundary inspection did not turn off independently");
        dialog()->findChild<QTableWidget*>("boundarySurfaceReferences")->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(dialog()->inspected(0),"Boundary edit source inspection unavailable");
        window.grab().save(QString::fromStdString((directory/"boundary-surface-edit.png").string()));
        dialog()->reject();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).find_container(id)->boundary_surface==saved.find_container(id)->boundary_surface,"Cancel changed surface sources");
        check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;}),"Cancel did not restore the calculated surface");
        action->trigger();flush();check(dialog(),"Second boundary creation unavailable");
        view->fit_all();flush();std::optional<QPointF> pick;
        for(int y=20;y<view->height()-20&&!pick;y+=8)for(int x=20;x<view->width()-20&&!pick;x+=8) {
            const QPointF position(x,y);const auto candidates=view->selection_candidates_at(position);
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::SketchSegment)pick=position;
        }
        check(pick.has_value(),"The common View picker offered no individual Sketch boundary");
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*pick,QPointF(view->mapToGlobal(pick->toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        check(!dialog()->pending.boundary_surface.boundaries[0].curve_id.empty(),"View confirmation lost individual curve identity");
        const auto picked=dialog()->pending.boundary_surface.boundaries[0];
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,position,QPointF(view->mapToGlobal(position.toPoint())),Qt::MiddleButton,
                type==QEvent::MouseButtonPress?Qt::MiddleButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        check(dialog()&&dialog()->active_row()==-1&&dialog()->pending.boundary_surface.boundaries[0]==picked,"Short middle click committed or erased a boundary");
        dialog()->reject();flush();
        if(const auto input=qEnvironmentVariable("ZIMA_VERIFY_BOUNDARY_SOURCE");!input.isEmpty()) {
            const auto copy=directory/"boundary-user-copy.prtz";
            std::filesystem::copy_file(std::filesystem::u8path(input.toStdString()),copy,std::filesystem::copy_options::overwrite_existing);
            auto source=document::PartDocument::load(copy);
            check(window.open_document_path(QString::fromStdString(copy.string())),"User boundary copy cannot open");flush();
            const auto active=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",source.body_history.active_body_id()}}}}.dump()));
            check(active.ok,"User boundary Body cannot activate");flush();
            action->trigger();flush();check(dialog(),"User boundary creation unavailable");
            const auto names=qEnvironmentVariable("ZIMA_VERIFY_BOUNDARY_NAMES").split('|');
            check(names.size()==4,"User boundary verification needs four source names");
            std::array<std::string,4> selected;
            for(int i=0;i<4;++i) {
                const auto found=std::ranges::find(source.history,names[i].toStdString(),&document::HistoryContainer::name);
                check(found!=source.history.end(),"User boundary source missing");selected[i]=found->id;
                auto* item=row(found->id);check(item,"User boundary Tree row missing");tree->setCurrentItem(item);flush();
                check(dialog()->pending.boundary_surface.boundaries[i].owner_id==found->id,"User boundary source was rejected");
            }
            const auto surface=dialog()->pending.id;
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(dialog())throw std::runtime_error("User boundary OK failed: "+dialog()->findChildren<QLabel*>().back()->text().toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto restored=document::PartDocument::load(copy);
            check(restored.find_container(surface),"User boundary result was not saved");
            for(int i=0;i<4;++i)check(restored.find_container(surface)->boundary_surface.boundaries[i].owner_id==selected[i],"User boundary lost selected source");
            window.show_tree_item_properties(row(surface));flush();check(dialog(),"User boundary edit unavailable");
            for(const auto& sketch:source.sketches) {
                const auto* owner=source.find_container(sketch.owner_container_id);
                if(!owner||owner->suppressed||sketch.suppressed||!owner->feature.shows_sketch())continue;
                check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==sketch.id;}),
                    "User boundary edit hid a preceding Sketch");
            }
            window.grab().save(QString::fromStdString((directory/"boundary-user-edit.png").string()));
            dialog()->reject();flush();
            std::cout<<"User boundary copy: four selected Sketches, calculation and native persistence passed\n";
        }
        window.grab().save(QString::fromStdString((directory/"boundary-surface-ui.png").string()));
        std::cout<<"Boundary GUI: command, icon, Tree sources, middle confirmation, native save, edit and Cancel passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
