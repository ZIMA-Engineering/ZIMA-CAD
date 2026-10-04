int verify_general_surface_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        window.resize(1280,950);window.show();flush();
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;const auto body=graph.create_body("Owned Surface GUI");part.set_body_history(graph);
        const auto path=directory/"general-surface-ui.prtz";part.save(path);
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open General Surface Body");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Cannot activate General Surface Body");flush();
        auto* action=window.findChild<QAction*>("generalSurfaceAction");check(action&&action->isEnabled(),"General Surface action missing");
        auto* surface_mode=window.findChild<QAction*>("applicationModeAction3");
        check(surface_mode&&!surface_mode->isVisible()&&!surface_mode->isEnabled(),"Surface commands still expose a separate application");
        bool grouped=false;
        for(auto* toolbar:window.findChildren<QToolBar*>()) {
            const auto actions=toolbar->actions();const auto position=actions.indexOf(action);
            if(position<2)continue;
            grouped=actions[position-1]->isSeparator()&&actions[position-2]->objectName()=="drillPointAction";
            if(grouped)break;
        }
        check(grouped,"Surface group is not below Drill Point with the common separator");
        const auto editor=[&]{return dynamic_cast<GeneralSurfaceDialog*>(window.findChild<QWidget*>("generalSurfaceDialog"));};
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"General Surface View missing");
        const auto row=[&](const std::string& id,const char* type)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)==type)return *it;
            return nullptr;
        };
        const auto documents=window.execute_console_command("documents").data;
        action->trigger();flush();check(editor()&&editor()->windowType()==Qt::SubWindow,"General Surface creation did not open internal editor");
        auto* parent=editor();const auto id=parent->pending.id;
        check(parent->findChild<QPushButton*>("containerOriginSelectionButton"),"General Surface has no common Origin selection");
        auto* placement=parent->findChild<QTableWidget*>("sweepPlacementReferences");check(placement,"General Surface shared placement missing");
        placement->cellClicked(0,1);flush();
        auto* origin=row(body+":origin","document-origin");check(origin,"General Surface Body Origin missing");tree->setCurrentItem(origin);flush();
        check(parent->first_empty_position_index()==3,"General Surface whole-Origin entry failed");
        check(parent->set_inline_parameter_value("placement:reference_offset:0",12),"General Surface reference correction missing");flush();
        auto* boundaries=parent->findChild<QTableWidget*>("generalSurfaceBoundaries");
        boundaries->cellWidget(3,2)->findChild<QPushButton*>()->click();flush();
        check(parent->pending.general_surface.boundaries.size()==3,"General Surface boundary removal failed");
        const std::array<std::array<double,4>,3> lines{{{0,0,100,0},{100,0,100,80},{100,80,0,80}}};
        for(unsigned i=0;i<3;++i) {
            auto sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[i].sketch_serialized);
            static_cast<void>(sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3]));parent->set_sketch(i,sketch);
        }
        parent->changed();flush();
        parent->edit_sketch(0);flush();check(!parent->isVisible(),"Owned surface Sketch did not open");
        auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Owned Surface Sketch has no Finish action");finish->trigger();flush();
        check(editor()==parent&&parent->isVisible(),"Surface Sketch did not return to same Properties");
        check(window.execute_console_command("documents").data==documents,"Surface Sketch transition committed pending history");
        parent->findChild<QPushButton*>("generalSurfaceAddCurve")->click();flush();
        check(parent->pending.general_surface.boundaries.size()==4&&parent->pending.general_surface.boundaries[3].curve,"Owned Curve creation failed");
        const auto curve_id=parent->pending.general_surface.boundaries[3].curve->id;
        parent->edit_curve(3);flush();check(!parent->isVisible(),"Owned Curve did not enter ordinary editor");
        const auto construction=[&]()->ConstructionPropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<ConstructionPropertiesDialog*>(child);d&&d->isVisible())return d;
            return nullptr;
        };
        auto* curve=construction();check(curve&&curve->construction_kind()==document::ConstructionKind::Curve3D,"Surface owns no ordinary Curve editor");
        for(unsigned i=0;i<2;++i) {
            auto* table=curve->findChild<QTableWidget*>("curve3DPoints");check(table,"Owned Curve has no ordinary Point table");table->cellClicked(i,0);flush();
            auto* point=construction();check(point&&point!=curve&&point->construction_kind()==document::ConstructionKind::Point,"Owned Curve Point did not open shared editor");
            point->findChild<QDoubleSpinBox*>("constructionX")->setValue(0);
            point->findChild<QDoubleSpinBox*>("constructionY")->setValue(i?0:80);
            point->findChild<QDoubleSpinBox*>("constructionZ")->setValue(0);
            point->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(construction()==curve&&curve->curve_point(i),"Owned Point OK did not return to Curve");
            check(window.execute_console_command("documents").data==documents,"Owned Point committed Surface before parent OK");
        }
        curve->findChild<QTableWidget*>("curve3DPoints")->cellClicked(0,0);flush();
        auto* point=construction();check(point&&point!=curve,"Saved draft Point did not reopen");
        const auto point_id=point->construction_id();
        const auto y_dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==point_id&&
            (dimension.reference.semantic_key=="parameter:y"||dimension.reference.semantic_key=="parameter:placement:y");});
        check(y_dimension!=view->mesh().dimensions.end(),"Owned Point Y annotation missing");
        const auto point_frame=parent->pending.placement;
        check(std::hypot(y_dimension->witness_second.x-point_frame.x,y_dimension->witness_second.y-(point_frame.y+80),y_dimension->witness_second.z-point_frame.z)<1e-6,
            "Owned Point annotation escaped its placed Surface frame");
        point->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(construction()==curve,"Owned Point Cancel did not return to Curve");
        curve->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(editor()==parent&&parent->isVisible(),"Owned Curve OK did not return to Surface");
        check(parent->pending.general_surface.boundaries[3].curve->curve_points.size()==2,"Surface lost draft Curve Points");
        check(window.execute_console_command("documents").data==documents,"Owned Curve committed Surface before parent OK");
        const auto expected_placement=parent->pending.placement;
        check(std::abs(std::hypot(expected_placement.x,expected_placement.y,expected_placement.z)-12)<1e-8,"Whole-Origin correction did not resolve in its selected direction");
        window.grab().save(QString::fromStdString((directory/"general-surface-properties.png").string()));
        parent->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor(),"General Surface calculation failed on OK");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);
        check(saved.find_container(id)&&!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().surface_area-8000)<.01,"General Surface lost calculated area on save");
        check(saved.sketches.empty()&&saved.constructions.empty(),"Owned Surface leaked geometry to document root");
        const auto stored=*saved.find_container(id);check(stored.general_surface.boundaries[3].curve->id==curve_id&&
            std::hypot(stored.placement.x-expected_placement.x,stored.placement.y-expected_placement.y,stored.placement.z-expected_placement.z)<1e-8,"General Surface lost native Curve or placement");
        const auto before=window.execute_console_command("documents").data;
        check(row(id,"part-container"),"General Surface Tree missing");const auto scene_before=view->base_mesh_revision();
        window.show_tree_item_properties(row(id,"part-container"));flush();check(editor(),"General Surface edit did not reopen");
        check(view->base_mesh_revision()==scene_before+1,"General Surface opening published an unused intermediate scene");
        editor()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!editor()&&window.execute_console_command("documents").data==before,"General Surface unchanged OK created a transaction");
        window.show_tree_item_properties(row(id,"part-container"));flush();check(editor(),"General Surface Cancel editor missing");
        editor()->edit_curve(3);flush();curve=construction();check(curve,"Saved owned Curve cannot reopen");
        curve->findChild<QDoubleSpinBox*>("constructionX")->setValue(17);curve->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(editor()&&editor()->pending.general_surface==stored.general_surface,"Owned Curve Cancel changed Surface draft");
        editor()->findChild<QDoubleSpinBox*>("sweepRotation2")->setValue(37);editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==before,"General Surface Cancel changed history");
        const auto stored_point=stored.general_surface.boundaries[3].curve->curve_points.front().id;
        check(row(stored_point,"curve3d-point"),"Owned Point has no Tree row");
        window.show_tree_item_properties(row(stored_point,"curve3d-point"));flush();
        point=construction();check(point&&point->construction_id()==stored_point,"Tree Point Properties did not enter the owned editor");
        point->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        curve=construction();check(curve&&curve->construction_id()==curve_id,"Tree Point Cancel did not return to owned Curve");
        curve->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(editor(),"Tree Curve Cancel did not return to Surface");editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==before,"Tree owned geometry editing committed pending history");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(!document::PartDocument::load(path).find_container(id),"General Surface Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).find_container(id),"General Surface Redo failed");
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
