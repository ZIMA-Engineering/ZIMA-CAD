static int verify_skeleton_limit_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        kernel::OcctKernel kernel;
        const auto make_part=[&](double half_x,double half_y,double height) {
            auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
            static_cast<void>(sketch.add_rectangle(-half_x,-half_y,half_x,half_y));
            auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.sides[0].length=height;
            sketch.owner_container_id=feature.id;part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            static_cast<void>(graph.create_body("Skeleton limit test"));graph.insert({document::PartHistoryKind::Feature,feature.id});
            part.set_body_history(graph);part.resolve_constructions();return part;
        };
        auto source=make_part(10,10,20),part=make_part(2,3,1),ordinary=make_part(3,3,25);
        const auto source_cache=workspace::calculate_part_with_resolved_references(kernel,source);
        const auto part_cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto ordinary_cache=workspace::calculate_part_with_resolved_references(kernel,ordinary);
        const auto source_file=directory/"gui-limit_SKELETON.prtz",part_file=directory/"gui-limit-part.prtz",ordinary_file=directory/"gui-limit-ordinary.prtz";
        source.save(source_file,source_cache);part.save(part_file,part_cache);ordinary.save(ordinary_file,ordinary_cache);
        kernel::FaceReference cap;const auto& geometry=source_cache.back().mesh.original_references;
        for(std::size_t index=0;index<geometry.triangle_references.size();++index) {
            bool top=true;for(unsigned j=0;j<3;++j)top=top&&std::abs(geometry.vertices.at(geometry.triangles.at(3*index+j)).z-20)<1e-7;
            if(top){cap=geometry.triangle_references[index];break;}
        }
        check(cap.valid(),"GUI Skeleton cap identity missing");
        auto owner=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("Skeleton",source.document_id,source_file,source_cache.back());
        auto moving=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_file,part_cache.back());
        auto other=assembly::AssemblyDocument::create_part_occurrence("Ordinary",ordinary.document_id,ordinary_file,ordinary_cache.back());other.placement.x=30;
        owner.components={fixed,moving,other};const auto file=directory/"gui-skeleton-limit.asmz";owner.save(file);
        app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1400,1000);window.show();flush();
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"GUI limit Assembly cannot open");flush();
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.code+": "+result.message);flush();return result.data;
        };
        const auto dependent_path=assembly::InstancePath{}.child(moving.occurrence_id).encoded();
        const auto source_path=assembly::InstancePath{}.child(fixed.occurrence_id).encoded();
        run("component.activate",{{"instance_path",dependent_path}});run("body.activate",{{"body",part.body_history.active_body_id()}});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        check(tree&&view,"GUI limit workspace missing");
        const auto dialog=[&]()->app::PrimitivePropertiesDialog* {
            for(auto* widget:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);d&&d->isVisible())return d;
            return nullptr;
        };
        const auto open=[&] {
            QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==part.history.front().id&&
                (*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
            check(row,"Active Part feature row missing");window.show_tree_item_properties(row);flush();check(dialog(),"GUI limit Properties missing");
        };
        const auto before=run("documents");
        for(bool cancel:{true,false}) {
            open();dialog()->findChild<QComboBox*>("featureSideEnd0")->setCurrentIndex(1);flush();
            auto* limits=dialog()->findChild<QWidget*>("featureSideLimits0");
            auto* table=limits?limits->findChild<QTableWidget*>():nullptr;
            check(table,"GUI limit table missing");table->cellClicked(0,3);flush();view->fit_all();flush();
            std::optional<QPointF> pixel;viewer::ViewerCandidate offered;
            for(int y=15;y<view->height()&&!pixel;y+=12)for(int x=15;x<view->width()&&!pixel;x+=12) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                const auto match=std::ranges::find_if(candidates,[&](const auto& c){return c.kind==viewer::CandidateKind::Face&&
                    c.instance_path==source_path&&c.owner_id==cap.owner_id&&c.semantic_key==cap.semantic_key;});
                check(std::ranges::none_of(candidates,[&](const auto& c){return c.instance_path==assembly::InstancePath{}.child(other.occurrence_id).encoded();}),
                    "An ordinary foreign Part is offered as an external limit");
                if(match!=candidates.end()){pixel=QPointF(x,y);offered=*match;}
            }
            check(pixel.has_value(),"The real View does not offer the Skeleton face as a limit");
            const auto mouse=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held) {
                QMouseEvent event(type,*pixel,QPointF(view->mapToGlobal(pixel->toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto boundary=view->candidate_face_triangles(offered);check(!boundary.empty(),"Skeleton feedback has no current face boundary");
            mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
            const auto candidates=view->selection_candidates_at(*pixel);
            for(std::size_t cycle=0;cycle<candidates.size()&&view->hovered_candidate()!=std::optional{offered};++cycle) {
                mouse(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
            }
            check(view->hovered_candidate()==std::optional{offered},"GUI limit hover/cycling lost the exact offered face");
            mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
            const auto pending=dialog()->pending_value();const auto& targets=pending.feature.sides[0].targets;
            check(targets.size()==1&&targets.front().external_source&&targets.front().external_source->document_id==source.document_id&&
                targets.front().reference.instance_path==source_path&&targets.front().external_source->source_face->kernel_shape.empty(),
                "View confirmation lost the external identity or calculated native geometry during picking");
            check(run("documents")==before,"Draft Skeleton limit changed a document revision");
            dialog()->buttons()->button(cancel?QDialogButtonBox::Cancel:QDialogButtonBox::Ok)->click();flush();check(!dialog(),"GUI limit Properties did not close");
            if(cancel)check(run("documents")==before,"Canceled Skeleton limit changed document state");
        }
        run("save");std::vector<kernel::BodyResult> result;auto saved=document::PartDocument::load(part_file,&result);
        kernel.ensure_body_properties(result.back());check(std::abs(result.back().volume-480)<1e-6,"GUI Skeleton limit produced the wrong volume");
        check(saved.history.front().feature.sides[0].targets.front().external_source.has_value(),"Native GUI save lost the external binding");
        run("undo");run("save");check(document::PartDocument::load(part_file).history.front().feature.sides[0].targets.empty(),"GUI limit Undo failed");
        run("redo");run("save");check(document::PartDocument::load(part_file).history.front().feature.sides[0].targets.size()==1,"GUI limit Redo failed");
        auto parameters=run("feature.get",{{"container",part.history.front().id}}).at("parameters");
        if(!parameters.at("sides")[0].contains("up_to"))parameters["sides"][0]["up_to"]={{"length_correction",0.},{"angle_correction_degrees",0.},{"retained_patches",commands::Json::array()}};
        parameters["sides"][0]["up_to"]["length_correction"]=2.;
        run("feature.set",{{"container",part.history.front().id},{"parameters",parameters}});run("save");
        saved=document::PartDocument::load(part_file,&result);kernel.ensure_body_properties(result.back());
        check(std::abs(result.back().volume-528)<1e-6,"CLI external limit correction did not use the same native source");
        check(document::PartDocument::load(source_file).serialized()==source.serialized(),"GUI/CLI limit changed the read-only Skeleton file");
        std::cout<<"Skeleton limit GUI/CLI: real face hover/cycling/LMB, foreign Part filtering, read-only draft, Cancel/OK, exact volume, correction, Undo/Redo and native persistence passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
