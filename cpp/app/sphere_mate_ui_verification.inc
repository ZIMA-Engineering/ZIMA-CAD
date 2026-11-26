static int verify_sphere_mate(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& root) {
    try {
        const auto directory=root/("sphere_mate-"+QUuid::createUuid().toString(QUuid::Id128).toStdString());
        std::filesystem::create_directories(directory);
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        kernel::OcctKernel kernel;kernel::RevolutionRequest sphere;
        sphere.surface_result=true;sphere.axis_direction={0,1,0};sphere.profile_region_id="sphere-region";
        sphere.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{0,-10,0},{10,0,0},{0,10,0}}}};
        sphere.outer_edge_source_ids={"meridian"};sphere.outer_vertex_source_ids={"south"};sphere.open_profile_end_id="north";
        kernel::StepProduct product;product.definition_id="gui-sphere";product.name="Sphere";
        product.body=kernel.evaluate_history({{"sphere",sphere}}).back();const auto step=directory/"mate-sphere.step";
        kernel.export_step(product,document::path_to_utf8(step));
        auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
        const auto source=directory/"mate-sphere.prtz";imported.document.save(source,imported.calculated);
        auto group=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("Fixed",imported.document.document_id,source,imported.calculated.back());fixed.grounded=true;
        auto moving=assembly::AssemblyDocument::create_part_occurrence("Moving",imported.document.document_id,source,imported.calculated.back());moving.placement.x=35;
        group.components={fixed,moving};const auto file=directory/"mate-sphere.asmz";group.save(file);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(file.string())),"Sphere Assembly could not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Sphere View missing");view->fit_all();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==moving.occurrence_id&&
            (*i)->data(0,Qt::UserRole+3).toString().endsWith("-occurrence")){row=*i;break;}
        check(row,"Sphere occurrence Tree row missing");window.show_tree_item_properties(row);flush();
        ComponentPropertiesDialog* dialog{};for(auto* candidate:window.findChildren<QDialog*>())if(auto* found=dynamic_cast<ComponentPropertiesDialog*>(candidate);found&&found->isVisible())dialog=found;
        check(dialog,"Sphere component properties missing");auto* table=dialog->findChild<QTableWidget*>("componentPlacementTable");check(table,"Sphere reference table missing");
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
        for(const auto column:{1,2}) {
            table->cellClicked(0,column);flush();ComponentReferenceKindIndex kinds;kinds.rebuild(view->mesh().original_references);
            const auto path=assembly::InstancePath{{column==1?moving.occurrence_id:fixed.occurrence_id}}.encoded();std::optional<QPointF> click;
            const viewer::PickingIndex picking(view->mesh());
            for(int y=15;y<view->height()&&!click;y+=12)for(int x=15;x<view->width()&&!click;x+=12) {
                const QPointF point(x,y);const auto ray=view->ray_at(point);if(!ray)continue;
                const auto faces=viewer::ordered_ray_candidates(view->mesh(),ray->first,ray->second,true,&picking);
                if(std::ranges::none_of(faces,[&](const auto& face){return face.reference.instance_path==path&&
                    kinds.kind({viewer::CandidateKind::Face,face.distance,face.triangle,face.reference.owner_id,
                        face.reference.semantic_key,face.reference.instance_path})==assembly::MateReferenceKind::SphereFace;}))continue;
                QCursor::setPos(view->mapToGlobal(point.toPoint()));flush();
                mouse(point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                if(const auto candidate=view->hovered_candidate();candidate&&candidate->instance_path==path&&kinds.kind(*candidate)==assembly::MateReferenceKind::SphereFace)click=point;
            }
            check(click.has_value(),"Sphere face was not offered by the actual picker");
            mouse(*click,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*click,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
            check(!dialog->placement_references().empty(),"Sphere click did not enter a reference row");
            const auto& entered=dialog->placement_references().front();
            check((column==1?entered.component_reference:entered.target_reference).kind==assembly::MateReferenceKind::SphereFace,"GUI classified an imported sphere as a plane");
        }
        check(std::abs(dialog->pending_value().placement.x)<1e-7,"Sphere mate did not move the pending component");
        check(dialog->findChild<QLabel*>("componentPlacementError")->text().isEmpty(),"Sphere references were rejected");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Sphere Assembly save failed");
        const auto saved=assembly::AssemblyDocument::load(file);check(std::abs(saved.components[1].placement.x)<1e-7,"Sphere mate did not persist the moved component");
        check(!saved.components[1].placement_references.empty(),"Sphere mate did not persist its reference row");
        check(saved.components[1].placement_references.front().mate_type==assembly::MateKind::PointCoincident,"Sphere mate did not persist its center equation");
        check(saved.component_constraint_state(moving.occurrence_id).remaining_dof==3,"Sphere joint does not retain free rotation");
        std::cout<<"STEP sphere GUI picker, reference kind, live movement, OK and native persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Sphere mate GUI: "<<error.what()<<'\n';return 1;}
}
