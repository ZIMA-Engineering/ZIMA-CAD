        window.activate_body(fem);flush();
        const auto condition_dialog=[&]{return dynamic_cast<FemConditionDialog*>(window.properties_dialog_);};
        const auto condition_row=[&](const std::string& id)->QTreeWidgetItem*{
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="fem-condition"&&(*it)->data(0,Qt::UserRole).toString().toStdString()==id)return *it;return nullptr;
        };
        const auto condition_count=kernel::body_calculation_count.load();
        const auto condition_revision=session.revision();
        const auto force_action=[&]{return window.findChild<QAction*>("femForceAction");};
        check(force_action()&&force_action()->isEnabled(),"FEM Force shortcut is missing");
        force_action()->trigger();flush();auto* condition=condition_dialog();
        check(condition&&condition->pending_value().kind==document::FemConditionKind::Force,"Force shortcut opened another condition type");
        check(condition->parentWidget()==&window&&(condition->windowFlags()&Qt::WindowType_Mask)==Qt::SubWindow&&!condition->buttons()->button(QDialogButtonBox::Apply),"Condition properties bypass shared presentation");
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
        check(tree->currentItem()==condition_row(force_id)&&session.revision()==saved_revision&&!condition_dialog(),"FEM View selection failed Tree synchronization or changed the document");
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
        for(auto region:{document::FemRegionKind::Point,document::FemRegionKind::Curve}) {
            force_action()->trigger();flush();condition=condition_dialog();
            auto* region_field=condition->findChild<QComboBox*>("femConditionRegion");region_field->setCurrentIndex(region_field->findData(int(region)));condition->arm(0);flush();view->fit_all();flush();
            std::optional<QPointF> hit;
            for(int y=3;y<view->height()&&!hit;y+=6)for(int x=3;x<view->width()&&!hit;x+=6)
                if(!view->selection_candidates_at(QPointF(x,y)).empty())hit=QPointF(x,y);
            check(hit.has_value(),region==document::FemRegionKind::Point?"FEM point entry offers no source points":"FEM curve entry offers no source curves");
            for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            }
            const auto value=condition->pending_value();check(value.references.size()==1&&!condition->active_input(),"FEM Point/Curve View click did not retain an exact reference");
            condition->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            const auto* stored=workspace::find_fem_condition(session.document(),fem,value.id);
            check(!condition_dialog()&&stored&&stored->region==region&&stored->references==value.references,"FEM Point/Curve region did not persist");
            window.show_tree_item_properties(condition_row(value.id));flush();condition=condition_dialog();
            condition->findChild<QTableWidget*>("femConditionGeometry")->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();condition->reject();flush();
            check(workspace::find_fem_condition(session.document(),fem,value.id)->references==value.references,"FEM region removal Cancel changed authored geometry");
        }
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
