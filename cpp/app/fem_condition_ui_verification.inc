        window.activate_body(fem);flush();
        const auto condition_dialog=[&]{return dynamic_cast<FemConditionDialog*>(window.properties_dialog_);};
        const auto condition_row=[&](const std::string& id)->QTreeWidgetItem*{
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="fem-condition"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;return nullptr;
        };
        const auto condition_count=kernel::body_calculation_count.load();
        const auto condition_revision=session.revision();
        const auto force_action=[&]{return window.findChild<QAction*>("femForceAction");};
        const auto check_plane_preview=[&](FemConditionDialog* dialog,bool pixels=false) {
            const auto value=window.fem_condition_preview_.value();
            const auto sketch=document::fem_condition_sketch(value);
            const auto owner=value.id+":work-plane:entity";
            const auto& preview=view->transient_context_mesh();
            const auto wire=std::ranges::find_if(preview.edges,[&](const auto& e){return e.reference.owner_id==owner;});
            check(wire!=preview.edges.end()&&wire->reference.semantic_key=="border"&&wire->points.size()==5,
                "FEM work-plane preview is missing its shared native Plane wire");
            kernel::Vec3 center{};
            for(unsigned i=0;i<4;++i){const auto p=wire->points[i];center.x+=p.x/4;center.y+=p.y/4;center.z+=p.z/4;
                const auto o=sketch.resolved_origin,n=sketch.normal();
                check(std::abs((p.x-o.x)*n.x+(p.y-o.y)*n.y+(p.z-o.z)*n.z)<1e-7,
                    "FEM work-plane wire left the resolved Sketch plane");}
            check(std::hypot(center.x-sketch.resolved_origin.x,center.y-sketch.resolved_origin.y,center.z-sketch.resolved_origin.z)<1e-7,
                "FEM work-plane wire lost its offset or placement");
            check(preview.triangles.empty()&&std::ranges::none_of(preview.original_references.triangle_references,
                [&](const auto& ref){return ref.owner_id==owner;}),"FEM preview created a filled or selectable datum");
            if(pixels){
                const auto cyan=[&] {
                    const auto image=view->grabFramebuffer();check(!image.isNull(),"FEM plane framebuffer missing");
                    unsigned count=0;
                    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);
                        if(c.red()<35&&c.green()>170&&c.blue()>225)++count;}
                    return count;
                };
                const auto highlighted=cyan();view->set_feature_preview_owners({});flush();const auto passive=cyan();
                dialog->changed();flush();
                check(highlighted>passive+20,"Selected FEM plane is not azure in the real View");
            }
        };
        const auto settle_sketch_view=[&]{QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();flush();};
        check(force_action()&&force_action()->isEnabled(),"FEM Force shortcut is missing");
        const auto placement_origin=workspace::fem_source_frame_owner(session.document().body_history,fem)+":origin";
        for(const auto& order:std::array{std::array{"xz","xy","yz"},std::array{"xy","yz","xz"},std::array{"yz","xz","xy"}}) {
            force_action()->trigger();flush();auto* entry=condition_dialog();
            check(entry&&window.pending_primitive_reference_index_==0&&window.primitive_reference_auto_advance_,"FEM did not automatically start placement entry");
            for(std::size_t index=0;index<3;++index) {
                if(index==1) {
                    auto* capture=entry->findChild<QAction*>("valueLock:placement:reference_slot:1");
                    check(capture,"FEM empty placement row has no distance lock");
                    if(!capture->isChecked())capture->trigger();flush();
                }
                QTreeWidgetItem* plane{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if(((*it)->data(0,Qt::UserRole+5).toString()==QString("origin:plane:")+order[index]||
                        (*it)->data(0,Qt::UserRole+5).toString()==QString("plane:")+order[index])&&
                       (*it)->parent()&&(*it)->parent()->data(0,Qt::UserRole).toString().toStdString()==placement_origin)plane=*it;
                check(plane,"FEM placement test has no source Body Origin plane");
                for(auto* parent=plane->parent();parent;parent=parent->parent())parent->setExpanded(true);
                tree->scrollToItem(plane);flush();const auto at=QPointF(tree->visualItemRect(plane).center());
                for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,at,QPointF(tree->viewport()->mapToGlobal(at.toPoint())),Qt::LeftButton,
                        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&event);flush();
                }
                if(entry->first_empty_position_index()!=index+1)throw std::runtime_error("FEM automatic reference sequence lost a pick: first="+std::string(order[0])+" index="+std::to_string(index)+" next="+std::to_string(entry->first_empty_position_index())+" refs="+std::to_string(entry->pending_value().placement.references.size())+" state="+window.state_->text().toStdString());
                const auto p=entry->pending_value().placement;
                check(p.references.front().orientation_role=="top","FEM first support did not define local XY");
                if(index==0) {
                    const auto normal=document::construction_direction_from_local_axis("z",{p.rotation_x,p.rotation_y,p.rotation_z});
                    const double aligned=std::string_view(order[0])=="xy"?normal.z:std::string_view(order[0])=="xz"?normal.y:normal.x;
                    check(std::abs(std::abs(aligned)-1)<1e-7,"FEM XY plane does not follow its first support");
                }
                if(index<2)check(window.pending_primitive_reference_index_==index+1,"FEM reference input did not advance automatically");
            }
            check(!window.pending_primitive_reference_index_,"Fully placed FEM kept reference entry active");
            entry->findChild<QComboBox*>("femConditionRegion")->setCurrentIndex(1);flush();
            check_plane_preview(entry);
            auto* plane_entry=entry->findChild<QTableWidget*>("femProfilePlaneView");
            check(plane_entry,"FEM has no shared View plane entry");
            QMetaObject::invokeMethod(plane_entry,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,1));flush();
            check(window.pending_primitive_reference_index_==0,"FEM View plane entry did not arm replacement");
            entry->findChild<QPushButton*>("femEditSketch")->click();flush();
            check(window.active_sketch()&&window.active_sketch()->plane_auto&&
                window.active_sketch()->plane==sketcher::SketchPlane::XY&&!window.pending_primitive_reference_index_,
                "Automatic FEM Sketch did not use local XY or retained placement entry");
            const auto normal=window.active_sketch()->normal();
            const double aligned=std::string_view(order[0])=="xy"?normal.z:std::string_view(order[0])=="xz"?normal.y:normal.x;
            check(std::abs(std::abs(aligned)-1)<1e-7,"Automatic FEM Sketch frame does not follow its first support");
            window.cancel_sketch_action_->trigger();flush();
            check(condition_dialog()==entry,"FEM Sketch Cancel did not restore its properties");
            entry->reject();flush();check(session.revision()==condition_revision,"Automatic FEM placement Cancel changed history");
        }
        // Finish the last Sketch camera animation before testing fixed-pixel picks.
        settle_sketch_view();
        force_action()->trigger();flush();auto* condition=condition_dialog();
        check(condition&&condition->pending_value().kind==document::FemConditionKind::Force,"Force shortcut opened another condition type");
        check(!condition->findChild<QTableWidget*>("femConditionGeometry")->isVisible()&&
            !condition->findChild<QPushButton*>("femEditSketch")->isVisible()&&
            !condition->findChild<QCheckBox*>("femProjectSketch")->isVisible(),"Point exposes region geometry controls");
        check(condition->parentWidget()==&window&&(condition->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow&&!condition->buttons()->button(QDialogButtonBox::Apply),"Condition properties bypass shared presentation");
        mouse(QEvent::MouseButtonPress,Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,Qt::NoButton);
        check(condition_dialog()==condition&&!window.pending_primitive_reference_index_&&!window.primitive_reference_auto_advance_,"Short middle click did not stop automatic FEM placement");
        check(condition->set_inline_parameter_value("placement:z",7),"Condition numeric placement is unavailable");flush();
        condition->findChild<QDoubleSpinBox*>("femForceValue")->setValue(5000);flush();
        const auto preview_id=condition->pending_value().id;
        check(window.fem_condition_preview_&&window.fem_condition_preview_->placement.z==7&&window.fem_condition_preview_->force_N==5000,"Condition preview lost numerical edits");
        condition->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(session.revision()==condition_revision&&!condition_row(preview_id)&&!window.fem_condition_preview_,"Condition Cancel leaked draft or history");
        force_action()->trigger();flush();condition=condition_dialog();
        auto* default_origin=condition->findChild<QPushButton*>("containerDefaultOriginButton");
        check(default_origin&&default_origin->isEnabled(),"Condition has no shared whole Origin action");
        default_origin->click();flush();
        check(document::placement_references_use_whole_origin(condition->pending_value().placement.references),"Condition did not adopt whole Origin");
        condition->findChild<QDoubleSpinBox*>("femForceValue")->setValue(5000);flush();
        const auto force_id=condition->pending_value().id;
        mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);
        if(condition_dialog())throw std::runtime_error("Condition OK failed: "+condition_dialog()->findChild<QLabel*>("femConditionStatus")->text().toStdString());
        const auto persisted_force=workspace::find_fem_condition(session.document(),fem,force_id);
        check(persisted_force&&persisted_force->force_N==5000&&document::placement_references_use_whole_origin(persisted_force->placement.references),"Condition placement/value did not persist");
        check(condition_row(force_id)&&std::ranges::any_of(view->mesh().constraint_markers,[&](const auto& mark){return mark.reference.owner_id==force_id&&mark.label.find("5000 N")!=std::string::npos;}),"Condition Tree row or force mark is missing");
        const auto saved_revision=session.revision();
        tree->setCurrentItem(condition_row(force_id));flush();
        check(!condition_dialog()&&std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==force_id;}),"Single-click condition selection opened parameters");
        view->fit_all();flush();view->clear_selection();flush();
        std::optional<QPointF> symbol_hit;
        for(int y=6;y<view->height()&&!symbol_hit;y+=6)for(int x=6;x<view->width()&&!symbol_hit;x+=6)
            for(const auto& candidate:view->selection_candidates_at(QPointF(x,y)))if(candidate.owner_id==force_id){symbol_hit=QPointF(x,y);break;}
        check(symbol_hit.has_value(),"FEM force symbol cannot be picked in the View");
        const auto symbol_mouse=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,*symbol_hit,QPointF(view->mapToGlobal(symbol_hit->toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        symbol_mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        check(view->offered_candidate()&&view->offered_candidate()->owner_id==force_id,"FEM symbol hover lost its owner");
        symbol_mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);symbol_mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        if(tree->currentItem()!=condition_row(force_id)||session.revision()!=saved_revision||condition_dialog())throw std::runtime_error("FEM View selection failed: tree="+(tree->currentItem()?tree->currentItem()->data(0,Qt::UserRole).toString().toStdString():"none")+" expected="+force_id+" revision="+std::to_string(session.revision())+" expected_revision="+std::to_string(saved_revision)+" dialog="+std::to_string(condition_dialog()!=nullptr)+" state="+window.state_->text().toStdString());
        symbol_mouse(QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
        check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==force_id;})&&!condition_dialog(),"FEM View double-click did not inspect parameters");
        mouse(QEvent::MouseButtonPress,Qt::MiddleButton);mouse(QEvent::MouseButtonRelease,Qt::NoButton);
        check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==force_id;})&&session.revision()==saved_revision,"FEM inspection failed shared middle-click dismissal");
        QMetaObject::invokeMethod(tree,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,condition_row(force_id)),Q_ARG(int,0));flush();
        check(!condition_dialog()&&std::ranges::any_of(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==force_id;})&&session.revision()==saved_revision,"Condition double-click failed readonly parameter inspection");
        window.show_tree_item_properties(condition_row(force_id));flush();condition=condition_dialog();check(condition,"Condition Properties did not reopen");
        condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!condition_dialog()&&session.revision()==saved_revision,"Unchanged condition OK added a transaction");
        window.show_tree_item_properties(condition_row(force_id));flush();condition=condition_dialog();
        auto* kind=condition->findChild<QComboBox*>("femConditionKind");
        kind->setCurrentIndex(kind->findData(int(document::FemConditionKind::Pressure)));flush();
        check(condition->pending_value().region==document::FemRegionKind::Surface,"Pressure did not select a Surface region");
        condition->findChild<QDoubleSpinBox*>("femPressureValue")->setValue(2.5);
        kind->setCurrentIndex(kind->findData(int(document::FemConditionKind::Force)));flush();
        check(condition->pending_value().force_N==5000&&condition->pending_value().pressure_MPa==2.5,"Changing condition type lost independent values");
        condition->reject();flush();
        for(auto region:{document::FemRegionKind::Curve,document::FemRegionKind::Surface}) {
            force_action()->trigger();flush();condition=condition_dialog();
            auto* region_field=condition->findChild<QComboBox*>("femConditionRegion");region_field->setCurrentIndex(region_field->findData(int(region)));flush();
            auto* targets=condition->findChild<QTableWidget*>("femConditionGeometry");
            auto* project=condition->findChild<QCheckBox*>("femProjectSketch");
            auto* sketch_button=condition->findChild<QPushButton*>("femEditSketch");
            auto* offset=condition->findChild<QDoubleSpinBox*>("femSketchOffset");
            check(!targets->isVisible()&&project->isVisible()&&!project->isChecked()&&
                sketch_button->isVisible()&&offset->isVisible(),"Planar region visibility is incorrect");
            const auto unchanged_revision_for_sketch=session.revision();
            offset->setValue(2.5);sketch_button->click();flush();
            check(window.sweep_profile_sketch_draft_&&!condition->isVisible(),"Planar FEM Sketch editor did not open");
            check(!window.pending_primitive_reference_index_,"FEM Sketch retained the parent placement picker");
            check(window.mutate_active_sketch([&](auto& sketch){
                if(region==document::FemRegionKind::Curve)static_cast<void>(sketch.add_segment(-2,0,2,0));
                else static_cast<void>(sketch.add_rectangle(-2,-1,2,1));
            }),"Planar FEM Sketch could not be authored");
            window.preserve_view_on_refresh_=true;window.refresh_scene();window.align_active_sketch_view();flush();
            settle_sketch_view();
            const auto drag_point=window.active_sketch()->points.back().id;
            const auto sketch_mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,held,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            };
            for(int repeat=0;repeat<3;++repeat) {
                const auto local=[&](QPointF pixel) {
                    const auto ray=view->ray_at(pixel);check(ray.has_value(),"FEM Sketch cursor ray is missing");
                    const auto point=window.active_sketch()->intersect_ray(ray->first,ray->second);
                    check(point.has_value(),"FEM Sketch plane is missing");return *point;
                };
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
                const auto before=*window.active_sketch()->find_point(drag_point);
                const auto from=screen(before.x,before.y),to=screen(before.x+.3,before.y+.2);
                view->clear_selection();flush();
                sketch_mouse(from,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                check(view->offered_candidate()&&view->offered_candidate()->semantic_key=="point:"+drag_point,"FEM Sketch point is not offered for dragging");
                sketch_mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                sketch_mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
                sketch_mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                const auto after=*window.active_sketch()->find_point(drag_point);
                check(std::hypot(after.x-before.x,after.y-before.y)>1e-6,"FEM Sketch mouse drag did not move a free point");
            }
            std::string dimension_id;
            check(window.mutate_active_sketch([&](auto& sketch){
                auto dimension=sketch.create_segment_dimension(sketch.segments.front().id);
                dimension_id=dimension.id;sketch.apply_dimension(dimension);
            }),"FEM Sketch dimension could not be created");
            window.refresh_scene();flush();
            for(double length:{3.,4.,3.5}) {
                const auto& dimensions=view->mesh().dimensions;
                const auto found=std::ranges::find_if(dimensions,[&](const auto& d){return d.reference.owner_id==window.active_sketch_id_&&d.reference.semantic_key=="dimension:"+dimension_id;});
                check(found!=dimensions.end(),"FEM Sketch dimension is absent from the View");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
                candidate.owner_id=window.active_sketch_id_;candidate.semantic_key=found->reference.semantic_key;candidate.geometry_index=found-dimensions.begin();
                const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"FEM Sketch dimension label is not clickable");
                sketch_mouse(*label,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                sketch_mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
                sketch_mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                auto* edit=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(edit,"FEM Sketch dimension value editor did not open");
                edit->setText(QString::number(length));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);flush();
                const auto& sketch=*window.active_sketch();const auto& segment=sketch.segments.front();
                const auto first=sketch.find_point(segment.first_point_id),second=sketch.find_point(segment.second_point_id);
                check(std::abs(std::hypot(first->x-second->x,first->y-second->y)-length)<1e-6,"FEM Sketch GUI dimension edit violated its length equation");
                check(session.revision()==unchanged_revision_for_sketch,"Embedded FEM Sketch editing changed the document before OK");
            }
            window.finish_sketch_action_->trigger();flush();
            const auto authored=condition->pending_value().projected_sketch->serialized;
            for(int repeat=0;repeat<3;++repeat) {
                project->click();flush();check(targets->isVisible()&&condition->pending_value().projected_sketch->project_to_body,"Projection did not expose target faces");
                condition->arm(0);flush();
                project->click();flush();check(!targets->isVisible()&&!condition->active_input()&&
                    !condition->pending_value().projected_sketch->project_to_body&&
                    condition->pending_value().projected_sketch->serialized==authored,"Projection toggle lost Sketch or retained hidden entry");
            }
            // An invalid region must report beside OK, even when the form is
            // scrolled. The failed attempt must leave the native document alone.
            offset->setValue(100.);flush();
            condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            auto* error=condition->findChild<QLabel*>("propertiesSubmitError");
            check(condition_dialog()==condition&&error&&error->isVisible()&&!error->text().isEmpty()&&
                session.revision()==unchanged_revision_for_sketch,
                "Failed Force OK hid its error or changed the document");
            const auto failed_id=condition->pending_value().id;
            if(region==document::FemRegionKind::Surface) {
                mouse(QEvent::MouseButtonDblClick,Qt::MiddleButton);
                mouse(QEvent::MouseButtonRelease,Qt::NoButton);
            }else {condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();}
            const auto* failed=workspace::find_fem_condition(session.document(),fem,failed_id);
            check(!condition_dialog()&&failed&&!failed->calculation_error.empty()&&
                session.revision()==unchanged_revision_for_sketch+1,
                "Second Force OK did not retain one unresolved definition");
            check(condition_row(failed_id)&&condition_row(failed_id)->foreground(0).color()==QColor("#FF5E52")&&
                !condition_row(failed_id)->toolTip(0).isEmpty(),"Unresolved Force has no red Tree error");
            window.generate_fem_mesh();flush();
            check(window.state_->text()==QCoreApplication::translate("AssemblyWorkspaceWindow",
                "Repair unresolved FEM conditions before calculation."),"Unresolved Force did not block meshing");
            window.solve_fem_statics();flush();
            check(window.state_->text()==QCoreApplication::translate("AssemblyWorkspaceWindow",
                "Repair unresolved FEM conditions before calculation."),"Unresolved Force did not block solving");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(!workspace::find_fem_condition(session.document(),fem,failed_id),"Unresolved Force Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            window.show_tree_item_properties(condition_row(failed_id));flush();condition=condition_dialog();
            offset=condition->findChild<QDoubleSpinBox*>("femSketchOffset");
            offset->setValue(2.5);flush();
            const auto value=condition->pending_value();
            condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            const auto* stored=workspace::find_fem_condition(session.document(),fem,value.id);
            check(!condition_dialog()&&stored&&stored->region==region&&stored->projected_sketch==value.projected_sketch,"Planar FEM region did not persist");
            check(stored->calculation_error.empty(),"Repaired Force retained its error");
            const auto unchanged_revision=session.revision();
            window.show_tree_item_properties(condition_row(value.id));flush();condition=condition_dialog();
            condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!condition_dialog()&&session.revision()==unchanged_revision,"Unchanged planar FEM OK changed the document");
            for(const bool back:{true,false,true,false}) {
                window.show_tree_item_properties(condition_row(value.id));flush();condition=condition_dialog();
                condition->findChild<QPushButton*>("containerOrientationFlipButton")->click();flush();
                auto* plane_offset=condition->findChild<QDoubleSpinBox*>("femSketchOffset");
                check(plane_offset,"FEM orientation test lost its plane offset");
                plane_offset->setValue(back?-2.5:2.5);flush();
                condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                const auto* flipped=workspace::find_fem_condition(session.document(),fem,value.id);
                check(!condition_dialog()&&flipped&&flipped->placement.orientation_back==back,
                    "Force FRONT/BACK OK did not commit its orientation");
                const auto sketch=document::fem_condition_sketch(*flipped);
                check(std::abs(sketch.resolved_origin.z-2.5)<1e-7&&
                    std::abs(sketch.normal().z-(back?-1.:1.))<1e-7,
                    "Force FRONT/BACK changed the intended plane or lost its normal");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                window.findChild<QAction*>("redoAction")->trigger();flush();
                check(workspace::find_fem_condition(session.document(),fem,value.id)->placement.orientation_back==back,
                    "Force orientation did not survive Undo/Redo");
            }
            window.show_tree_item_properties(condition_row(value.id));flush();condition=condition_dialog();
            condition->findChild<QComboBox*>("femConditionRegion")->setCurrentIndex(3);flush();
            check(condition->findChild<QTableWidget*>("femConditionGeometry")->isVisible()&&
                !condition->findChild<QPushButton*>("femEditSketch")->isVisible()&&
                !condition->pending_value().projected_sketch,"Body surface retained Sketch controls");
            condition->reject();flush();
            check(workspace::find_fem_condition(session.document(),fem,value.id)->projected_sketch==value.projected_sketch,"FEM mode change Cancel changed authored geometry");
        }
        // Current-edge projection uses an ordinary calculated Body source.
        // Boolean source selection and face/planar regions are covered above.
        auto edge_source=*session.document().body_history.find(fem);edge_source.fem->source_id=a;
        static_cast<void>(workspace::commit_fem(window.workspace_,workspace::prepare_fem_edit(session.document(),fem),edge_source));
        window.refresh_scene();flush();
        force_action()->trigger();flush();condition=condition_dialog();
        condition->findChild<QComboBox*>("femConditionRegion")->setCurrentIndex(1);
        condition->findChild<QDoubleSpinBox*>("femSketchOffset")->setValue(2.5);
        condition->findChild<QPushButton*>("femEditSketch")->click();flush();
        const auto external_revision=session.revision();
        settle_sketch_view();
        check(window.sketch_external_profile_action_&&window.sketch_external_profile_action_->isEnabled(),"FEM Sketch cannot project source geometry");
        window.sketch_external_profile_action_->trigger();view->fit_all();flush();
        std::optional<QPointF> external_hit;
        std::optional<viewer::ViewerCandidate> external_candidate;
        for(int y=10;y<view->height()&&!external_hit;y+=5)for(int x=10;x<view->width()&&!external_hit;x+=5)
            for(const auto& candidate:view->selection_candidates_at(QPointF(x,y)))if(candidate.kind==viewer::CandidateKind::Edge&&candidate.owner_id==first.id) {
                const auto edge=std::ranges::find_if(view->mesh().edges,[&](const auto& e){return e.reference.owner_id==candidate.owner_id&&e.reference.semantic_key==candidate.semantic_key;});
                if(edge!=view->mesh().edges.end()&&edge->points.size()>1&&std::hypot(edge->points.back().x-edge->points.front().x,edge->points.back().y-edge->points.front().y)>2) {
                    external_hit=QPointF(x,y);external_candidate=candidate;break;
                }
            }
        check(external_hit.has_value(),"FEM Sketch projection offers no source edge");
        const auto external_mouse=[&](QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held) {
            QMouseEvent e(type,*external_hit,QPointF(view->mapToGlobal(external_hit->toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
        };
        external_mouse(QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        for(std::size_t i=0;i<view->selection_candidates_at(*external_hit).size();++i) {
            const auto offered=view->offered_candidate();
            if(offered&&offered->owner_id==external_candidate->owner_id&&offered->semantic_key==external_candidate->semantic_key)break;
            external_mouse(QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);external_mouse(QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton);
        }
        external_mouse(QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);external_mouse(QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        if(window.active_sketch()->external_references.size()!=4||session.revision()!=external_revision)
            throw std::runtime_error("FEM Sketch projection failed or changed the source: references="+std::to_string(window.active_sketch()->external_references.size())+
                " revision="+std::to_string(session.revision())+" expected="+std::to_string(external_revision)+" "+window.state_->text().toStdString());
        check(std::ranges::all_of(window.active_sketch()->external_references,[](const auto& reference){return !reference.broken&&reference.body_edge;}),
            "FEM projected edge/endpoints lost their current source identity");
        const auto external_reference=window.active_sketch()->external_references.front().id;
        window.finish_sketch_action_->trigger();flush();const auto external_condition=condition->pending_value().id;
        condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        const auto* saved_external=workspace::find_fem_condition(session.document(),fem,external_condition);
        check(!condition_dialog()&&saved_external&&sketcher::Sketch::from_serialized(saved_external->projected_sketch->serialized).external_references.front().id==external_reference,
            "FEM projected source edge did not preserve its reference on OK");
        session.document().save(path,session.calculated_boundaries());
        check(workspace::find_fem_condition(document::PartDocument::load(path),fem,external_condition)->projected_sketch==saved_external->projected_sketch,
            "FEM projected source edge did not survive native reopen");
        window.remove_fem_condition(external_condition);flush();
        auto boolean_source=*session.document().body_history.find(fem);boolean_source.fem->source_id=joined;
        static_cast<void>(workspace::commit_fem(window.workspace_,workspace::prepare_fem_edit(session.document(),fem),boolean_source));
        window.refresh_scene();flush();
        window.findChild<QAction*>("femPressureAction")->trigger();flush();condition=condition_dialog();check(condition&&condition->active_input(),"Pressure creation did not arm geometry selection");
        view->fit_all();flush();
        std::optional<QPointF> face_hit;
        for(int y=12;y<view->height()&&!face_hit;y+=12)for(int x=12;x<view->width()&&!face_hit;x+=12)
            if(const auto choices=view->selection_candidates_at(QPointF(x,y));!choices.empty()&&choices.front().kind==viewer::CandidateKind::Face)face_hit=QPointF(x,y);
        check(face_hit.has_value(),"FEM Surface entry offers no current source faces");
        const auto click_face=[&] {
            QMouseEvent move(QEvent::MouseMove,*face_hit,QPointF(view->mapToGlobal(face_hit->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){QMouseEvent event(type,*face_hit,QPointF(view->mapToGlobal(face_hit->toPoint())),Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();}
        };
        click_face();check(condition->pending_value().references.size()==1&&!condition->active_input(),"FEM face click did not retain its exact reference");
        auto* geometry_table=condition->findChild<QTableWidget*>("femConditionGeometry");
        geometry_table->cellWidget(0,3)->findChild<QToolButton*>()->click();flush();
        check(!condition->active_input()&&condition->inspected_geometry().size()==1,"Condition eye armed geometry entry");
        const auto face_reference=condition->pending_value().references.front();
        geometry_table->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();
        check(condition->pending_value().references.empty()&&condition->active_input()&&condition->inspected_geometry().empty(),"Removing condition region did not clear inspection and rearm input");
        click_face();check(condition->pending_value().references==std::vector{face_reference},"Condition region replacement lost exact identity");
        const int geometry_top=geometry_table->mapTo(condition,QPoint{}).y();condition->resize(condition->width()+90,condition->height()+100);flush();
        check(geometry_table->mapTo(condition,QPoint{}).y()==geometry_top&&geometry_table->rowHeight(0)==32,"Condition resize displaced or stretched rows");
        condition->findChild<QDoubleSpinBox*>("femPressureValue")->setValue(2.5);flush();const auto pressure_id=condition->pending_value().id;
        condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!condition_dialog()&&workspace::find_fem_condition(session.document(),fem,pressure_id),"Pressure commit failed");
        for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
            window.findChild<QAction*>("femPressureAction")->trigger();flush();condition=condition_dialog();
            auto* region=condition->findChild<QComboBox*>("femConditionRegion");
            check(region->isEnabled(),"Pressure cannot choose its region definition");region->setCurrentIndex(2);flush();
            auto* plane_combo=condition->findChild<QComboBox*>("femProfilePlane");
            check(plane_combo&&dynamic_cast<ProfilePlaneControls*>(condition->findChild<QGroupBox*>("femPlaneGroup")->parentWidget()),"FEM does not share Feature Plane controls");
            condition->findChild<QToolButton*>(QString("femProfilePlaneChoice")+(plane==sketcher::SketchPlane::XY?"XY":plane==sketcher::SketchPlane::XZ?"XZ":"YZ"))->click();flush();
            check_plane_preview(condition,true);
            auto* offset=condition->findChild<QDoubleSpinBox*>("femSketchOffset");offset->setValue(.5);
            check_plane_preview(condition);
            offset->findChild<QAction*>("valueLock:profile_offset")->trigger();flush();
            check(offset->isReadOnly()&&condition->pending_value().placement.value_locks.contains("profile_offset"),"FEM plane offset lock is ineffective");
            condition->findChild<QPushButton*>("femEditSketch")->click();flush();
            check(window.active_sketch()->plane==plane&&!window.active_sketch()->plane_auto,"FEM Sketch did not use its explicit work plane");
            check(window.mutate_active_sketch([](auto& sketch){static_cast<void>(sketch.add_rectangle(-1,-1,1,1));}),"Pressure Sketch could not be drawn");
            window.finish_sketch_action_->trigger();flush();const auto pending=condition->pending_value();
            condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!condition_dialog(),"Pressure on a planar Sketch failed to commit");
            const auto* saved=workspace::find_fem_condition(session.document(),fem,pending.id);
            check(saved&&saved->kind==document::FemConditionKind::Pressure&&saved->projected_sketch&&saved->placement.value_locks.contains("profile_offset"),"Pressure Sketch or offset lock was lost");
            window.show_tree_item_properties(condition_row(pending.id));flush();condition=condition_dialog();
            check(condition->findChild<QDoubleSpinBox*>("femSketchOffset")->isReadOnly()&&
                selected_work_plane(condition->findChild<QComboBox*>("femProfilePlane")).toInt()==int(plane),"Pressure plane or lock did not reopen");
            check_plane_preview(condition);
            const auto revision=session.revision();condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(!condition_dialog()&&session.revision()==revision,"Unchanged Pressure Sketch OK created a transaction");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(workspace::find_fem_condition(session.document(),fem,pending.id),"Pressure Sketch Undo/Redo lost its condition");
        }
        window.findChild<QAction*>("femConstraintAction")->trigger();flush();condition=condition_dialog();
        condition->findChild<QCheckBox*>("femRestraint1")->setChecked(false);condition->findChild<QCheckBox*>("femRestraint2")->setChecked(false);
        const auto fixed_id=condition->pending_value().id;condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!condition_dialog()&&workspace::find_fem_condition(session.document(),fem,fixed_id)->restrained==std::array{true,false,false},"Partial constraint did not persist");
        window.remove_fem_condition(fixed_id);flush();check(!workspace::find_fem_condition(session.document(),fem,fixed_id),"Condition removal failed");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(workspace::find_fem_condition(session.document(),fem,fixed_id),"Condition removal Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(!workspace::find_fem_condition(session.document(),fem,fixed_id),"Condition removal Redo failed");
        session.document().save(path,session.calculated_boundaries());const auto conditions_saved=document::PartDocument::load(path);
        check(conditions_saved.body_history.find(fem)->fem->conditions==session.document().body_history.find(fem)->fem->conditions,"Native reopen lost conditions");
        check(kernel::body_calculation_count.load()==condition_count,"Condition interaction recalculated material geometry");
        std::cout<<"FEM conditions GUI: shortcuts, shared placement, whole Origin, preview, actual face picking, inspection, type changes, no-op, Cancel, MMB confirmation, Undo/Redo and native reopen passed\n";
