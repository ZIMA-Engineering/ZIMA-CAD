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
        check(boundaries->viewport()->height()>=5*boundaries->rowHeight(0),"General Surface initially shows fewer than five boundary rows");
        check(boundaries->columnCount()==5&&boundaries->rowCount()==5&&boundaries->verticalHeader()->isVisible()&&
            boundaries->horizontalHeaderItem(0)->text().isEmpty(),"General Surface retained custom numbering or plane/offset columns");
        boundaries->cellWidget(3,1)->findChild<QPushButton*>()->click();flush();
        check(parent->pending.general_surface.boundaries.size()==3,"General Surface boundary removal failed");
        const std::array<std::array<double,4>,3> lines{{{0,0,100,0},{100,0,100,80},{100,80,0,80}}};
        for(unsigned i=0;i<3;++i) {
            auto sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[i].sketch_serialized);
            static_cast<void>(sketch.add_segment(lines[i][0],lines[i][1],lines[i][2],lines[i][3]));parent->set_sketch(i,sketch);
        }
        parent->changed();flush();
        const auto inspection_revision=view->base_mesh_revision();
        const auto first_sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[0].sketch_serialized).id;
        const auto second_sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[1].sketch_serialized).id;
        const auto third_sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[2].sketch_serialized).id;
        const auto context_visible=[&](const std::string& owner) {
            return std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==owner||edge.display_owner_id==owner;})||
                std::ranges::any_of(view->transient_edges(),[&](const auto& edge){return edge.reference.owner_id==owner||edge.display_owner_id==owner;});
        };
        check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),
            "Uninspected Surface boundaries are not visible as context");
        const auto uninspected_edges=view->transient_edges();
        const auto uninspected_points=view->transient_model_points();
        check(std::ranges::any_of(uninspected_points,[&](const auto& point){return point.reference.owner_id==first_sketch&&!point.construction;}),
            "Surface draft lost ordinary Sketch point presentation");
        const auto check_inspection=[&](const std::string& owner) {
            check(view->transient_model_points().size()==uninspected_points.size(),"Inspection hid or added native draft points");
            for(std::size_t i=0;i<uninspected_points.size();++i)check(view->transient_model_points()[i].position==uninspected_points[i].position&&
                view->transient_model_points()[i].reference==uninspected_points[i].reference&&view->transient_model_points()[i].construction==uninspected_points[i].construction,
                "Inspection changed a native draft point or its ordinary role");
            check(view->transient_edges().size()==uninspected_edges.size(),"Boundary inspection changed visible geometry");
            for(std::size_t i=0;i<uninspected_edges.size();++i) {
                const auto& edge=view->transient_edges()[i];const auto& original=uninspected_edges[i];
                check(edge.reference==original.reference&&edge.points==original.points,"Boundary inspection moved or replaced its geometry");
                check(edge.color==(edge.reference.owner_id==owner?interaction::selected.name().toStdString():original.color),
                    "Boundary inspection did not recolour only its exact owned wire");
            }
        };
        boundaries->cellWidget(0,4)->findChild<QToolButton*>()->click();flush();
        check_inspection(first_sketch);
        boundaries->cellWidget(1,4)->findChild<QToolButton*>()->click();boundaries->cellWidget(0,4)->findChild<QToolButton*>()->click();flush();
        check_inspection(second_sketch);
        boundaries->cellWidget(1,4)->findChild<QToolButton*>()->click();flush();
        check_inspection({});
        check(view->base_mesh_revision()==inspection_revision&&window.execute_console_command("documents").data==documents,
            "Boundary inspection changed the base scene or committed pending history");
        const auto original_boundaries=parent->pending.general_surface;
        boundaries->cellClicked(0,3);flush();
        auto* sketch_properties=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(sketch_properties&&sketch_properties->isVisible()&&!parent->isVisible(),"Owned boundary field did not open Sketch Properties");
        check(context_visible(second_sketch)&&context_visible(third_sketch),"Sketch Properties hid other Surface boundaries");
        check(sketch_properties->feature_kind()==document::FeatureKind::Feature&&
            sketch_properties->findChild<QComboBox*>("featureType")->currentIndex()==static_cast<int>(document::FeatureType::Sketch)&&
            !sketch_properties->findChild<QComboBox*>("featureType")->isEnabled()&&
            sketch_properties->findChild<QTableWidget*>("primitiveReferenceTable")->isVisible(),
            "Owned Sketch is not a complete placed Feature with a fixed type");
        sketch_properties->findChild<QDoubleSpinBox*>("featureProfileOffset")->setValue(12);
        check(sketch_properties->set_inline_parameter_value("placement:x",7),"Owned Sketch has no independent placement");
        sketch_properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(parent->isVisible()&&parent->pending.general_surface==original_boundaries,"Owned Sketch Properties Cancel changed the Surface draft");
        auto positioned_sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[0].sketch_serialized);
        positioned_sketch.points[0].x-=13;positioned_sketch.points[1].x-=13;
        parent->set_sketch(0,positioned_sketch);
        boundaries->cellClicked(0,3);flush();
        sketch_properties=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(sketch_properties->set_inline_parameter_value("placement:x",13),"Owned Sketch numeric placement failed");flush();
        window.grab().save(QString::fromStdString((directory/"general-surface-owned-sketch-properties.png").string()));
        sketch_properties->findChild<QPushButton*>("featureSketchButton")->click();flush();check(!parent->isVisible(),"Owned surface Sketch did not open");
        check(context_visible(second_sketch)&&context_visible(third_sketch),"Sketcher hid other Surface boundaries");
        auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Owned Surface Sketch has no Finish action");finish->trigger();flush();
        check(sketch_properties->isVisible()&&!parent->isVisible(),"Drawing did not return to the owned Sketch Properties");
        sketch_properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(editor()==parent&&parent->isVisible(),"Surface Sketch Properties did not return to same Surface");
        check(parent->pending.general_surface.boundaries[0].sketch_feature->placement.x==13,
            "Owned Sketch placement was not retained inside the Surface draft");
        check(window.execute_console_command("documents").data==documents,"Surface Sketch transition committed pending history");
        auto* arrow=boundaries->cellWidget(3,1)->findChild<QLabel*>();check(arrow,"General Surface trailing row arrow missing");
        const QPointF arrow_position(arrow->rect().center());
        QMouseEvent add_sketch(QEvent::MouseButtonRelease,arrow_position,arrow_position,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(arrow,&add_sketch);flush();
        sketch_properties=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(sketch_properties&&sketch_properties->isVisible()&&parent->pending.general_surface.boundaries.size()==4,"Entry arrow did not add a Sketch and open its Properties");
        check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),"New Sketch Properties hid preceding boundaries");
        auto* own_references=sketch_properties->findChild<QTableWidget*>("primitiveReferenceTable");
        own_references->cellClicked(0,1);flush();
        origin=row(body+":origin","document-origin");tree->setCurrentItem(origin);flush();
        check(sketch_properties->first_empty_position_index()==3,"Owned Sketch whole-Origin placement failed");
        auto* sketch_plane=sketch_properties->findChild<QComboBox*>("featureProfilePlane");
        sketch_plane->setCurrentIndex(sketch_plane->findData(static_cast<int>(sketcher::SketchPlane::XZ)));
        sketch_properties->findChild<QDoubleSpinBox*>("featureProfileOffset")->setValue(12);
        const auto own_placement_references=sketch_properties->pending_value().placement.references;
        sketch_properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        const auto offset_sketch=sketcher::Sketch::from_serialized(parent->pending.general_surface.boundaries[3].sketch_serialized);
        check(offset_sketch.plane==sketcher::SketchPlane::XZ&&offset_sketch.plane_offset==12&&offset_sketch.owner_container_id==parent->pending.general_surface.boundaries[3].sketch_feature->id&&
            offset_sketch.plane_reference_owner_id==parent->pending.general_surface.boundaries[3].sketch_feature->container_origin.id,"Owned Sketch Properties lost local plane/offset or ownership");
        check(!own_placement_references.empty()&&parent->pending.general_surface.boundaries[3].sketch_feature->placement.references==own_placement_references,
            "Owned Sketch lost its placement references on inner OK");
        boundaries->cellWidget(3,1)->findChild<QPushButton*>()->click();flush();
        qobject_cast<QComboBox*>(boundaries->cellWidget(3,2))->setCurrentIndex(1);boundaries->cellClicked(3,3);flush();
        check(parent->pending.general_surface.boundaries.size()==4&&parent->pending.general_surface.boundaries[3].curve,"Owned Curve creation failed");
        const auto curve_id=parent->pending.general_surface.boundaries[3].curve->id;
        check(!parent->isVisible(),"Owned Curve did not enter ordinary editor");
        const auto construction=[&]()->ConstructionPropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<ConstructionPropertiesDialog*>(child);d&&d->isVisible())return d;
            return nullptr;
        };
        auto* curve=construction();check(curve&&curve->construction_kind()==document::ConstructionKind::Curve3D,"Surface owns no ordinary Curve editor");
        check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),"New Curve Properties hid preceding boundaries");
        check(!curve->findChild<QTableWidget*>("constructionReferenceTable")->isVisible()&&
            !curve->findChild<QDoubleSpinBox*>("constructionX")->isVisible(),"Owned Curve exposed an independent container placement");
        window.grab().save(QString::fromStdString((directory/"general-surface-owned-curve-properties.png").string()));
        for(unsigned i=0;i<2;++i) {
            auto* table=curve->findChild<QTableWidget*>("curve3DPoints");check(table,"Owned Curve has no ordinary Point table");table->cellClicked(i,0);flush();
            auto* point=construction();check(point&&point!=curve&&point->construction_kind()==document::ConstructionKind::Point,"Owned Curve Point did not open shared editor");
            check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),"New Curve Point Properties hid preceding boundaries");
            point->findChild<QDoubleSpinBox*>("constructionX")->setValue(0);
            point->findChild<QDoubleSpinBox*>("constructionY")->setValue(i?0:80);
            point->findChild<QDoubleSpinBox*>("constructionZ")->setValue(0);
            check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),"Curve Point preview hid preceding boundaries");
            point->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(construction()==curve&&curve->curve_point(i),"Owned Point OK did not return to Curve");
            check(window.execute_console_command("documents").data==documents,"Owned Point committed Surface before parent OK");
        }
        curve->findChild<QTableWidget*>("curve3DPoints")->cellClicked(0,0);flush();
        auto* point=construction();check(point&&point!=curve,"Saved draft Point did not reopen");
        check(context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),"Existing Curve Point Properties hid preceding boundaries");
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
        const auto curve_entity=parent->pending.general_surface.boundaries[3].curve->entity_id;
        const auto curve_inspection_revision=view->base_mesh_revision();
        const auto before_curve_inspection=view->transient_edges();
        boundaries->cellWidget(3,4)->findChild<QToolButton*>()->click();flush();
        bool inspected_curve_wire=false;
        for(const auto& edge:view->transient_edges())if(edge.reference.owner_id==curve_entity) {
            inspected_curve_wire=true;check(edge.color==interaction::selected.name().toStdString(),"Curve eye did not inspect its native wire");
        }
        check(inspected_curve_wire&&context_visible(first_sketch)&&context_visible(second_sketch)&&context_visible(third_sketch),
            "Curve inspection hid context or missed the curve entity");
        boundaries->cellWidget(3,4)->findChild<QToolButton*>()->click();flush();
        check(view->base_mesh_revision()==curve_inspection_revision&&view->transient_edges().size()==before_curve_inspection.size(),
            "Curve eye republished the scene or hid a boundary");
        for(std::size_t i=0;i<before_curve_inspection.size();++i)check(view->transient_edges()[i].color==before_curve_inspection[i].color&&
            view->transient_edges()[i].reference==before_curve_inspection[i].reference&&view->transient_edges()[i].points==before_curve_inspection[i].points,
            "Turning off Curve inspection changed its visible wire");
        const auto before_sketch_context=parent->pending.general_surface;
        parent->edit_sketch_properties(1);flush();
        sketch_properties=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(sketch_properties&&context_visible(curve_id),"Sketch Properties hid an existing Curve boundary");
        sketch_properties->findChild<QPushButton*>("featureSketchButton")->click();flush();
        check(context_visible(first_sketch)&&context_visible(third_sketch)&&context_visible(curve_id),
            "Sketcher did not retain mixed Sketch and Curve context");
        finish->trigger();flush();sketch_properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(editor()==parent&&parent->pending.general_surface==before_sketch_context,
            "Mixed boundary context editing changed the draft on Cancel");
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
        check(stored.general_surface.boundaries[0].sketch_feature->placement.x==13&&
            sketcher::Sketch::from_serialized(stored.general_surface.boundaries[0].sketch_serialized).resolved_origin.x==13,
            "Owned Sketch placement was not persisted relative to the Surface Origin");
        const auto definition_hidden=[&] {
            for(const auto& boundary:stored.general_surface.boundaries) {
                const auto owner=boundary.curve?boundary.curve->entity_id:boundary.sketch_feature->feature.sketch_id;
                if(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==owner;})||
                    std::ranges::any_of(view->mesh().points,[&](const auto& point){return point.reference.owner_id==owner;}))return false;
            }
            return true;
        };
        check(definition_hidden(),"Committed General Surface still displays authored boundary wires or points");
        check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;}),
            "Hiding General Surface definitions hid the resulting surface");
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
        const auto selection_revision=view->base_mesh_revision();
        for(const unsigned index : {0U,3U}) {
            QTreeWidgetItem* boundary_row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                (*it)->data(0,Qt::UserRole+3)=="general-surface-boundary"&&(*it)->data(0,Qt::UserRole+6).toUInt()==index)boundary_row=*it;
            check(boundary_row,"Owned Surface boundary has no selectable Tree row");
            tree->setCurrentItem(boundary_row);flush();
            const auto& boundary=stored.general_surface.boundaries[index];
            const auto owner=boundary.curve?boundary.curve->entity_id:sketcher::Sketch::from_serialized(boundary.sketch_serialized).id;
            const auto selected=view->confirmed_candidate();const auto wire=view->confirmed_component_wire();
            check(selected&&selected->owner_id==id&&selected->semantic_key=="component:general-surface-boundary:"+owner,
                "Tree did not confirm the exact owned Surface boundary");
            check(!wire.empty()&&std::ranges::all_of(wire,[&](const auto& edge){return edge.reference.owner_id==owner;}),
                "Tree boundary selection highlighted unrelated Surface geometry");
        }
        tree->clearSelection();flush();
        check(!view->confirmed_candidate()&&view->confirmed_component_wire().empty(),"Clearing Tree retained an owned boundary selection");
        tree->setCurrentItem(row(id,"part-container"));flush();
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==id&&view->confirmed_component_wire().empty(),
            "Selecting the outer Surface retained its child boundary wire");
        check(view->base_mesh_revision()==selection_revision&&window.execute_console_command("documents").data==before,
            "Owned boundary selection regenerated geometry or changed history");
        const auto owned_row=[&](unsigned index)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                (*it)->data(0,Qt::UserRole+3)=="general-surface-boundary"&&(*it)->data(0,Qt::UserRole+6).toUInt()==index)return *it;
            return nullptr;
        };
        const auto fingerprint=cache.back().source_fingerprint;
        for(const unsigned index:{0U,3U}) {
            check(owned_row(index)->foreground(0).color()==QColor(125,125,125),"Hidden owned definition is not gray in Tree");
            toggle_surface_wire_tree_ui(application,tree,owned_row(index),false);flush();
            check(owned_row(index)->foreground(0).color()!=QColor(125,125,125),"Shown owned definition remains gray in Tree");
            const auto& boundary=stored.general_surface.boundaries[index];
            const auto owner=boundary.curve?boundary.curve->entity_id:boundary.sketch_feature->feature.sketch_id;
            check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==owner;}),
                "Show did not display an owned Surface definition");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> shown_cache;const auto shown=document::PartDocument::load(path,&shown_cache);
            const auto object=boundary.curve?boundary.curve->id:owner;
            check(shown.surface_wire_visibility.at(object)&&shown_cache.back().source_fingerprint==fingerprint&&
                shown_cache.back().surface_area==cache.back().surface_area,"Show changed calculated geometry or lost its native visibility override");
            verify_surface_wire_reopened_ui(application,path,owner,true);flush();
            window.findChild<QAction*>("undoAction")->trigger();flush();check(definition_hidden(),"Visibility Undo retained a Surface definition");
            check(owned_row(index)->foreground(0).color()==QColor(125,125,125),"Visibility Undo did not restore gray Tree state");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==owner;}),"Visibility Redo did not restore the definition");
            window.findChild<QAction*>("undoAction")->trigger();flush();
        }
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(window.execute_console_command("documents").data==before&&definition_hidden(),"Visibility inspection changed the Surface definition/history");
        verify_surface_wire_reopened_ui(application,path,curve_entity,false);flush();
        QTreeWidgetItem* owned_sketch_row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
            (*it)->data(0,Qt::UserRole+3)=="general-surface-boundary"&&(*it)->data(0,Qt::UserRole+6).toUInt()==0)owned_sketch_row=*it;
        check(owned_sketch_row,"Owned Sketch has no Tree row");window.show_tree_item_properties(owned_sketch_row);flush();
        sketch_properties=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(sketch_properties&&sketch_properties->isVisible(),"Tree Sketch Properties did not use the ordinary owned editor");
        sketch_properties->findChild<QDoubleSpinBox*>("featureProfileOffset")->setValue(25);
        sketch_properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(editor()&&editor()->pending.general_surface==stored.general_surface,"Tree Sketch Cancel changed Surface draft");
        editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==before,"Tree Sketch Properties committed pending history");
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
