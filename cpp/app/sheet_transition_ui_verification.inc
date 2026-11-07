int verify_sheet_transition_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_TRANSITION_SOURCE")) {
            std::vector<kernel::BodyResult> bodies;
            auto part=document::PartDocument::load(std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_TRANSITION_SOURCE").toStdString()),&bodies);
            const auto source=std::ranges::find(part.history,document::FeatureKind::SheetTransition,&document::HistoryContainer::feature_kind);
            check(source!=part.history.end(),"Saved transition missing");const auto id=source->id;
            const auto path=directory/"transition-source-copy.prtz";part.save(path,bodies);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1400,1000);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Saved transition cannot open");flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Saved transition View missing");
            view->fit_all();flush();
            window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-source-native.png"))));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){row=*it;break;}
            check(row,"Saved transition Tree row missing");window.show_tree_item_properties(row);flush();
            auto* dialog=window.findChild<QDialog*>("sheetTransitionDialog");check(dialog,"Saved transition Properties missing");
            window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-source-preview.png"))));
            dialog->reject();flush();
            check(document::PartDocument::load(path).serialized()==part.serialized(),"Saved transition inspection changed source");
            std::cout<<"Saved transition native/preview inspection and Cancel passed\n";return 0;
        }
        auto part=document::PartDocument::load(std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_SHEET_TRANSITION").toStdString()));
        document::set_sheet_metal_defaults(part,{2.5,.5});
        const auto source=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetTransition;});
        check(source!=part.history.end(),"Transition GUI fixture has no transition");const auto initial=*source;
        const auto body=part.body_owner_for_object(initial.id)->scope.id;part.erase_history_object(initial.id);
        kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto path=directory/"transition-ui.prtz";part.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1400,1000);window.show();
        check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Transition fixture did not open");flush();
        check(activate_test_body(application,window,body),"Transition Body could not activate");
        window.findChild<QAction*>("applicationModeAction2")->trigger();flush();
        auto* action=window.findChild<QAction*>("sheetTransitionAction");check(action&&action->isEnabled()&&!action->icon().isNull(),"Transition action unavailable");
        const auto dialog=[&]{auto* d=window.findChild<QDialog*>("sheetTransitionDialog");check(d&&d->isVisible(),"Transition dialog did not open");return d;};
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        const auto verify_ordinary_interaction=[&](const document::HistoryContainer& target,const char* label) {
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Transition interaction workspace missing");
            view->fit_all();view->set_reference_visibility(viewer::ReferenceVisibility::AxisPoints,false);flush();
            const auto document_state=window.execute_console_command("documents").data;
            const auto mesh_revision=view->base_mesh_revision();
            const auto project=[&](kernel::Vec3 world) {
                const auto center=view->ray_at({view->width()*.5,view->height()*.5});
                check(center.has_value(),"Transition camera ray missing");
                const auto normal=center->second;
                const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
                const auto sub=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
                const auto hit=[&](QPointF screen) {
                    const auto ray=view->ray_at(screen);check(ray.has_value(),"Transition projection ray missing");
                    const double t=dot(sub(world,ray->first),normal)/dot(ray->second,normal);
                    return kernel::Vec3{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,ray->first.z+t*ray->second.z};
                };
                const auto origin=hit({0,0}),u=sub(hit({100,0}),origin),v=sub(hit({0,100}),origin),r=sub(world,origin);
                const double uu=dot(u,u),vv=dot(v,v),uv=dot(u,v),det=uu*vv-uv*uv;
                return QPointF(100*(dot(r,u)*vv-dot(r,v)*uv)/det,100*(dot(r,v)*uu-dot(r,u)*uv)/det);
            };
            std::optional<QPointF> point;
            const auto& references=view->mesh().original_references;
            for(std::size_t triangle=0;triangle<references.triangle_references.size()&&!point;++triangle) {
                const auto& reference=references.triangle_references[triangle];
                const bool transition_region=reference.sheet_owner.starts_with(target.id+":panel:")||
                    reference.sheet_owner.starts_with(target.id+":bend:");
                if(!transition_region||(reference.sheet_role!=kernel::SheetFaceRole::SideA&&
                        reference.sheet_role!=kernel::SheetFaceRole::SideB)||triangle*3+2>=references.triangles.size())continue;
                kernel::Vec3 center{};bool valid=true;
                for(std::size_t corner=0;corner<3;++corner) {
                    const auto index=references.triangles[triangle*3+corner];
                    if(index>=references.vertices.size()){valid=false;break;}
                    const auto& p=references.vertices[index];center.x+=p.x/3;center.y+=p.y/3;center.z+=p.z/3;
                }
                if(!valid)continue;
                const auto screen=project(center);
                if(!view->rect().contains(screen.toPoint()))continue;
                const auto candidates=view->selection_candidates_at(screen);
                if(std::ranges::any_of(candidates,[&](const auto& candidate) {
                    return candidate.kind==viewer::CandidateKind::Container&&candidate.owner_id==target.id&&!candidate.marker_derived;
                }))point=screen;
            }
            check(point.has_value(),label);
            QMouseEvent offer(QEvent::MouseMove,*point,QPointF(view->mapToGlobal(point->toPoint())),
                Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&offer);flush();
            const auto wanted=[&] {
                const auto candidate=view->offered_candidate();
                return candidate&&candidate->kind==viewer::CandidateKind::Container&&candidate->owner_id==target.id;
            };
            for(unsigned cycle=0;cycle<64&&!wanted();++cycle)for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*point,QPointF(view->mapToGlobal(point->toPoint())),Qt::RightButton,
                    type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
            check(wanted(),"Transition cannot be offered by the ordinary View picker");
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*point,QPointF(view->mapToGlobal(point->toPoint())),Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
            auto* current=tree->currentItem();const auto selected=view->confirmed_candidate();
            check(selected&&selected->owner_id==target.id&&current&&
                current->data(0,Qt::UserRole).toString().toStdString()==target.id&&
                current->data(0,Qt::UserRole+3)==QStringLiteral("part-container"),
                "Transition View selection did not select its owning Tree row");
            static_cast<void>(view->grabFramebuffer());
            check(view->property("gpuSourceWireVertexCount").toULongLong()>0&&
                    view->property("selectedFeatureFaceTriangleCount").toULongLong()==0,
                "Transition selection did not display only its authored wire");
            const auto selected_image=view->grabFramebuffer();unsigned azure{};
            for(int y=0;y<selected_image.height();++y)for(int x=0;x<selected_image.width();++x) {
                const auto color=selected_image.pixelColor(x,y);
                if(std::abs(color.red()-0)<24&&std::abs(color.green()-209)<24&&
                        std::abs(color.blue()-255)<24)++azure;
            }
            check(azure>4,"Transition authored wire is not visibly azure");
            check(window.execute_console_command("documents").data==document_state&&
                    view->base_mesh_revision()==mesh_revision,
                "Transition single selection changed the document or base geometry");
            check(view->container_inspection_wire().empty(),
                "A single Transition selection exposed double-click inspection");
            bool menu_seen=false,properties_seen=false;
            QTimer::singleShot(0,[&] {
                if(auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    menu_seen=menu->objectName()==QStringLiteral("partHistoryMenu");
                    properties_seen=std::ranges::any_of(menu->actions(),[](const auto* action) {
                        return action->text().contains(QObject::tr("Vlastnosti"));
                    });
                    menu->close();
                }
            });
            for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*point,QPointF(view->mapToGlobal(point->toPoint())),Qt::RightButton,
                    type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
            check(menu_seen&&properties_seen,"Transition View context menu has no Properties action");
            unsigned calculations{};
            {workspace::CalculationExecutionScope counted([&](auto run){++calculations;run();});
                QMouseEvent double_click(QEvent::MouseButtonDblClick,*point,QPointF(view->mapToGlobal(point->toPoint())),
                    Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&double_click);flush();}
            check(calculations==0,"Transition View inspection invoked geometry calculation");
            check(!view->container_inspection_wire().empty(),"Transition View double-click did not open parameter inspection");
            check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& dimension) {
                    return dimension.reference.owner_id==target.id&&
                        dimension.reference.semantic_key.starts_with("parameter:");
                }),"Transition View double-click exposed no parameter dimensions");
            check(!window.findChild<QDialog*>("sheetTransitionDialog")&&
                    window.execute_console_command("documents").data==document_state,
                "Transition View inspection opened Properties or changed the document");
            check(window.finish_parameter_dimensions(),"Transition parameter inspection did not close");flush();
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole+3)==QStringLiteral("part-container")&&
                   (*it)->data(0,Qt::UserRole).toString().toStdString()==target.id){row=*it;break;}
            check(row,"Transition Tree row disappeared after View inspection");
            for(auto* parent=row->parent();parent;parent=parent->parent())parent->setExpanded(true);
            tree->scrollToItem(row);flush();
            const auto tree_point=QPointF(tree->visualItemRect(row).center());
            QMouseEvent tree_double(QEvent::MouseButtonDblClick,tree_point,
                QPointF(tree->viewport()->mapToGlobal(tree_point.toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&tree_double);flush();
            check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& dimension) {
                    return dimension.reference.owner_id==target.id&&
                        dimension.reference.semantic_key.starts_with("parameter:");
                })&&window.execute_console_command("documents").data==document_state,
                "Transition Tree double-click did not expose dimensions without mutation");
            check(window.finish_parameter_dimensions(),"Transition Tree inspection did not close");flush();
            view->set_reference_visibility(viewer::ReferenceVisibility::AxisPoints,true);flush();
        };
        auto* rectangular=window.findChild<QAction*>("rectangularSheetTransitionAction");
        check(rectangular&&rectangular->isEnabled()&&!rectangular->icon().isNull(),"Rectangular transition action unavailable");
        rectangular->trigger();flush();
        auto* rectangular_editor=dynamic_cast<app::SheetTransitionDialog*>(dialog());
        auto* sides=rectangular_editor->findChild<QComboBox*>("transitionSides");
        check(sides&&sides->count()==2&&sides->currentData().toUInt()==3,"Rectangular transition must offer only L and U");
        sides->setCurrentIndex(0);rectangular_editor->reject();flush();
        check(save().history.size()==part.history.size(),"Rectangular creation Cancel modified history");
        rectangular->trigger();flush();rectangular_editor=dynamic_cast<app::SheetTransitionDialog*>(dialog());
        sides=rectangular_editor->findChild<QComboBox*>("transitionSides");sides->setCurrentIndex(0);
        check(!rectangular_editor->findChild<QWidget*>("originDisplayControls"),"Rectangular transition retained Origin point/text controls");
        check(!rectangular_editor->findChild<QSpinBox*>("transitionRightFacets"),"Rectangular transition exposed arc segmentation");
        rectangular_editor->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(25);
        for(unsigned i=0;i<3;++i)rectangular_editor->findChild<QDoubleSpinBox*>(QString("transitionEndRotation%1").arg(i))->setValue(std::array<double,3>{8,-10,12}[i]);
        flush();window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"rectangular-transition-properties.png"))));
        rectangular_editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("sheetTransitionDialog"),"Rectangular transition did not commit");
        const auto rectangular_saved=save();check(rectangular_saved.history.size()==part.history.size()+1,"Rectangular transition did not insert one history row");
        const auto rectangular_feature=rectangular_saved.history.back();
        check(!action->isEnabled()&&!rectangular->isEnabled(),"A second transition is offered in the same Part");
        auto* rectangular_view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(rectangular_view,"Rectangular transition View missing");
        rectangular_view->fit_all();flush();
        window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"rectangular-transition-native.png"))));
        check(document::rectangular_sheet_transition(rectangular_feature)&&std::abs(rectangular_feature.placement.x-25)<1e-8,"Rectangular transition lost placement or profile mode");
        check(std::ranges::none_of(rectangular_view->mesh().edges,[](const auto& edge){return edge.preview_terminal_dashed;}),"Rectangular material preview retained dashed envelope");
        check(rectangular_feature.sheet_transition.end_rotation==kernel::Vec3{8,-10,12},"Rectangular transition lost its three-axis rotation");
        verify_ordinary_interaction(rectangular_feature,"Rectangular transition is absent from the ordinary View picker");
        auto* rectangular_tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* rectangular_row=nullptr;
        for(QTreeWidgetItemIterator it(rectangular_tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==rectangular_feature.id){rectangular_row=*it;break;}
        check(rectangular_row,"Rectangular transition missing from Tree");window.show_tree_item_properties(rectangular_row);flush();
        sides=dialog()->findChild<QComboBox*>("transitionSides");check(sides&&sides->currentData().toUInt()==2,"Reopening lost rectangular side count");
        sides->setCurrentIndex(1);dialog()->reject();flush();
        check(save().find_container(rectangular_feature.id)->sheet_transition==rectangular_feature.sheet_transition,"Rectangular edit Cancel changed profiles");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(save().history.size()==part.history.size(),"Rectangular UI Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(save().find_container(rectangular_feature.id),"Rectangular UI Redo failed");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        check(action->isEnabled()&&rectangular->isEnabled(),"Undo did not restore transition creation");
        action->trigger();flush();dialog()->reject();flush();check(save().history.size()==part.history.size(),"Creation Cancel modified history");
        action->trigger();flush();auto* d=dialog();
        check(d->windowType()==Qt::SubWindow,"Transition properties are not an internal subwindow");
        auto* editor=dynamic_cast<app::SheetTransitionDialog*>(d);check(editor,"Wrong transition editor");
        auto* custom=editor->findChild<QCheckBox*>("transitionThicknessOverride");
        auto* thickness=editor->findChild<QDoubleSpinBox*>("transitionThickness");
        check(custom&&!custom->isChecked()&&thickness&&!thickness->isEnabled()&&thickness->value()==2.5,"New transition does not inherit document thickness");
        check(!editor->set_inline_parameter_value("thickness",9),"Inherited thickness accepted an inline override");
        custom->click();check(thickness->isEnabled()&&editor->pending.sheet_transition.thickness_override,"Custom thickness did not enable editing");
        thickness->setValue(3);custom->click();
        check(!thickness->isEnabled()&&thickness->value()==2.5&&!editor->pending.sheet_transition.thickness_override,"Disabling custom thickness did not restore document value");
        custom->click();check(thickness->isEnabled()&&thickness->value()==2.5,"Enabling custom thickness did not start with effective thickness");
        unsigned theme_calculations=0;
        {workspace::CalculationExecutionScope counted([&](auto run){++theme_calculations;run();});
            for(const auto* theme:{"light","dark","light"}) {
                app::ApplicationSettings appearance;appearance.theme=theme;app::apply_application_appearance(application,appearance);flush();
                check(editor->palette().color(QPalette::Window)==application.palette().color(QPalette::Window),"Open Properties retained the preceding theme palette");
                check(custom->palette().color(QPalette::WindowText)!=custom->palette().color(QPalette::Window),"Thickness checkbox has no theme contrast");
                for(unsigned i=0;i<3;++i) {
                    auto* field=editor->findChild<QDoubleSpinBox*>(QString("transitionEndPosition%1").arg(i));
                    check(field&&field->height()>=field->sizeHint().height(),"Transition XYZ input is vertically compressed");
                }
                window.grab().save(QString::fromStdString(document::path_to_utf8((directory/(std::string("transition-thickness-")+theme+".png")))));
            }
        }
        check(theme_calculations==0,"Live transition theme change calculated geometry");
        check(editor->width()<=std::max(editor->minimumWidth(),editor->minimumSizeHint().width())+2,"Transition did not open at minimum width");
        check(sketcher::Sketch::from_serialized(editor->pending.sheet_transition.sketches[1]).plane_reference_owner_id==editor->pending.container_origin.id,"Rectangle is not attached to main Origin");
        check(sketcher::Sketch::from_serialized(editor->pending.sheet_transition.sketches[0]).plane_reference_owner_id==editor->pending.sheet_transition.end_origin_id,"Round profile is not attached to second Origin");
        check(editor->findChild<QTableWidget*>("sweepPlacementReferences")&&editor->findChild<QTableWidget*>("sweepPlacementOrientation"),"Shared placement controls missing");
        check(editor->findChild<QPushButton*>("containerOriginSelectionButton"),"Whole-Origin selection missing");
        editor->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(12);
        for(unsigned i=0;i<2;++i){
            auto* button=editor->findChild<QPushButton*>(QString("transitionSketch%1").arg(i));check(button,"Owned SKETCH button missing");
            button->click();flush();check(!editor->isVisible(),"SKETCH did not enter Sketcher");
            auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Sketcher Finish missing");finish->trigger();flush();check(dialog()==editor,"Sketcher did not return to the same editor");
            check(std::abs(editor->pending.placement.x-12)<1e-9,"Sketcher round trip lost container placement");
        }
        editor->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(12);
        check(std::abs(editor->pending.placement.x-12)<1e-9,"Numeric placement did not update");
        editor->findChild<QDoubleSpinBox*>("sweepTranslation0")->setValue(0);
        editor->request_placement(0);flush();
        auto* placement_tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* origin_row=nullptr;
        for(QTreeWidgetItemIterator it(placement_tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==part.body_history.find(body)->origin().id){origin_row=*it;break;}
        check(origin_row,"Whole Body Origin missing from Tree");
        for(auto* parent=origin_row->parent();parent;parent=parent->parent())parent->setExpanded(true);
        placement_tree->scrollToItem(origin_row);flush();
        const QPointF origin_pos(placement_tree->visualItemRect(origin_row).center()),origin_global(placement_tree->viewport()->mapToGlobal(origin_pos.toPoint()));
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,origin_pos,origin_global,Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(placement_tree->viewport(),&event);
        }flush();
        check(editor->pending.placement.references.size()>=3,"Whole-Origin placement not resolved");
        check(editor->set_inline_parameter_value("placement:reference_offset:0",12),"Reference placement offset is not editable");flush();
        const auto expected_placement=editor->pending.placement;
        check(editor->set_inline_parameter_value("end_z",175),"Second Origin dimension is not editable");flush();
        editor->findChild<QDoubleSpinBox*>("transitionEndRotation0")->setValue(5);
        editor->findChild<QDoubleSpinBox*>("transitionThickness")->setValue(3);flush();
        check(!editor->findChild<QWidget*>("originDisplayControls"),"Transition retained Origin point/text controls");
        auto* notches=editor->findChild<QCheckBox*>("transitionEndNotches");
        auto* short_axes=editor->findChild<QCheckBox*>("transitionShortAxes");
        auto* reliefs=editor->findChild<QCheckBox*>("transitionReliefs");
        check(notches&&short_axes&&reliefs,"Manufacturing controls missing");
        check(!editor->findChild<QWidget*>("transitionReliefBends"),"Collective corner relief still exposes individual bend selection");
        check(!editor->findChild<QDoubleSpinBox*>("transitionNotchDepth")->isEnabled(),"Disabled notch retained editable depth");
        notches->setChecked(true);short_axes->setChecked(true);reliefs->setChecked(true);
        flush();
        check(editor->pending.sheet_transition.rectangle_reliefs,"Collective relief did not reach pending parameters");
        auto* facets=editor->findChild<QSpinBox*>("transitionRightFacets");const auto original_facets=facets->value();
        facets->setValue(original_facets+1);flush();
        check(editor->pending.sheet_transition.rectangle_reliefs,"Changing facet count disabled corner relief");
        facets->setValue(original_facets);flush();
        const auto manufacturing=editor->pending.sheet_transition;

        auto* mesh_view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(mesh_view,"Mesh View missing");
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==editor->pending.id&&dimension.reference.semantic_key=="parameter:end_z";}),"Relative Origin dimension missing from View");
        const auto profile=sketcher::Sketch::from_serialized(editor->pending.sheet_transition.sketches[0]);
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==profile.id;}),"Owned Sketch dimensions missing from View");
        mesh_view->set_view_direction({1,-1,1});mesh_view->fit_all();flush();
        window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-properties.png"))));
        auto* parameter_tabs=editor->findChild<QTabWidget*>("transitionParameterTabs");parameter_tabs->setCurrentIndex(1);flush();
        window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-manufacturing.png"))));
        parameter_tabs->setCurrentIndex(0);flush();
        auto* view=window.findChild<QWidget*>("modelWorkspace");check(view,"Transition View missing");
        const QPointF point(view->rect().center());QMouseEvent middle(QEvent::MouseButtonDblClick,point,point,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&middle);flush();
        check(!window.findChild<QDialog*>("sheetTransitionDialog"),"MMB double-click did not commit transition");
        const auto saved_marking=save().history.back().sheet_transition;
        check(saved_marking.end_notches&&saved_marking.short_bend_axes&&saved_marking.rectangle_reliefs&&saved_marking.rectangle_relief_depth==manufacturing.rectangle_relief_depth,"Manufacturing choices did not persist through GUI OK/save/reopen");
        const auto saved=save();check(saved.history.size()==part.history.size()+1,"Creation did not insert one feature");const auto feature=saved.history.back();
        check(feature.feature_kind==document::FeatureKind::SheetTransition&&feature.sheet_transition.end_position.z==175&&feature.placement.references.size()>=3,"UI lost transition parameters");
        verify_ordinary_interaction(feature,"Sheet transition is absent from the ordinary View picker");
        check(feature.sheet_transition.end_rotation.x==5&&feature.sheet_transition.thickness==3,"Tilted transition lost rotation or thickness");
        const auto end_normal=mesh_view->candidate_face_normal({viewer::CandidateKind::Plane,0,0,feature.id,"plane:end",{},viewer::CandidateGeometry::OriginalReference});
        check(end_normal&&std::abs(std::hypot(end_normal->x,end_normal->y,end_normal->z)-1)<1e-8,"Transition plane cannot orient a normal view");
        check(std::ranges::any_of(mesh_view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==feature.id&&edge.reference.semantic_key=="plane:end"&&edge.points.size()==5;}),"Transition end plane has no displayed border");
        for(const auto& edge:mesh_view->mesh().edges)if(edge.reference.owner_id==feature.id&&edge.reference.semantic_key=="plane:end"&&edge.points.size()==5)check(std::abs(std::hypot(edge.points[1].x-edge.points[0].x,edge.points[1].y-edge.points[0].y,edge.points[1].z-edge.points[0].z)-presentation::container_origin_plane_size)<1e-8,"Transition end plane differs from container Origin size");
        check(std::hypot(feature.placement.x-expected_placement.x,feature.placement.y-expected_placement.y,feature.placement.z-expected_placement.z)<1e-8,"OK lost reference placement");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(save().history.size()==part.history.size(),"Transition UI Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(save().history.size()==saved.history.size(),"Transition UI Redo failed");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
        check(row,"Transition absent from Tree");window.show_tree_item_properties(row);flush();
        d=dialog();
        check(d->findChild<QCheckBox*>("transitionThicknessOverride")->isChecked()&&d->findChild<QDoubleSpinBox*>("transitionThickness")->isEnabled(),"Reopening lost custom thickness");
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==feature.id&&dimension.reference.semantic_key=="parameter:end_z";}),"Reopened container has no editable distance");
        d->findChild<QCheckBox*>("transitionThicknessOverride")->click();d->reject();flush();
        check(save().find_container(feature.id)->sheet_transition==feature.sheet_transition,"Edit Cancel changed transition");
        row=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
        check(row,"Transition missing after Cancel");window.show_tree_item_properties(row);flush();
        unsigned calculations=0;
        {workspace::CalculationExecutionScope counted([&](auto run){++calculations;run();});
            dialog()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();}
        check(calculations==0,"Unchanged transition OK invoked geometry calculation");
        check(save().find_container(feature.id)->sheet_transition==feature.sheet_transition,"Unchanged OK changed transition parameters");
        window.show_parameter_dimensions(feature.id);flush();
        const auto dimension=std::ranges::find_if(mesh_view->mesh().dimensions,[&](const auto& value){return value.reference.owner_id==feature.id&&value.reference.semantic_key=="parameter:end_z";});
        check(dimension!=mesh_view->mesh().dimensions.end(),"Standalone transition dimension missing");
        viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=feature.id;candidate.semantic_key="parameter:end_z";
        candidate.geometry_index=std::distance(mesh_view->mesh().dimensions.begin(),dimension);
        window.edit_dimension_inline(candidate);flush();
        auto* inline_value=window.findChild<QLineEdit*>("inlineDimensionValueEdit");check(inline_value,"Transition inline editor did not open");
        inline_value->setText("180");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(inline_value,&enter);flush();
        check(save().find_container(feature.id)->sheet_transition.end_position.z==180,"Standalone dimension edit was not committed");
        const auto rectangle=sketcher::Sketch::from_serialized(feature.sheet_transition.sketches[1]);
        const auto width=std::ranges::find_if(mesh_view->mesh().dimensions,[&](const auto& value){return value.reference.owner_id==rectangle.id&&std::abs(value.value-200)<1e-8;});
        check(width!=mesh_view->mesh().dimensions.end(),"Overall rectangle width dimension missing");
        candidate.owner_id=rectangle.id;candidate.semantic_key=width->reference.semantic_key;candidate.geometry_index=std::distance(mesh_view->mesh().dimensions.begin(),width);
        window.edit_dimension_inline(candidate);flush();inline_value=window.findChild<QLineEdit*>("inlineDimensionValueEdit");check(inline_value,"Rectangle inline editor did not open");
        inline_value->setText("220");QKeyEvent width_enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(inline_value,&width_enter);flush();
        const auto edited_rectangle=sketcher::Sketch::from_serialized(save().find_container(feature.id)->sheet_transition.sketches[1]);
        check(std::ranges::any_of(edited_rectangle.dimensions,[](const auto& value){return std::abs(value.value-220)<1e-8;}),"Rectangle dimension edit was not committed");
        window.finish_parameter_dimensions();flush();
        mesh_view->set_view_direction({1,-1,1});mesh_view->fit_all();flush();
        window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-native.png"))));
        for(bool unfold:{true,false}) {
            auto* change=window.findChild<QAction*>(unfold?"unbendAction":"bendBackAction");check(change&&change->isEnabled(),"Sheet state action unavailable");
            change->trigger();flush();auto* state_dialog=window.findChild<QDialog*>("sheetStateDialog");check(state_dialog,"Sheet state dialog did not open");
            state_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(!window.findChild<QDialog*>("sheetStateDialog"),"Transition sheet state did not commit");
            check(save().history.back().feature_kind==(unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack),"Wrong committed sheet state");
            if(unfold)window.grab().save(QString::fromStdString(document::path_to_utf8((directory/"transition-native-unfolded.png"))));
        }
        window.close();flush();std::cout<<"Native transition UI passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Transition UI: "<<error.what()<<'\n';return 1;}
}
