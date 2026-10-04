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
        const auto frame_hash=[&] {
            const auto pixels=view->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
            return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()),pixels.sizeInBytes()),QCryptographicHash::Sha256).toHex();
        };
        const auto mesh_packet=[&] {
            kernel::BodyResult body;body.mesh=view->mesh();
            return document::serialize_body_result(body,false);
        };
        const auto original_packet=mesh_packet();
        const auto original_camera=view->camera_state();
        const auto document_state=window.execute_console_command("documents").data;
        auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
        check(undo&&redo,"Boundary Undo/Redo actions missing");
        const bool undo_before=undo->isEnabled(),redo_before=redo->isEnabled();
        QByteArray first_preview;
        for(int trial=0;trial<6;++trial) {
            const auto revision=view->base_mesh_revision();
            auto* item=row(id);check(item,"Boundary benchmark tree row missing");
            QElapsedTimer timer;timer.start();window.show_tree_item_properties(item);flush();
            const auto opening=timer.nsecsElapsed()/1e6;
            check(dialog(),"Boundary benchmark editor missing");
            const auto publications=view->base_mesh_revision()-revision;
            check(publications==1,"Boundary opening published an unused intermediate scene");
            const auto preview=frame_hash();
            if(trial==0)first_preview=preview;
            check(preview==first_preview,"Repeated Boundary opening changed preview");
            timer.restart();dialog()->buttons()->button(trial<3?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
            const auto closing=timer.nsecsElapsed()/1e6;
            check(!dialog(),"Boundary unchanged confirmation failed");
            check(window.execute_console_command("documents").data==document_state&&
                undo->isEnabled()==undo_before&&redo->isEnabled()==redo_before,"Boundary no-op changed document or Undo/Redo");
            check(view->camera_state()==original_camera&&mesh_packet()==original_packet,"Boundary no-op changed camera or display geometry");
            std::cout<<"Boundary no-op trial="<<trial<<" open_ms="<<opening<<" close_ms="<<closing
                <<" publications="<<publications<<" preview_sha256="<<preview.constData()
                <<" restored_sha256="<<frame_hash().constData()<<std::endl;
        }
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
        auto triangle=document::PartDocument::create_default();triangle.history.clear();triangle.sketches.clear();triangle.history_order.clear();
        document::BodyHistoryGraph triangle_graph;const auto triangle_body=triangle_graph.create_body("Triangle UI");
        const std::array<std::array<double,4>,3> triangle_lines{{{0,0,100,0},{100,0,0,80},{0,80,0,0}}};
        std::array<std::string,3> triangle_sources;
        for(int i=0;i<3;++i) {
            auto source=document::PartDocument::create_sketch_container();source.name="Triangle "+std::to_string(i+1);
            source.feature_kind=document::FeatureKind::Feature;source.feature.type=document::FeatureType::Sketch;
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=source.id;source.feature.sketch_id=sketch.id;
            sketch.add_segment(triangle_lines[i][0],triangle_lines[i][1],triangle_lines[i][2],triangle_lines[i][3]);
            triangle_sources[i]=source.id;triangle_graph.insert({document::PartHistoryKind::Feature,source.id});
            triangle.history.push_back(source);triangle.sketches.push_back(sketch);
        }
        triangle.set_body_history(triangle_graph);const auto triangle_path=directory/"boundary-triangle-ui.prtz";triangle.save(triangle_path);
        check(window.open_document_path(QString::fromStdString(triangle_path.string())),"Triangle fixture cannot open");flush();
        const auto triangle_activation=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",triangle_body}}}}.dump()));
        check(triangle_activation.ok,"Triangle Body cannot activate");flush();action->trigger();flush();check(dialog(),"Triangle creation dialog missing");
        auto* count=dialog()->findChild<QSpinBox*>("boundarySurfaceCount");check(count,"Triangle boundary count missing");count->setValue(3);flush();
        for(const auto& source:triangle_sources){auto* item=row(source);check(item,"Triangle source row missing");tree->setCurrentItem(item);flush();}
        const auto triangle_id=dialog()->pending.id;check(dialog()->active_row()==-1,"Triangle source entry did not finish");
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Triangle OK failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();std::vector<kernel::BodyResult> triangle_results;
        const auto triangle_saved=document::PartDocument::load(triangle_path,&triangle_results);
        check(triangle_saved.find_container(triangle_id)&&triangle_saved.find_container(triangle_id)->boundary_surface.boundaries.size()==3&&
            !triangle_results.empty()&&std::abs(triangle_results.back().surface_area-4000)<.01,"Triangle GUI lost definition or calculated native result");
        window.show_tree_item_properties(row(triangle_id));flush();check(dialog(),"Triangle Properties cannot reopen");
        dialog()->findChild<QSpinBox*>("boundarySurfaceCount")->setValue(2);flush();dialog()->reject();flush();
        window.show_tree_item_properties(row(triangle_id));flush();check(dialog()&&dialog()->pending.boundary_surface.boundaries.size()==3,
            "Triangle Cancel committed changed boundary count");dialog()->reject();flush();
        check(window.open_document_path(QString::fromStdString(path.string())),"Supported fill source cannot reopen");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,
            "Supported fill Body cannot activate");flush();action->trigger();flush();check(dialog(),"Supported fill creation missing");view->fit_all();flush();
        const auto pick_original=[&](viewer::CandidateKind kind,const std::string& owner,const std::string& key) {
            std::optional<QPointF> point;std::size_t index=0;
            for(int y=20;y<view->height()-20&&!point;y+=8)for(int x=20;x<view->width()-20&&!point;x+=8) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].kind==kind&&candidates[i].owner_id==owner&&
                    candidates[i].semantic_key==key&&candidates[i].geometry==viewer::CandidateGeometry::OriginalReference){point=QPointF(x,y);index=i;break;}
            }
            check(point.has_value(),"The common picker omitted an original fill edge or supporting face");
            const auto send=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons){
                QMouseEvent event(type,*point,QPointF(view->mapToGlobal(point->toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            send(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
            for(std::size_t i=0;i<index;++i){send(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);send(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);}
            send(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        };
        const auto supported_id=dialog()->pending.id;
        for(int i=0;i<4;++i) {
            const auto key="boundary:source:"+sources[i]+":edge:from:"+part.sketches[i].segments.front().id;
            pick_original(viewer::CandidateKind::Edge,id,key);
            check(dialog()->pending.boundary_surface.boundaries[i].kind==document::BoundaryCurveSource::Kind::Edge&&
                dialog()->pending.boundary_surface.boundaries[i].curve_id==key,"View fill lost original edge identity");
            dialog()->findChild<QComboBox*>(QString("boundaryContinuity%1").arg(i))->setCurrentIndex(i%2?1:2);flush();
            check(dialog()->support_entry()&&dialog()->active_row()==i,"Continuity did not arm its support field");
            pick_original(viewer::CandidateKind::Face,id,"surface:from:"+saved.find_container(id)->feature_id);
            check(dialog()->pending.boundary_surface.boundaries[i].support&&dialog()->support_inspected(i),"View fill lost supporting face or inspection");
            dialog()->findChild<QTableWidget*>("boundarySurfaceReferences")->cellWidget(i,8)->findChild<QCheckBox*>()->setChecked(i%2);flush();
        }
        check(dialog()->active_row()==-1,"Supported fill entry remained active");
        const auto supported_parameters=dialog()->pending.boundary_surface;
        window.grab().save(QString::fromStdString((directory/"supported-fill-properties.png").string()));
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Supported G1/G2 fill OK failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();std::vector<kernel::BodyResult> supported_results;
        const auto supported_saved=document::PartDocument::load(path,&supported_results);
        check(supported_saved.find_container(supported_id)&&supported_saved.find_container(supported_id)->boundary_surface==supported_parameters&&
            !supported_results.empty()&&std::abs(supported_results.back().surface_area-16000)<.01,"Supported GUI fill lost definition, side or native result");
        undo->trigger();flush();check(!row(supported_id),"Supported fill Undo retained the feature");
        redo->trigger();flush();check(row(supported_id),"Supported fill Redo lost the feature");
        window.show_tree_item_properties(row(supported_id));flush();check(dialog()&&dialog()->pending.boundary_surface==supported_parameters,"Supported fill Properties lost persisted references");
        dialog()->findChild<QTableWidget*>("boundarySurfaceReferences")->cellWidget(0,5)->findChild<QPushButton*>()->click();flush();
        check(dialog()->support_entry()&&!dialog()->pending.boundary_surface.boundaries[0].support,"Supported clear did not re-arm entry");
        dialog()->reject();flush();window.show_tree_item_properties(row(supported_id));flush();
        check(dialog()&&dialog()->pending.boundary_surface==supported_parameters,"Supported fill Cancel changed stored references");dialog()->reject();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto sewing_source=saved;sewing_source.document_id=document::PartDocument::create_default().document_id;
        auto wall_source=document::PartDocument::create_sketch_container();wall_source.name="Sewing wall Sketch";
        auto wall_feature=document::create_boundary_surface();wall_feature.name="Sewing wall surface";
        auto wall_sketch=sketcher::Sketch::create_default();wall_sketch.owner_container_id=wall_source.id;wall_sketch.plane=sketcher::SketchPlane::YZ;
        wall_sketch.plane_auto=false;wall_sketch.plane_offset=100;wall_source.feature_kind=document::FeatureKind::Feature;
        wall_source.feature.type=document::FeatureType::Sketch;wall_source.feature.sketch_id=wall_sketch.id;
        const std::array<std::array<double,4>,4> wall_lines{{{0,0,80,0},{80,0,80,80},{80,80,0,80},{0,80,0,0}}};
        for(int i=0;i<4;++i)wall_feature.boundary_surface.boundaries[i]={wall_source.id,wall_sketch.add_segment(wall_lines[i][0],wall_lines[i][1],wall_lines[i][2],wall_lines[i][3])};
        for(const auto& element:{wall_source,wall_feature}){sewing_source.insert_history_entry(document::PartHistoryKind::Feature,element.id);sewing_source.history.push_back(element);}
        sewing_source.sketches.push_back(wall_sketch);sewing_source.resolve_constructions();kernel::OcctKernel fixture_kernel;
        const auto sewing_inputs=fixture_kernel.evaluate_history(sewing_source.kernel_operations());
        check(!sewing_inputs.empty()&&sewing_inputs.back().calculation_errors.empty()&&std::abs(sewing_inputs.back().surface_area-14400)<.01,"Sewing GUI input fixture failed");
        const auto sewing_path=directory/"surface-sewing-ui.prtz";sewing_source.save(sewing_path,sewing_inputs);
        std::cout<<"Sewing GUI fixture saved"<<std::endl;
        check(window.open_document_path(QString::fromStdString(sewing_path.string())),"Sewing source cannot open");flush();
        std::cout<<"Sewing GUI fixture opened"<<std::endl;
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,
            "Sewing Body cannot activate");flush();view->fit_all();flush();
        auto* sewing_action=window.findChild<QAction*>("surfaceSewingAction");check(sewing_action&&sewing_action->isEnabled(),"Sewing Modeling action missing");
        const auto sewing_dialog=[&]{return dynamic_cast<SurfaceSewingDialog*>(window.findChild<QWidget*>("surfaceSewingDialog"));};
        auto revision=view->base_mesh_revision();sewing_action->trigger();flush();check(sewing_dialog(),"Sewing creation dialog missing");
        check(view->base_mesh_revision()-revision==1,"Sewing opening published an unused intermediate scene");
        std::cout<<"Sewing GUI input scene ready"<<std::endl;
        pick_original(viewer::CandidateKind::Face,id,"surface:from:"+saved.find_container(id)->feature_id);
        pick_original(viewer::CandidateKind::Face,wall_feature.id,"surface:from:"+wall_feature.feature_id);
        check(sewing_dialog()->pending.surface_sewing.faces.size()==2,"Sewing common picker lost selected faces");
        std::cout<<"Sewing GUI faces selected"<<std::endl;
        const auto sewing_id=sewing_dialog()->pending.id;const auto sewing_parameters=sewing_dialog()->pending.surface_sewing;
        window.grab().save(QString::fromStdString((directory/"surface-sewing-properties.png").string()));
        QMouseEvent sewing_accept(QEvent::MouseButtonDblClick,position,QPointF(view->mapToGlobal(position.toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&sewing_accept);flush();check(!sewing_dialog(),"Sewing middle double-click failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();std::vector<kernel::BodyResult> sewing_results;
        const auto sewn_saved=document::PartDocument::load(sewing_path,&sewing_results);
        check(sewn_saved.find_container(sewing_id)&&sewn_saved.find_container(sewing_id)->surface_sewing==sewing_parameters&&
            !sewing_results.empty()&&std::abs(sewing_results.back().surface_area-14400)<.01&&std::abs(sewing_results.back().volume)<1e-9,
            "GUI sewing did not persist its surface-only shell");
        const auto sewing_packet=mesh_packet();const auto sewing_camera=view->camera_state();const auto sewing_state=window.execute_console_command("documents").data;
        const bool sewing_undo_before=undo->isEnabled(),sewing_redo_before=redo->isEnabled();
        for(int trial=0;trial<4;++trial) {
            revision=view->base_mesh_revision();window.show_tree_item_properties(row(sewing_id));flush();
            check(sewing_dialog()&&view->base_mesh_revision()-revision==1,"Sewing Properties did not publish one input boundary scene");
            check(sewing_dialog()->pending.surface_sewing==sewing_parameters,"Sewing Properties lost stored references");
            if(trial%2)sewing_dialog()->reject();else sewing_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!sewing_dialog()&&mesh_packet()==sewing_packet&&view->camera_state()==sewing_camera&&window.execute_console_command("documents").data==sewing_state&&
                undo->isEnabled()==sewing_undo_before&&redo->isEnabled()==sewing_redo_before,"Sewing unchanged OK/Cancel changed geometry, camera or transaction state");
        }
        window.show_tree_item_properties(row(sewing_id));flush();sewing_dialog()->findChild<QTableWidget*>("surfaceSewingReferences")->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();
        check(sewing_dialog()->pending.surface_sewing.faces.size()==1,"Sewing remove did not delete the list item");sewing_dialog()->reject();flush();
        window.show_tree_item_properties(row(sewing_id));flush();check(sewing_dialog()->pending.surface_sewing==sewing_parameters,"Sewing Cancel changed the reference list");sewing_dialog()->reject();flush();
        undo->trigger();flush();check(!row(sewing_id),"Sewing Undo retained its container");redo->trigger();flush();check(row(sewing_id),"Sewing Redo lost its container");
        kernel::EdgeReference joint;
        for(const auto& edge:sewing_results.back().mesh.edges)if(edge.reference.owner_id==sewing_id&&!edge.points.empty()&&
            std::ranges::all_of(edge.points,[](const auto& point){return std::abs(point.x-100)<1e-7&&std::abs(point.z)<1e-7;}))joint=edge.reference;
        check(joint.valid(),"Sewn shell lost its selectable common edge");
        const auto fillet_dialog=[&]() -> PrimitivePropertiesDialog* {
            for(auto* candidate:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(candidate);dialog&&dialog->isVisible()&&
                dialog->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary"))return dialog;
            return nullptr;
        };
        auto* fillet_action=window.findChild<QAction*>("filletAction");check(fillet_action&&fillet_action->isEnabled(),"Fillet on a sewn shell is unavailable");
        fillet_action->trigger();flush();check(fillet_dialog(),"Sewn shell Fillet dialog did not open");view->fit_all();flush();
        std::optional<QPointF> joint_pixel;std::size_t joint_candidate_index=0;
        for(int y=20;y<view->height()-20&&!joint_pixel;y+=4)for(int x=20;x<view->width()-20&&!joint_pixel;x+=4) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            for(std::size_t i=0;i<candidates.size();++i)if(candidates[i].kind==viewer::CandidateKind::Edge&&
                candidates[i].geometry==viewer::CandidateGeometry::Display&&candidates[i].owner_id==joint.owner_id&&candidates[i].semantic_key==joint.semantic_key)
                {joint_pixel=QPointF(x,y);joint_candidate_index=i;break;}
        }
        check(joint_pixel.has_value(),"Fillet common picker omitted the sewn shell edge");
        const auto send_joint=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,*joint_pixel,QPointF(view->mapToGlobal(joint_pixel->toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        send_joint(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        for(std::size_t i=0;i<joint_candidate_index;++i){send_joint(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);send_joint(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);}
        send_joint(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);send_joint(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        fillet_dialog()->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(5);flush();
        fillet_dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();check(!fillet_dialog(),"Sewn shell Fillet OK failed");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();std::vector<kernel::BodyResult> fillet_results;
        const auto fillet_saved=document::PartDocument::load(sewing_path,&fillet_results);const auto fillet_id=fillet_saved.history.back().id;
        check(fillet_saved.history.back().feature_kind==document::FeatureKind::Fillet&&!fillet_results.empty()&&
            fillet_results.back().calculation_errors.empty()&&std::abs(fillet_results.back().volume)<1e-9&&
            fillet_results.back().surface_area>14000&&fillet_results.back().surface_area<14400,"GUI Fillet changed shell volume or lost its geometry");
        window.show_tree_item_properties(row(fillet_id));flush();check(fillet_dialog(),"Sewn shell Fillet Properties cannot reopen");
        fillet_dialog()->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(6);flush();fillet_dialog()->reject();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(sewing_path).find_container(fillet_id)->edge_treatment.primary_size==5,"Sewn shell Fillet Cancel changed its definition");
        undo->trigger();flush();check(!row(fillet_id)&&row(sewing_id),"Sewn shell Fillet Undo removed the shell");redo->trigger();flush();check(row(fillet_id),"Sewn shell Fillet Redo failed");
        window.grab().save(QString::fromStdString((directory/"boundary-surface-ui.png").string()));
        std::cout<<"Boundary/fill/sewing GUI: common picking, middle confirmation, native save, edit, Cancel, unchanged OK and shell Fillet passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
