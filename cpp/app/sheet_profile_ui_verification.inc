static int verify_sheet_profile_ui(QApplication& application,
        const std::filesystem::path& parent) {
    using namespace zima;
    const auto require=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    const auto flush=[&] {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        application.processEvents();
    };
    try {
        QTemporaryDir isolated(QString::fromStdString(
            (parent/"sheet-profile-ui-XXXXXX").string()));
        require(isolated.isValid(),"Cannot isolate Sheet Profile UI test");
        const auto directory=std::filesystem::path(
            isolated.path().toStdWString());
        auto part=document::PartDocument::create_default();
        document::BodyHistoryGraph graph;
        const auto body=graph.create_body("Body");
        part.set_body_history(std::move(graph));
        document::set_sheet_metal_defaults(part,{2.5,.42});
        const auto path=directory/"sheet-profile-ui.prtz";
        part.save(path,{});

        app::AssemblyWorkspaceWindow window(isolated.path());
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1400,1000);window.show();
        require(window.open_document_path(
            QString::fromStdWString(path.wstring())),
            "Sheet Profile UI fixture did not open");
        flush();
        require(activate_test_body(application,window,body),
            "Sheet Profile Body could not activate");
        auto* sheet_mode=window.findChild<QAction*>("applicationModeAction2");
        require(sheet_mode,"Sheet Metal mode is missing");
        sheet_mode->trigger();flush();
        auto* action=window.findChild<QAction*>("sheetProfileAction");
        require(action&&action->isEnabled()&&!action->icon().isNull(),
            "Sheet Profile action is unavailable or has no icon");
        action->trigger();flush();
        auto* dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("sheetProfilePropertiesDialog"));
        require(dialog&&dialog->isVisible()&&
            dialog->windowType()==Qt::SubWindow,
            "Sheet Profile did not open in the shared internal Properties window");
        auto* custom=dialog->findChild<QCheckBox*>(
            "sheetProfileThicknessOverride");
        auto* thickness=dialog->findChild<QDoubleSpinBox*>(
            "sheetProfileThickness");
        auto* width_panel=dialog->findChild<QWidget*>(
            "sheetProfileWidthPanel");
        auto* width=dialog->findChild<QDoubleSpinBox*>(
            "featureSideValue0");
        auto* width_direction=dialog->findChild<QDoubleSpinBox*>(
            "featureDirection");
        auto* width_symmetric=dialog->findChild<QCheckBox*>(
            "featureSymmetric");
        auto* first_mode=dialog->findChild<QComboBox*>(
            "featureSideMode0");
        auto* second_mode=dialog->findChild<QComboBox*>(
            "featureSideMode1");
        require(custom&&!custom->isChecked()&&thickness&&
            !thickness->isEnabled()&&std::abs(thickness->value()-2.5)<1e-9,
            "New Sheet Profile does not inherit the Part sheet thickness");
        require(width_panel&&width_panel->isVisible()&&width&&
                width->isVisible()&&width->isEnabled()&&
                std::abs(width->value()-50.)<1e-9&&width_direction&&
                width_direction->isVisible()&&width_direction->value()==1&&
                width_symmetric&&!width_symmetric->isChecked()&&
                first_mode&&first_mode->currentIndex()==1&&
                second_mode&&second_mode->currentIndex()==0,
            "Free Sheet Profile does not expose the shared one-side Feature extent");
        require(!dialog->findChild<QComboBox*>("sheetProfileMaterialSide")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileMaterialSideSwitch")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileOtherEndpoint")&&
            !dialog->findChild<QPushButton*>(
                "sheetProfileManualPlacement")&&
            dialog->findChild<QPushButton*>("sketchOpenButton"),
            "Sheet Profile still exposes manual attachment controls or lost its Sketch control");
        auto* view=dynamic_cast<viewer::MeshView*>(
            window.findChild<QOpenGLWidget*>());
        require(view,"Sheet Profile View is missing");
        require(view->extent_manipulators().size()==1&&
                view->extent_manipulators().front().end_key=="side0_length"&&
                std::abs(view->extent_manipulators().front().length-50.)<1e-9,
            "One-side free Profile has no Side 1 width manipulator");
        width->setValue(60);flush();
        require(view->extent_manipulators().size()==1&&
                view->extent_manipulators().front().end_key=="side0_length"&&
                std::abs(view->extent_manipulators().front().length-60.)<1e-9,
            "First-side free Profile did not update its purple width manipulator");
        const auto forward_direction=view->extent_manipulators().front().direction;
        view->set_view_direction({1,1,1});view->fit_all();flush();
        const auto project=[&](kernel::Vec3 point) {
            const auto center=view->ray_at(
                {view->width()*.5,view->height()*.5});
            require(center.has_value(),
                "Sheet Profile width manipulator camera ray is missing");
            const auto normal=center->second;
            const auto dot=[](kernel::Vec3 a,kernel::Vec3 b) {
                return a.x*b.x+a.y*b.y+a.z*b.z;
            };
            const auto sub=[](kernel::Vec3 a,kernel::Vec3 b) {
                return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};
            };
            const auto hit=[&](QPointF screen) {
                const auto ray=view->ray_at(screen);
                require(ray.has_value(),
                    "Sheet Profile width manipulator projection ray is missing");
                const double t=dot(sub(point,ray->first),normal)/
                    dot(ray->second,normal);
                return kernel::Vec3{ray->first.x+t*ray->second.x,
                    ray->first.y+t*ray->second.y,
                    ray->first.z+t*ray->second.z};
            };
            const auto origin=hit({0,0});
            const auto u=sub(hit({100,0}),origin);
            const auto v=sub(hit({0,100}),origin);
            const auto r=sub(point,origin);
            const double uu=dot(u,u),vv=dot(v,v),uv=dot(u,v);
            const double determinant=uu*vv-uv*uv;
            require(std::abs(determinant)>1e-12,
                "Sheet Profile width manipulator projection is degenerate");
            return QPointF(100*(dot(r,u)*vv-dot(r,v)*uv)/determinant,
                100*(dot(r,v)*uu-dot(r,u)*uv)/determinant);
        };
        const auto handle=view->extent_manipulators().front();
        const double handle_norm=std::hypot(std::hypot(handle.direction.x,
            handle.direction.y),handle.direction.z);
        require(handle_norm>1e-12,
            "Sheet Profile width manipulator has no direction");
        const kernel::Vec3 handle_direction{
            handle.direction.x/handle_norm,handle.direction.y/handle_norm,
            handle.direction.z/handle_norm};
        const auto along=[&](double length) {
            return kernel::Vec3{
                handle.origin.x+handle_direction.x*length,
                handle.origin.y+handle_direction.y*length,
                handle.origin.z+handle_direction.z*length};
        };
        const auto drag_from=project(along(60));
        const auto drag_to=project(along(80));
        const auto revision_before_drag=
            window.execute_console_command("documents").data;
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,
                QEvent::MouseButtonRelease}) {
            const auto position=type==QEvent::MouseButtonPress?
                drag_from:drag_to;
            QMouseEvent event(type,position,
                QPointF(view->mapToGlobal(position.toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,
                Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        require(std::abs(width->value()-80.)<.1&&
                view->extent_manipulators().size()==1&&
                std::abs(view->extent_manipulators().front().length-80.)<.1,
            "Dragging the purple Profile manipulator did not update width and preview");
        require(window.execute_console_command("documents").data==
                revision_before_drag,
            "Dragging the purple Profile manipulator changed the document before OK");
        const auto reverse_drag_from=project(along(80));
        const auto reverse_drag_to=project(along(-40));
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,
                QEvent::MouseButtonRelease}) {
            const auto position=type==QEvent::MouseButtonPress?
                reverse_drag_from:reverse_drag_to;
            QMouseEvent event(type,position,
                QPointF(view->mapToGlobal(position.toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,
                Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        require(std::abs(width->value()-40.)<.1&&
                width_direction->value()==-1&&
                view->extent_manipulators().size()==1&&
                std::abs(view->extent_manipulators().front().length-40.)<.1,
            "Dragging the Profile width through its origin did not switch sides");
        require(window.execute_console_command("documents").data==
                revision_before_drag,
            "Side-switching the Profile manipulator changed the document before OK");
        width_direction->setValue(-1);flush();
        require(view->extent_manipulators().size()==1&&
                forward_direction.x*view->extent_manipulators().front().direction.x+
                forward_direction.y*view->extent_manipulators().front().direction.y+
                forward_direction.z*view->extent_manipulators().front().direction.z<-.999,
            "Second-side free Profile did not reverse its purple width manipulator");
        width_direction->setValue(1);
        width_symmetric->setChecked(true);
        width->setValue(50);flush();
        require(view->extent_manipulators().size()==2&&
                std::ranges::all_of(view->extent_manipulators(),[](const auto& handle) {
                    return (handle.end_key=="side0_length"||
                            handle.end_key=="side1_length")&&
                        std::abs(handle.length-50.)<1e-9;
                }),
            "Symmetric Profile is not dimensioned as 50 mm plus 50 mm");
        const auto free_draft=dialog->pending_value().first;
        require(free_draft.points.empty()&&free_draft.segments.empty()&&
                free_draft.arcs.empty()&&free_draft.dimensions.empty()&&
                free_draft.constraints.empty(),
            "Free Profile unexpectedly pre-created Sketch geometry");
        dialog->findChild<QPushButton*>("sketchOpenButton")->click();flush();
        QEventLoop empty_view_settle;
        QTimer::singleShot(950,&empty_view_settle,&QEventLoop::quit);
        empty_view_settle.exec();flush();
        const double dpi=view->physicalDpiY()>=50&&view->physicalDpiY()<=400
            ?view->physicalDpiY():96.;
        const double physical_scale=view->height()*25.4/(2.*dpi);
        require(std::abs(view->camera_state()[4]-physical_scale)<.05,
            "Empty Profile Sketcher did not start at monitor physical scale");
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        require(dialog->isVisible(),
            "Empty Profile did not return from Sketcher");
        auto [driver,placement]=dialog->pending_value();
        static_cast<void>(placement);
        require(driver.points.empty()&&driver.segments.empty()&&
                driver.arcs.empty()&&driver.dimensions.empty()&&
                driver.constraints.empty(),
            "Entering an empty Profile Sketcher created geometry");
        static_cast<void>(driver.add_segment(0,0,20,0));
        static_cast<void>(driver.add_arc(20,5,20,0,25,5));
        static_cast<void>(driver.add_segment(25,5,25,25));
        dialog->set_pending_sketch(std::move(driver));flush();
        require(!view->transient_edges().empty(),
            "Sheet Profile Sketch return produced no cyan feature wire");
        dialog->findChild<QDialogButtonBox*>()->button(
            QDialogButtonBox::Ok)->click();flush();
        require(!window.findChild<QDialog*>("sheetProfilePropertiesDialog"),
            "Sheet Profile OK did not close Properties");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();

        auto saved=document::PartDocument::load(path);
        const auto parent_feature=std::ranges::find(saved.history,
            document::FeatureKind::SheetProfile,
            &document::HistoryContainer::feature_kind);
        require(parent_feature!=saved.history.end()&&
            !parent_feature->sheet_profile.thickness_override&&
            std::abs(parent_feature->sheet_profile.thickness-2.5)<1e-9,
            "Sheet Profile creation lost inherited thickness");
        std::vector<std::string> children;
        for(const auto& feature:saved.history)
            if(feature.sheet_profile_binding.parent_id==parent_feature->id)
                children.push_back(feature.id);
        require(children.size()==3,
            "Sheet Profile UI did not create Flat/Bend/Flat native children");
        kernel::OcctKernel kernel;
        const auto calculated=kernel.evaluate_history(saved.kernel_operations());
        require(!calculated.empty()&&
            calculated.back().calculation_errors.empty(),
            "Sheet Profile GUI OK did not calculate its generated features");
        const auto calculated_body=calculated.back().body_outputs.find(body);
        require(calculated_body!=calculated.back().body_outputs.end()&&
            !calculated_body->second->kernel_shape.empty()&&
            calculated_body->second->volume>0,
            "Sheet Profile GUI OK produced no visible solid Body");

        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        QTreeWidgetItem* profile_row{};
        std::map<std::string,QTreeWidgetItem*> child_rows;
        for(QTreeWidgetItemIterator row(tree);*row;++row) {
            const auto id=(*row)->data(0,Qt::UserRole).toString().toStdString();
            if(id==parent_feature->id)profile_row=*row;
            if(std::ranges::find(children,id)!=children.end())child_rows[id]=*row;
        }
        require(profile_row&&child_rows.size()==children.size()&&
            std::ranges::all_of(children,[&](const auto& id) {
                return child_rows.at(id)->parent()==profile_row;
            }),"Generated Sheet Profile children are not grouped below Profile");

        // In the model a free Profile is one authored item.  A click on any
        // calculated Flat/Bend selects the parent and a double-click inspects
        // the route instead of exposing the child's derived XYZ/RX/RY/RZ.
        view->fit_all();flush();
        std::optional<QPointF> child_hit;
        for(int y=4;y<view->height()&&!child_hit;y+=4)
            for(int x=4;x<view->width()&&!child_hit;x+=4) {
                const auto candidates=view->selection_candidates_at(
                    QPointF(x,y));
                if(std::ranges::any_of(candidates,[&](const auto& candidate) {
                        return candidate.kind==viewer::CandidateKind::Container&&
                            std::ranges::find(children,candidate.owner_id)!=
                                children.end();
                    }))child_hit=QPointF(x,y);
            }
        require(child_hit.has_value(),
            "Free Profile has no selectable calculated segment in View");
        const auto send_mouse=[&](QEvent::Type type,Qt::MouseButton button,
                Qt::MouseButtons buttons) {
            QMouseEvent event(type,*child_hit,
                QPointF(view->mapToGlobal(child_hit->toPoint())),button,buttons,
                Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        send_mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        send_mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
        send_mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        require(tree->currentItem()&&
                tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==
                    parent_feature->id,
            "Clicking a free Profile segment did not select the parent Profile");
        send_mouse(QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
        require(std::ranges::none_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return (dimension.reference.owner_id==parent_feature->id||
                            std::ranges::find(children,
                                dimension.reference.owner_id)!=children.end())&&
                        dimension.reference.semantic_key.starts_with(
                            "parameter:placement:");
                }),
            "Free Profile double-click exposed a generated placement angle");
        send_mouse(QEvent::MouseButtonPress,Qt::MiddleButton,Qt::MiddleButton);
        send_mouse(QEvent::MouseButtonRelease,Qt::MiddleButton,Qt::NoButton);

        profile_row=nullptr;
        QTreeWidgetItem* free_child_row{};
        for(QTreeWidgetItemIterator row(tree);*row;++row)
            if(const auto id=(*row)->data(0,Qt::UserRole).toString().toStdString();
                    id==parent_feature->id)profile_row=*row;
            else if(id==children.front())free_child_row=*row;
        require(profile_row&&free_child_row,
            "Profile row disappeared after model double-click inspection");
        tree->setCurrentItem(free_child_row);
        tree->itemDoubleClicked(free_child_row,0);flush();
        require(tree->currentItem()&&
                tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==
                    parent_feature->id&&
                std::ranges::none_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return std::ranges::find(children,
                                dimension.reference.owner_id)!=children.end()&&
                        dimension.reference.semantic_key.starts_with(
                            "parameter:placement:");
                }),
            "Tree double-click treated a free Profile child as independent");
        send_mouse(QEvent::MouseButtonPress,Qt::MiddleButton,Qt::MiddleButton);
        send_mouse(QEvent::MouseButtonRelease,Qt::MiddleButton,Qt::NoButton);
        profile_row=nullptr;
        for(QTreeWidgetItemIterator row(tree);*row;++row)
            if((*row)->data(0,Qt::UserRole).toString().toStdString()==
                    parent_feature->id) {
                profile_row=*row;break;
            }
        require(profile_row,
            "Profile row disappeared after Tree double-click inspection");

        window.show_tree_item_properties(profile_row);flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("sheetProfilePropertiesDialog"));
        require(dialog&&dialog->isVisible(),
            "Saved Sheet Profile Properties did not reopen");
        dialog->reject();flush();
        profile_row=nullptr;
        for(QTreeWidgetItemIterator row(tree);*row;++row)
            if((*row)->data(0,Qt::UserRole).toString().toStdString()==
                    parent_feature->id) {
                profile_row=*row;break;
            }
        require(profile_row,"Sheet Profile row disappeared after Cancel");

        bool exploded{};
        QTimer::singleShot(0,&window,[&] {
            auto* menu=window.findChild<QMenu*>("partHistoryMenu");
            auto* explode=menu?menu->findChild<QAction*>(
                "explodeSheetProfileAction"):nullptr;
            if(!explode||!explode->isEnabled())return;
            exploded=true;menu->setActiveAction(explode);
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(menu,&enter);
        });
        tree->customContextMenuRequested(
            tree->visualItemRect(profile_row).center());flush();
        require(exploded,"Explode Profile context action is unavailable");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto decomposed=document::PartDocument::load(path);
        require(!decomposed.find_container(parent_feature->id)&&
            std::ranges::all_of(children,[&](const auto& id) {
                const auto* child=decomposed.find_container(id);
                return child&&child->sheet_profile_binding.parent_id.empty();
            }),"Explode Profile did not retain independent native children");
        const auto decomposed_result=kernel.evaluate_history(
            decomposed.kernel_operations());
        require(!decomposed_result.empty()&&
                decomposed_result.back().calculation_errors.empty()&&
                decomposed_result.back().body_outputs.contains(body)&&
                decomposed_result.back().body_outputs.at(body)->volume>0,
            "Exploded Profile children no longer calculate a solid Body");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto restored=document::PartDocument::load(path);
        require(restored.find_container(parent_feature->id)&&
            std::ranges::all_of(children,[&](const auto& id) {
                const auto* child=restored.find_container(id);
                return child&&child->sheet_profile_binding.parent_id==
                    parent_feature->id;
            }),"Undo did not restore the complete Sheet Profile group");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        decomposed=document::PartDocument::load(path);
        require(!decomposed.find_container(parent_feature->id)&&
            std::ranges::all_of(children,[&](const auto& id) {
                const auto* child=decomposed.find_container(id);
                return child&&child->sheet_profile_binding.parent_id.empty();
            }),"Redo did not decompose the same Sheet Profile group");

        auto attached_part=document::PartDocument::create_default();
        document::BodyHistoryGraph attached_graph;
        const auto attached_body=attached_graph.create_body("Attached Profile body");
        attached_part.set_body_history(std::move(attached_graph));
        document::set_sheet_metal_defaults(attached_part,{2.5,.42});
        auto stock=document::PartDocument::create_sketch_container();
        stock.feature_kind=document::FeatureKind::Flat;
        stock.flat.thickness=2.5;stock.flat.thickness_override=true;
        auto stock_outline=sketcher::Sketch::create_default();
        stock_outline.owner_container_id=stock.id;
        stock.flat.sketch_id=stock_outline.id;
        static_cast<void>(stock_outline.add_rectangle(0,0,60,40));
        attached_part.insert_history_entry(
            document::PartHistoryKind::Feature,stock.id);
        attached_part.history.push_back(stock);
        attached_part.sketches.push_back(stock_outline);
        auto attached_results=workspace::calculate_part_with_resolved_references(
            kernel,attached_part);
        require(!attached_results.empty()&&
            attached_results.back().calculation_errors.empty(),
            "Attached Profile UI source Flat did not calculate");
        const auto& attached_edges=
            attached_results.back().mesh.original_references.edges;
        auto attached_edge=attached_edges.end();
        for(auto edge=attached_edges.begin();edge!=attached_edges.end();++edge)
            try {
                const auto refs=document::bend_sheet_references(*edge);
                if(document::bend_attachment_profile_direction(refs,
                        attached_results.back().mesh.original_references)) {
                    attached_edge=edge;break;
                }
            } catch(const std::exception&) {}
        require(attached_edge!=attached_edges.end(),
            "Attached Profile UI source has no valid boundary edge");
        const auto attached_path=directory/"attached-sheet-profile-ui.prtz";
        attached_part.save(attached_path,attached_results);
        require(window.open_document_path(
                QString::fromStdWString(attached_path.wstring())),
            "Attached Profile UI fixture did not open");
        flush();
        require(activate_test_body(application,window,attached_body),
            "Attached Profile Body could not activate");
        sheet_mode=window.findChild<QAction*>("applicationModeAction2");
        require(sheet_mode,"Sheet Metal mode disappeared");
        sheet_mode->trigger();flush();
        action=window.findChild<QAction*>("sheetProfileAction");
        require(action&&action->isEnabled(),
            "Attached Profile action is unavailable");
        action->trigger();flush();
        dialog=dynamic_cast<app::SketchPropertiesDialog*>(
            window.findChild<QDialog*>("sheetProfilePropertiesDialog"));
        require(dialog,"Attached Profile Properties did not open");
        const auto unattached_draft=dialog->pending_value().first;
        require(unattached_draft.points.empty()&&
                unattached_draft.segments.empty()&&
                unattached_draft.arcs.empty(),
            "Profile created geometry before an attachment edge was selected");
        auto* reference_table=dialog->findChild<QTableWidget*>(
            "sketchReferenceTable");
        require(reference_table,"Attached Profile lost shared reference entry");
        reference_table->cellClicked(0,1);flush();
        view->fit_all();flush();
        std::optional<QPointF> edge_hit;
        for(int y=4;y<view->height()&&!edge_hit;y+=4)
            for(int x=4;x<view->width();x+=4) {
                const auto candidates=view->selection_candidates_at(
                    QPointF(x,y));
                if(!std::ranges::any_of(candidates,[](const auto& candidate) {
                        return candidate.kind==viewer::CandidateKind::Edge;
                    }))continue;
                const QPointF position(x,y);
                QMouseEvent move(QEvent::MouseMove,position,
                    QPointF(view->mapToGlobal(position.toPoint())),Qt::NoButton,
                    Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&move);flush();
                const auto hovered=view->hovered_candidate();
                if(!hovered||hovered->kind!=viewer::CandidateKind::Edge)
                    continue;
                const auto offered_edge=std::ranges::find_if(
                    view->mesh().original_references.edges,
                    [&](const auto& edge) {
                        return edge.reference.owner_id==hovered->owner_id&&
                            edge.reference.semantic_key==hovered->semantic_key&&
                            edge.reference.instance_path==hovered->instance_path;
                    });
                if(offered_edge==view->mesh().original_references.edges.end()||
                        kernel::sheet_edge_role(*offered_edge)!=
                            kernel::SheetEdgeRole::Boundary)
                    continue;
                try {
                    const auto refs=document::bend_sheet_references(*offered_edge);
                    if(document::bend_attachment_profile_direction(refs,
                            view->mesh().original_references)) {
                        edge_hit=position;break;
                    }
                } catch(const std::exception&) {}
            }
        require(edge_hit.has_value(),
            "Attached Profile source edge is absent from the real 3D picker");
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,
                QEvent::MouseButtonRelease}) {
            const auto button=type==QEvent::MouseMove?
                Qt::NoButton:Qt::LeftButton;
            QMouseEvent event(type,*edge_hit,
                QPointF(view->mapToGlobal(edge_hit->toPoint())),button,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,
                Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        flush();
        const auto attached_draft=dialog->pending_value();
        require(!dialog->findChild<QWidget*>(
                    "sheetProfileWidthPanel")->isVisible()&&
                view->extent_manipulators().empty(),
            "Edge-attached Profile still exposes a free-width control or manipulator");
        const auto radius_id=attached_draft.first.id+":profile:radius";
        const auto length_id=attached_draft.first.id+":profile:length";
        const auto angle_id=attached_draft.first.id+":profile:angle";
        const auto draft_radius=std::ranges::find(
            attached_draft.first.dimensions,radius_id,
            &sketcher::SketchDimension::id);
        const auto draft_length=std::ranges::find(
            attached_draft.first.dimensions,length_id,
            &sketcher::SketchDimension::id);
        const auto draft_angle=std::ranges::find(
            attached_draft.first.dimensions,angle_id,
            &sketcher::SketchDimension::id);
        require(draft_radius!=attached_draft.first.dimensions.end()&&
                draft_length!=attached_draft.first.dimensions.end()&&
                draft_angle!=attached_draft.first.dimensions.end()&&
                draft_radius->kind==sketcher::DimensionKind::Radius&&
                draft_length->kind==sketcher::DimensionKind::Distance&&
                draft_angle->kind==sketcher::DimensionKind::AngleThreePoint&&
                std::abs(draft_radius->value-5.)<1e-8&&
                std::abs(draft_length->value-40.)<1e-8&&
                std::abs(draft_angle->value-90.)<1e-8,
            "Attached Profile UI did not preserve its R, angle and line length");
        dialog->findChild<QPushButton*>("sketchOpenButton")->click();flush();
        require(!dialog->isVisible(),
            "Attached Profile did not enter Sketcher");
        QEventLoop settle;
        QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
        const auto route_line=attached_draft.first.segments.front();
        const auto route_end=route_line.second_point_id;
        const auto owned_arc_count=[&] {
            return std::ranges::count_if(view->mesh().edges,
                [&](const auto& edge) {
                    return edge.reference.owner_id==attached_draft.first.id&&
                        edge.reference.semantic_key.starts_with("arc:");
                });
        };
        const auto tangent_count=[&] {
            return std::ranges::count_if(view->mesh().constraint_markers,
                [](const auto& marker){return marker.label=="T";});
        };
        const auto initial_arcs=owned_arc_count();
        const auto initial_tangencies=tangent_count();
        auto* polyline=window.findChild<QAction*>("sketchPolylineAction");
        require(polyline&&polyline->isEnabled(),
            "Attached Profile cannot start a Polyline continuation");
        polyline->trigger();flush();
        std::optional<QPointF> route_end_hit;
        for(int y=2;y<view->height()&&!route_end_hit;y+=3)
            for(int x=2;x<view->width();x+=3) {
                const auto candidates=view->selection_candidates_at({
                    static_cast<qreal>(x),static_cast<qreal>(y)});
                if(std::ranges::any_of(candidates,[&](const auto& candidate) {
                        return candidate.kind==viewer::CandidateKind::SketchPoint&&
                            candidate.owner_id==attached_draft.first.id&&
                            candidate.semantic_key=="point:"+route_end;
                    })) {
                    route_end_hit=QPointF(x,y);break;
                }
            }
        require(route_end_hit.has_value(),
            "Attached Profile final endpoint is absent from the Sketch picker");
        const auto mouse=[&](QPointF position,QEvent::Type type,
                Qt::MouseButton button) {
            QMouseEvent event(type,position,
                QPointF(view->mapToGlobal(position.toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,
                Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        const auto click=[&](QPointF position,Qt::MouseButton button) {
            mouse(position,QEvent::MouseMove,Qt::NoButton);
            mouse(position,QEvent::MouseButtonPress,button);
            mouse(position,QEvent::MouseButtonRelease,button);
        };
        click(*route_end_hit,Qt::LeftButton);
        QPointF extension_end=*route_end_hit+QPointF(90,-70);
        if(!view->rect().adjusted(10,10,-10,-10).contains(
                extension_end.toPoint()))
            extension_end=*route_end_hit-QPointF(90,-70);
        click(extension_end,Qt::RightButton);
        click(extension_end,Qt::LeftButton);
        QKeyEvent finish_polyline(QEvent::KeyPress,Qt::Key_Escape,
            Qt::NoModifier);
        QApplication::sendEvent(&window,&finish_polyline);flush();
        require(owned_arc_count()==initial_arcs+1&&
                tangent_count()==initial_tangencies+1,
            "Profile endpoint did not create a native tangent arc");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        require(owned_arc_count()==initial_arcs,
            "Tangent Profile extension Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();
        require(owned_arc_count()==initial_arcs+1,
            "Tangent Profile extension Redo failed");
        window.findChild<QAction*>("undoAction")->trigger();flush();
        require(owned_arc_count()==initial_arcs,
            "Tangent Profile extension could not restore the original route");
        const auto edit_dimension=[&](const std::string& id,double value) {
            const auto& dimensions=view->mesh().dimensions;
            const auto shown=std::ranges::find_if(dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==
                            attached_draft.first.id&&
                        dimension.reference.semantic_key=="dimension:"+id;
                });
            require(shown!=dimensions.end(),
                "Attached Profile dimension is missing in Sketcher");
            viewer::ViewerCandidate candidate;
            candidate.kind=viewer::CandidateKind::Dimension;
            candidate.owner_id=shown->reference.owner_id;
            candidate.semantic_key=shown->reference.semantic_key;
            candidate.geometry_index=std::distance(dimensions.begin(),shown);
            const auto position=view->candidate_dimension_label_position(
                candidate);
            require(position.has_value(),
                "Attached Profile dimension label is missing");
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease,
                    QEvent::MouseButtonDblClick}) {
                QMouseEvent event(type,QPointF(*position),
                    QPointF(view->mapToGlobal(*position)),Qt::LeftButton,
                    type==QEvent::MouseButtonRelease?Qt::NoButton:
                        Qt::LeftButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
            auto* editor=view->findChild<QLineEdit*>(
                "inlineDimensionValueEdit");
            require(editor,"Attached Profile mouse dimension editor did not open");
            editor->setText(QString::number(value));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(editor,&enter);flush();
            require(view->candidate_dimension_value(candidate)&&
                    std::abs(*view->candidate_dimension_value(candidate)-value)<
                        1e-8,
                "Attached Profile mouse dimension edit failed");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            window.findChild<QAction*>("redoAction")->trigger();flush();
            require(view->candidate_dimension_value(candidate)&&
                    std::abs(*view->candidate_dimension_value(candidate)-value)<
                        1e-8,
                "Attached Profile dimension Undo/Redo failed");
        };
        edit_dimension(radius_id,7.5);
        edit_dimension(angle_id,75.);
        edit_dimension(length_id,55.);
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        require(dialog->isVisible(),
            "Attached Profile did not return from Sketcher");
        const auto returned_draft=dialog->pending_value().first;
        const auto returned_radius=std::ranges::find(
            returned_draft.dimensions,radius_id,&sketcher::SketchDimension::id);
        const auto returned_angle=std::ranges::find(
            returned_draft.dimensions,angle_id,&sketcher::SketchDimension::id);
        const auto returned_length=std::ranges::find(
            returned_draft.dimensions,length_id,&sketcher::SketchDimension::id);
        require(returned_radius!=returned_draft.dimensions.end()&&
                returned_angle!=returned_draft.dimensions.end()&&
                returned_length!=returned_draft.dimensions.end()&&
                std::abs(returned_radius->value-7.5)<1e-8&&
                std::abs(returned_angle->value-75.)<1e-8&&
                std::abs(returned_length->value-55.)<1e-8,
            "Returning from attached Profile Sketcher shifted R, angle or length");
        dialog->findChild<QDialogButtonBox*>()->button(
            QDialogButtonBox::Ok)->click();flush();
        require(!window.findChild<QDialog*>("sheetProfilePropertiesDialog"),
            "Attached Profile OK did not close Properties");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto attached_saved=document::PartDocument::load(attached_path);
        const auto attached_parent=std::ranges::find(attached_saved.history,
            document::FeatureKind::SheetProfile,
            &document::HistoryContainer::feature_kind);
        require(attached_parent!=attached_saved.history.end(),
            "Attached Profile GUI did not persist its parent");
        const auto attached_route=std::ranges::find(attached_saved.sketches,
            attached_parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
        const auto* attached_master=attached_saved.find_container(
            attached_parent->sheet_profile.master_feature_id);
        require(attached_route!=attached_saved.sketches.end()&&
                attached_master&&
                attached_master->feature_kind==document::FeatureKind::Bend&&
                attached_master->bend.sheet_attachment&&
                std::abs(attached_master->bend.radius-5.)<1e-8&&
                std::abs(attached_master->bend.angle_degrees-75.)<1e-8,
            "Attached Profile GUI did not persist its real attached Bend");
        const auto saved_radius=std::ranges::find(attached_route->dimensions,
            radius_id,&sketcher::SketchDimension::id);
        const auto saved_length=std::ranges::find(attached_route->dimensions,
            length_id,&sketcher::SketchDimension::id);
        const auto saved_angle=std::ranges::find(attached_route->dimensions,
            angle_id,&sketcher::SketchDimension::id);
        require(saved_radius!=attached_route->dimensions.end()&&
                saved_length!=attached_route->dimensions.end()&&
                saved_angle!=attached_route->dimensions.end()&&
                std::abs(saved_radius->value-7.5)<1e-8&&
                std::abs(saved_length->value-55.)<1e-8&&
                std::abs(saved_angle->value-75.)<1e-8,
            "Attached Profile GUI dimension edits did not persist");

        // The first real edge-attached Bend is the only generated child that
        // remains directly inspectable.  It exposes R/angle, never its
        // derived placement Euler angles.
        view->fit_all();flush();
        std::optional<QPointF> master_hit;
        for(int y=4;y<view->height()&&!master_hit;y+=4)
            for(int x=4;x<view->width()&&!master_hit;x+=4) {
                const auto candidates=view->selection_candidates_at(
                    QPointF(x,y));
                if(std::ranges::any_of(candidates,[&](const auto& candidate) {
                        return candidate.kind==viewer::CandidateKind::Container&&
                            candidate.owner_id==attached_master->id;
                    }))master_hit=QPointF(x,y);
            }
        require(master_hit.has_value(),
            "Attached Profile first Bend is not selectable in View");
        const auto send_attached_mouse=[&](QEvent::Type type,
                Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,*master_hit,
                QPointF(view->mapToGlobal(master_hit->toPoint())),button,
                buttons,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        };
        send_attached_mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        send_attached_mouse(QEvent::MouseButtonPress,
            Qt::LeftButton,Qt::LeftButton);
        send_attached_mouse(QEvent::MouseButtonRelease,
            Qt::LeftButton,Qt::NoButton);
        require(tree->currentItem()&&
                tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==
                    attached_master->id,
            "Attached Profile first Bend did not retain its edit exception");
        send_attached_mouse(QEvent::MouseButtonDblClick,
            Qt::LeftButton,Qt::LeftButton);
        const auto bend_path=sketcher::Sketch::from_serialized(
            attached_master->bend.auxiliary_sketches[0]);
        require(std::ranges::any_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==bend_path.id&&
                        dimension.reference.semantic_key.starts_with(
                            "dimension:");
                })&&
                std::ranges::none_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==attached_master->id&&
                        dimension.reference.semantic_key.starts_with(
                            "parameter:placement:");
                }),
            "Attached Profile first Bend did not show clean R/angle inspection");
        send_attached_mouse(QEvent::MouseButtonPress,
            Qt::MiddleButton,Qt::MiddleButton);
        send_attached_mouse(QEvent::MouseButtonRelease,
            Qt::MiddleButton,Qt::NoButton);
        QTreeWidgetItem* attached_master_row{};
        for(QTreeWidgetItemIterator row(tree);*row;++row)
            if((*row)->data(0,Qt::UserRole).toString().toStdString()==
                    attached_master->id) {
                attached_master_row=*row;break;
            }
        require(attached_master_row,
            "Attached Profile first Bend is missing from Tree");
        tree->setCurrentItem(attached_master_row);
        tree->itemDoubleClicked(attached_master_row,0);flush();
        require(tree->currentItem()&&
                tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==
                    attached_master->id&&
                std::ranges::any_of(view->mesh().dimensions,
                [&](const auto& dimension) {
                    return dimension.reference.owner_id==bend_path.id&&
                        dimension.reference.semantic_key.starts_with(
                            "dimension:");
                }),
            "Tree double-click lost the attached first-Bend exception");

        const auto edit_attached_bend_angle=[&](double angle) {
            QTreeWidgetItem* master_row{};
            for(QTreeWidgetItemIterator row(tree);*row;++row)
                if((*row)->data(0,Qt::UserRole).toString().toStdString()==
                        attached_master->id) {
                    master_row=*row;break;
                }
            require(master_row,
                "Attached Profile first Bend row disappeared before Properties edit");
            window.show_tree_item_properties(master_row);flush();
            auto* bend=window.findChild<QDialog*>("bendPropertiesDialog");
            auto* angle_field=bend?
                bend->findChild<QDoubleSpinBox*>("bendAngle"):nullptr;
            require(bend&&angle_field,
                "Attached Profile first Bend did not open its native Properties");
            angle_field->setValue(angle);flush();
            bend->findChild<QDialogButtonBox*>()->button(
                QDialogButtonBox::Ok)->click();flush();
            require(!window.findChild<QDialog*>("bendPropertiesDialog"),
                "Attached Profile first Bend Properties did not close on OK");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto edited=document::PartDocument::load(attached_path);
            const auto parent=std::ranges::find(edited.history,
                document::FeatureKind::SheetProfile,
                &document::HistoryContainer::feature_kind);
            require(parent!=edited.history.end(),
                "Attached Profile parent disappeared after Bend Properties edit");
            const auto driver=std::ranges::find(edited.sketches,
                parent->sheet_profile.sketch_id,&sketcher::Sketch::id);
            require(driver!=edited.sketches.end(),
                "Attached Profile route disappeared after Bend Properties edit");
            const auto angle_dimension=std::ranges::find(driver->dimensions,
                angle_id,&sketcher::SketchDimension::id);
            const auto length_dimension=std::ranges::find(driver->dimensions,
                length_id,&sketcher::SketchDimension::id);
            const auto route=document::sheet_profile_route(*driver,
                parent->sheet_profile.start_point_id);
            require(angle_dimension!=driver->dimensions.end()&&
                    length_dimension!=driver->dimensions.end()&&
                    std::abs(angle_dimension->value-angle)<1e-8&&
                    std::abs(length_dimension->value-55)<1e-8&&
                    std::abs(std::abs(route.spans.front().turn_degrees)-angle)<1e-8&&
                    std::abs(route.spans.back().length-55)<1e-8,
                "Attached Profile Bend Properties accumulated or restored an old route value");
            const auto* master=edited.find_container(
                parent->sheet_profile.master_feature_id);
            const auto flat=std::ranges::find_if(edited.history,
                [&](const auto& feature) {
                    return feature.sheet_profile_binding.parent_id==parent->id&&
                        feature.sheet_profile_binding.curve_id==
                            route.spans.back().curve_id;
                });
            require(master&&flat!=edited.history.end()&&
                    flat->feature_kind==document::FeatureKind::Flat&&
                    flat->flat.direction==document::ExtrusionDirection::Reverse,
                "Attached Profile Bend Properties flipped its following Flat");
            const auto bend_end=sketcher::Sketch::from_serialized(
                master->bend.auxiliary_sketches[1]);
            const auto flat_sketch=std::ranges::find(edited.sketches,
                flat->flat.sketch_id,&sketcher::Sketch::id);
            const auto distance=[](kernel::Vec3 a,kernel::Vec3 b) {
                return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
            };
            const auto dot=[](kernel::Vec3 a,kernel::Vec3 b) {
                return a.x*b.x+a.y*b.y+a.z*b.z;
            };
            require(flat_sketch!=edited.sketches.end()&&
                    distance(flat_sketch->resolved_origin,
                        bend_end.resolved_origin)<1e-8&&
                    dot(flat_sketch->resolved_normal,
                        bend_end.resolved_y_axis)<-1+1e-8,
                "Attached Profile following Flat detached from its edited Bend");
        };
        edit_attached_bend_angle(60);
        edit_attached_bend_angle(105);
        edit_attached_bend_angle(60);
        std::cout<<"Sheet Profile dialog, attachment, dimensions, Tree, Explode and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
