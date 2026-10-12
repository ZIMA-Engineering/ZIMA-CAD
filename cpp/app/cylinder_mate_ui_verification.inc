static int verify_cylinder_mate(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto part=document::PartDocument::create_default();auto feature=zima::test::circular_feature(part,5,20);
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
        kernel::OcctKernel kernel;kernel::StepProduct product;product.definition_id="gui-cylinder";product.name="Cylinder";
        product.body=kernel.evaluate_history(part.kernel_operations()).back();const auto step=directory/"mate-cylinder.step";
        kernel.export_step(product,document::path_to_utf8(step));
        auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
        const auto source=directory/"mate-cylinder.prtz";imported.document.save(source,imported.calculated);
        auto group=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("Fixed",imported.document.document_id,source,imported.calculated.back());fixed.grounded=true;
        auto moving=assembly::AssemblyDocument::create_part_occurrence("Moving",imported.document.document_id,source,imported.calculated.back());moving.placement.x=35;
        group.components={fixed,moving};const auto file=directory/"mate-cylinder.asmz";group.save(file);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(file.string())),"Cylinder Assembly could not open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Cylinder View missing");view->fit_all();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==moving.occurrence_id&&
            (*i)->data(0,Qt::UserRole+3).toString().endsWith("-occurrence")){row=*i;break;}
        check(row,"Cylinder occurrence Tree row missing");window.show_tree_item_properties(row);flush();
        ComponentPropertiesDialog* dialog{};for(auto* candidate:window.findChildren<QDialog*>())if(auto* found=dynamic_cast<ComponentPropertiesDialog*>(candidate);found&&found->isVisible())dialog=found;
        check(dialog,"Cylinder component properties missing");auto* table=dialog->findChild<QTableWidget*>("componentPlacementTable");check(table,"Cylinder reference table missing");
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
                        face.reference.semantic_key,face.reference.instance_path})==assembly::MateReferenceKind::CylinderFace;}))continue;
                QCursor::setPos(view->mapToGlobal(point.toPoint()));flush();
                mouse(point,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                if(const auto candidate=view->hovered_candidate();candidate&&candidate->instance_path==path&&kinds.kind(*candidate)==assembly::MateReferenceKind::CylinderFace)click=point;
            }
            check(click.has_value(),"Cylinder face was not offered by the actual picker");
            mouse(*click,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*click,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
            check(!dialog->placement_references().empty(),"Cylinder click did not enter a reference row");
            const auto& entered=dialog->placement_references().front();
            check((column==1?entered.component_reference:entered.target_reference).kind==assembly::MateReferenceKind::CylinderFace,"GUI classified an imported cylinder as a plane");
        }
        check(std::abs(dialog->pending_value().placement.x)<1e-7,"Cylinder mate did not move the pending component");
        check(dialog->findChild<QLabel*>("componentPlacementError")->text().isEmpty(),"Cylinder references were rejected");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("save").ok,"Cylinder Assembly save failed");
        const auto saved=assembly::AssemblyDocument::load(file);check(std::abs(saved.components[1].placement.x)<1e-7,"Cylinder mate did not persist the moved component");
        check(!saved.components[1].placement_references.empty(),"Cylinder mate did not persist its reference row");
        check(saved.components[1].placement_references.front().mate_type==assembly::MateKind::AxisCoincident,"Cylinder mate did not persist its axis equation");
        std::cout<<"STEP cylinder GUI picker, reference kind, live movement, OK and native persistence passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Cylinder mate GUI: "<<error.what()<<'\n';return 1;}
}
