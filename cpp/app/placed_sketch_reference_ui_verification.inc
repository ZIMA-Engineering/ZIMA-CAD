int verify_placed_sketch_reference_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();
        auto anchor=document::PartDocument::create_construction(document::ConstructionKind::Point);
        auto source=document::PartDocument::create_construction(document::ConstructionKind::Point);source.origin={12,4,0};
        auto axis=document::PartDocument::create_construction(document::ConstructionKind::Axis);axis.origin={12,4,0};axis.direction={0,0,1};
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_circle(0,0,10));
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);sketch.owner_container_id=feature.id;
        feature.placement.references.push_back({{},anchor.container_origin.id,"point"});
        part.history={feature};part.sketches={sketch};part.constructions={anchor,source,axis};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Placed profile");
        for(const auto& c:part.constructions)graph.insert({document::PartHistoryKind::Construction,c.id});
        graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        const auto geometry=part.construction_viewer_mesh().original_references;
        const auto add=[&](sketcher::ExternalReferenceKind kind,const std::string& owner,const std::string& key) {
            auto ref=sketcher::Sketch::create_external_reference(kind);ref.source_document_id=part.document_id;ref.source_owner_id=owner;ref.source_semantic_key=key;
            workspace::populate_external_reference_cache(part.sketches.front(),ref,geometry);
            const auto id=ref.id;part.sketches.front().add_external_reference(ref);return id;
        };
        const auto point_ref=add(sketcher::ExternalReferenceKind::Point,source.container_origin.id,"point");
        const auto axis_ref=add(sketcher::ExternalReferenceKind::Axis,axis.entity_id,"axis");
        const auto position=part.sketches.front().external_references.front().cached_points.front();
        const auto line=part.sketches.front().add_segment(position[0],position[1],26,12,1e-6,true,false,false);
        const auto endpoint=part.sketches.front().segments.front().first_point_id;
        static_cast<void>(part.sketches.front().add_point_reference_constraint(endpoint,point_ref));
        kernel::OcctKernel kernel;auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
        check(!calculated.empty()&&calculated.back().volume>0,"Placed profile fixture failed to calculate");
        const auto original=part.sketches.front();const auto definitions=part.constructions;
        const auto path=directory/"placed-sketch-reference.prtz";part.save(path,calculated);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Placed profile failed to open");flush();
        check(activate_test_body(application,window,body),"Placed profile Body unavailable");flush();
        const auto open=[&] {
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*i;break;}
            check(row,"Placed profile Tree row missing");window.show_tree_item_properties(row);flush();
            app::PrimitivePropertiesDialog* dialog{};
            for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(d);p&&p->isVisible())dialog=p;
            check(dialog,"Placed profile Properties missing");return dialog;
        };
        const auto return_sketch=[&](app::PrimitivePropertiesDialog* dialog) {
            auto expected_document=part;
            *expected_document.find_container(feature.id)=dialog->pending_value();
            expected_document.resolve_constructions(geometry);
            const auto& frame=expected_document.sketches.front();
            const auto expected_local=frame.local_point({12,4,0});
            const auto expected_world=frame.world_point(expected_local[0],expected_local[1]);
            const bool tilted=std::abs(frame.resolved_normal.x)>1e-8||std::abs(frame.resolved_normal.y)>1e-8;
            auto* button=dialog->findChild<QPushButton*>("primitiveOwnSketchButton");check(button,"Placed profile Sketch button missing");button->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Placed Sketch View missing");
            const auto offered=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==original.id&&p.reference.semantic_key=="external_point:"+point_ref;});
            check(offered!=view->mesh().points.end()&&std::hypot(std::hypot(offered->position.x-expected_world.x,offered->position.y-expected_world.y),offered->position.z-expected_world.z)<1e-8,"Returning to Sketch displayed a floating stale external point");
            const auto dependent=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==original.id&&p.reference.semantic_key=="point:"+endpoint;});
            check(dependent!=view->mesh().points.end()&&std::hypot(std::hypot(dependent->position.x-expected_world.x,dependent->position.y-expected_world.y),dependent->position.z-expected_world.z)<1e-8,"Returning to Sketch left the anchored native point behind");
            check(!tilted||std::ranges::none_of(view->mesh().points,[&](const auto& p){return p.reference.owner_id==original.id&&p.reference.semantic_key=="external_point:"+axis_ref;}),"Returning to Sketch displayed an unusable old axis point");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::PrimitivePropertiesDialog* resumed{};
            for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::PrimitivePropertiesDialog*>(d);p&&p->isVisible())resumed=p;
            check(resumed,"Placed profile did not resume Properties");return resumed;
        };
        const auto saved=[&]{return document::PartDocument::load(path);};
        const auto verify=[&](const sketcher::Sketch& current,bool tilted) {
            const auto p=std::ranges::find(current.external_references,point_ref,&sketcher::SketchExternalReference::id);
            check(p!=current.external_references.end()&&!p->broken,"Valid reference lost after placement");
            const auto expected=current.local_point({12,4,0});
            check(std::hypot(p->cached_points.front()[0]-expected[0],p->cached_points.front()[1]-expected[1])<1e-8,"External projection retained its previous local coordinates");
            const auto* q=current.find_point(endpoint);check(q&&std::hypot(q->x-expected[0],q->y-expected[1])<1e-8,"Dependent point did not follow reprojected source");
            const auto a=std::ranges::find(current.external_references,axis_ref,&sketcher::SketchExternalReference::id);
            check(tilted?a==current.external_references.end():a!=current.external_references.end()&&!a->broken,"Unusable axis point was retained or valid axis was removed");
        };
        auto* dialog=open();check(dialog->set_inline_parameter_value("placement:rotation_z",30),"Bound container rotation unavailable");flush();
        dialog=return_sketch(dialog);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Placed profile Save failed");auto rotated=saved();verify(rotated.sketches.front(),false);
        check(rotated.constructions==definitions,"Placement edit changed external sources");
        check(window.execute_console_command("undo").ok&&window.execute_console_command("save").ok,"Placed profile Undo failed");
        check(saved().sketches.front().serialized()==original.serialized(),"Placement Undo lost original projection");
        check(window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Placed profile Redo failed");verify(saved().sketches.front(),false);
        dialog=open();check(dialog->set_inline_parameter_value("placement:rotation_x",30),"Bound container tilt unavailable");flush();
        dialog=return_sketch(dialog);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("save").ok&&saved().sketches.front().serialized()==rotated.sketches.front().serialized(),"Cancel committed reference retirement");
        dialog=open();check(dialog->set_inline_parameter_value("placement:rotation_x",30),"Repeated bound tilt unavailable");flush();
        dialog=return_sketch(dialog);dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Tilted profile Save failed");auto tilted=saved();verify(tilted.sketches.front(),true);
        check(tilted.constructions==definitions,"Reference retirement changed source definitions");
        check(window.execute_console_command("undo").ok&&window.execute_console_command("save").ok,"Reference removal Undo failed");verify(saved().sketches.front(),false);
        check(window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Reference removal Redo failed");verify(saved().sketches.front(),true);
        std::cout<<"Bound container frame edit reprojects valid references, removes unusable axis points, and preserves Cancel, Undo/Redo, sources and native reopen\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
