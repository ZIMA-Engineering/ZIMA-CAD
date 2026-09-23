int verify_sheet_transition_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        auto part=document::PartDocument::load(qEnvironmentVariable("ZIMA_VERIFY_SHEET_TRANSITION").toStdString());
        const auto source=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetTransition;});
        check(source!=part.history.end(),"Transition GUI fixture has no transition");const auto initial=*source;
        const auto body=part.body_owner_for_object(initial.id)->scope.id;part.erase_history_object(initial.id);
        kernel::OcctKernel kernel;auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto path=directory/"transition-ui.prtz";part.save(path,cache);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Transition fixture did not open");flush();
        check(activate_test_body(application,window,body),"Transition Body could not activate");
        window.findChild<QAction*>("applicationModeAction2")->trigger();flush();
        auto* action=window.findChild<QAction*>("sheetTransitionAction");check(action&&action->isEnabled()&&!action->icon().isNull(),"Transition action unavailable");
        const auto dialog=[&]{auto* d=window.findChild<QDialog*>("sheetTransitionDialog");check(d&&d->isVisible(),"Transition dialog did not open");return d;};
        const auto save=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path);};
        action->trigger();flush();dialog()->reject();flush();check(save().history.size()==part.history.size(),"Creation Cancel modified history");
        action->trigger();flush();auto* d=dialog();
        check(d->windowType()==Qt::SubWindow,"Transition properties are not an internal subwindow");
        auto* editor=dynamic_cast<app::SheetTransitionDialog*>(d);check(editor,"Wrong transition editor");
        check(editor->findChild<QTableWidget*>("sweepPlacementReferences")&&editor->findChild<QTableWidget*>("sweepPlacementOrientation"),"Shared placement controls missing");
        check(editor->findChild<QPushButton*>("containerOriginSelectionButton"),"Whole-Origin selection missing");
        for(unsigned i=0;i<2;++i){
            auto* button=editor->findChild<QPushButton*>(QString("transitionSketch%1").arg(i));check(button,"Owned SKETCH button missing");
            button->click();flush();check(!editor->isVisible(),"SKETCH did not enter Sketcher");
            auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Sketcher Finish missing");finish->trigger();flush();check(dialog()==editor,"Sketcher did not return to the same editor");
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
        check(editor->set_inline_parameter_value("end_z",175),"Second Origin dimension is not editable");flush();
        auto* mesh_view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(mesh_view,"Mesh View missing");
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==editor->pending.id&&dimension.reference.semantic_key=="parameter:end_z";}),"Relative Origin dimension missing from View");
        const auto profile=sketcher::Sketch::from_serialized(editor->pending.sheet_transition.sketches[0]);
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==profile.id;}),"Owned Sketch dimensions missing from View");
        mesh_view->set_view_direction({1,-1,1});mesh_view->fit_all();flush();
        window.grab().save(QString::fromStdString((directory/"transition-properties.png").string()));
        auto* view=window.findChild<QWidget*>("modelWorkspace");check(view,"Transition View missing");
        const QPointF point(view->rect().center());QMouseEvent middle(QEvent::MouseButtonDblClick,point,point,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&middle);flush();
        check(!window.findChild<QDialog*>("sheetTransitionDialog"),"MMB double-click did not commit transition");
        const auto saved=save();check(saved.history.size()==part.history.size()+1,"Creation did not insert one feature");const auto feature=saved.history.back();
        check(feature.feature_kind==document::FeatureKind::SheetTransition&&feature.sheet_transition.end_position.z==175&&feature.placement.references.size()>=3,"UI lost transition parameters");
        window.findChild<QAction*>("undoAction")->trigger();flush();check(save().history.size()==part.history.size(),"Transition UI Undo failed");
        window.findChild<QAction*>("redoAction")->trigger();flush();check(save().history.size()==saved.history.size(),"Transition UI Redo failed");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
        check(row,"Transition absent from Tree");window.show_tree_item_properties(row);flush();
        d=dialog();
        check(std::ranges::any_of(mesh_view->mesh().dimensions,[&](const auto& dimension){return dimension.reference.owner_id==feature.id&&dimension.reference.semantic_key=="parameter:end_z";}),"Reopened container has no editable distance");
        d->findChild<QDoubleSpinBox*>("transitionThickness")->setValue(3);d->reject();flush();
        check(save().find_container(feature.id)->sheet_transition==feature.sheet_transition,"Edit Cancel changed transition");
        mesh_view->set_view_direction({1,-1,1});mesh_view->fit_all();flush();
        window.grab().save(QString::fromStdString((directory/"transition-native.png").string()));
        for(bool unfold:{true,false}) {
            auto* change=window.findChild<QAction*>(unfold?"unbendAction":"bendBackAction");check(change&&change->isEnabled(),"Sheet state action unavailable");
            change->trigger();flush();auto* state_dialog=window.findChild<QDialog*>("sheetStateDialog");check(state_dialog,"Sheet state dialog did not open");
            state_dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            check(!window.findChild<QDialog*>("sheetStateDialog"),"Transition sheet state did not commit");
            check(save().history.back().feature_kind==(unfold?document::FeatureKind::Unbend:document::FeatureKind::BendBack),"Wrong committed sheet state");
            if(unfold)window.grab().save(QString::fromStdString((directory/"transition-native-unfolded.png").string()));
        }
        window.close();flush();std::cout<<"Native transition UI passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Transition UI: "<<error.what()<<'\n';return 1;}
}
