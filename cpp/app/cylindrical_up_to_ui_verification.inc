// Exercise the same exact preview used by a single curved limit in Properties.
Q_NEVER_INLINE static int verify_cylindrical_up_to_ui(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto wait=[&](PrimitivePropertiesDialog* dialog) {
            QElapsedTimer timer;timer.start();
            while(dialog->property("featureExactPreviewState")=="pending"&&timer.elapsed()<30000) {
                application.processEvents();QThread::msleep(5);
            }
            check(dialog->property("featureExactPreviewState")=="ready","Single-cylinder preview is not ready");
        };
        for(double side:{-1.,1.}) {
            auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
            const auto body_id=graph.create_body("Cylinder");
            auto circle=sketcher::Sketch::create_default();static_cast<void>(circle.add_circle(0,0,10));
            auto stock=document::PartDocument::create_feature_container(circle.id);circle.owner_container_id=stock.id;
            stock.feature.symmetric=true;stock.feature.sides[0].length=20;
            part.history={stock};part.sketches={circle};graph.insert({document::PartHistoryKind::Feature,stock.id});
            part.set_body_history(graph);part.resolve_constructions();kernel::OcctKernel engine;
            auto geometry=workspace::calculate_part_with_resolved_references(engine,part);
            document::ExtrusionParameters::EndTarget limit;
            for(const auto& face:geometry.back().mesh.original_references.triangle_references)
                if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){limit.reference=face;break;}
            check(limit.reference.valid(),"Cylinder fixture has no curved face");
            limit=workspace::resolve_profile_end_target(part,geometry,body_id,limit);
            check(limit.kind==document::EndTargetKind::Face,"Cylinder was classified as a plane");
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(-2,-5,2,5));
            auto feature=document::PartDocument::create_feature_container(sketch.id);sketch.owner_container_id=feature.id;
            feature.placement.y=side*20;feature.placement.absolute_rotation_x=side*90;
            feature.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
            feature.feature.sides[0].targets={limit};feature.name="Cylinder preview";feature.feature.automatic_name.clear();
            part.history.push_back(feature);part.sketches.push_back(sketch);
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            geometry=workspace::calculate_part_with_resolved_references(engine,part);
            for(const auto& [owner,error]:geometry.back().calculation_errors)std::cerr<<owner<<": "<<error<<'\n';
            check(geometry.back().calculation_errors.empty(),"Cylinder fixture calculation failed");
            const auto file=directory/(side<0?"cylinder-negative.prtz":"cylinder-positive.prtz");part.save(file,geometry);
            check(window.open_document_path(QString::fromStdString(file.string())),"Cylinder preview copy did not open");
            window.showMaximized();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Cylinder View or Tree is missing");
            const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
                const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
                if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
            };
            const auto get=[&]{return run("feature.get",{{"container",feature.id}});};
            const auto original=get();
            const auto edit=[&] {
                QTreeWidgetItem* row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
                check(row,"Cylinder Feature is absent from Tree");window.show_tree_item_properties(row);flush();
                auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(dialog,"Cylinder Properties did not open");
                QEventLoop settled;QTimer::singleShot(220,&settled,&QEventLoop::quit);settled.exec();
                if(dialog->property("featureExactPreviewCalculations").toInt()!=0) {
                    auto before=document::PartDocument::load(file);auto after=before;
                    *after.find_container(feature.id)=dialog->pending_value();
                    std::cerr<<"Cylinder opening definition changes: "<<commands::Json::diff(before.serialized(),after.serialized()).dump()<<'\n';
                }
                check(dialog->property("featureExactPreviewCalculations").toInt()==0,"Opening Cylinder Properties calculated geometry");
                wait(dialog);return dialog;
            };
            const auto wire_equations=[&](const auto& wire,double correction) {
                check(!wire.empty(),"Cylinder preview wire is empty");bool contact=false,start=false;
                for(const auto& edge:wire) {
                    check(edge.reference.owner_id==feature.id&&!edge.parameter_seam,"Cylinder preview includes foreign or seam edges");
                    for(const auto p:edge.points) {
                        check(std::abs(p.x)<=2+1e-5&&std::abs(p.z)<=5+1e-5,"Cylinder preview exceeds its profile");
                        const bool on_start=std::abs(p.y-side*20)<1e-5;
                        const double y=p.y+side*correction;
                        const bool on_contact=std::abs(p.x*p.x+y*y-100)<1e-4&&side*y>0;
                        check(on_start||on_contact,"Cylinder preview violates the independent supporting surface equation");
                        start|=on_start;contact|=on_contact;
                    }
                }
                check(start&&contact,"Cylinder preview has no start or curved end boundary");
            };
            auto* dialog=edit();wire_equations(view->transient_edges(),0);
            const auto scenes=view->base_mesh_revision();
            auto* correction=dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0");check(correction,"Correction field is missing");
            for(double value:{.25,.5,1.25})correction->setValue(value);
            wait(dialog);wire_equations(view->transient_edges(),1.25);
            check(dialog->property("featureExactPreviewCalculations").toInt()==1,"Rapid Cylinder edits were not debounced");
            check(view->base_mesh_revision()==scenes&&get().at("revision")==original.at("revision"),
                "Cylinder draft republished the base scene or committed the document");
            correction->setValue(-1.);wait(dialog);wire_equations(view->transient_edges(),-1.);
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(get().at("parameters")==original.at("parameters")&&get().at("revision")==original.at("revision"),"Cylinder Cancel changed its definition");
            dialog=edit();wire_equations(view->transient_edges(),0);
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(get().at("revision")==original.at("revision"),"Unchanged Cylinder OK created an Undo step");
            dialog=edit();dialog->findChild<QDoubleSpinBox*>("featureSideCorrection0")->setValue(1.25);
            wait(dialog);wire_equations(view->transient_edges(),1.25);
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();run("save");
            const auto accepted=get();std::vector<kernel::BodyResult> saved;
            const auto reopened=document::PartDocument::load(file,&saved);document::DocumentSession session(reopened,saved);
            wire_equations(authored_feature_wire(session,feature.id),1.25);
            dialog=edit();wire_equations(view->transient_edges(),1.25);
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            window.show_parameter_dimensions(feature.id);flush();wire_equations(view->container_inspection_wire(),1.25);
            check(get().at("revision")==accepted.at("revision"),"Cylinder inspection changed the document");
            run("undo");check(get().at("parameters")==original.at("parameters"),"Cylinder Undo lost its original limit");
            run("redo");check(get().at("parameters")==accepted.at("parameters"),"Cylinder Redo lost its corrected limit");
            view->grabFramebuffer().save(QString::fromStdString((directory/(side<0?"cylinder-negative.png":"cylinder-positive.png")).string()));
        }
        std::cout<<"Single-cylinder GUI preview: both contact sides, exact equations, debouncing, unchanged opening/OK, Cancel, commit, persistence, authored inspection and Undo/Redo passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
