void verify_relief_depth_edit(QApplication& application,AssemblyWorkspaceWindow& window,viewer::MeshView* view,
        const std::function<QTreeWidgetItem*()>& row,const std::function<PrimitivePropertiesDialog*()>& editor,
        const std::string& owner,const std::filesystem::path& file) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    const auto twice=[&](QWidget* widget,QPointF at) {
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease,
                QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,at,QPointF(widget->mapToGlobal(at.toPoint())),Qt::LeftButton,
                type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);
        }
        flush();
    };
    const auto inspect=[&] {tree->scrollToItem(row());flush();twice(tree->viewport(),tree->visualItemRect(row()).center());};
    const auto candidate=[&] {
        // Properties publish lightweight transient dimensions, while ordinary
        // inspection uses the base scene. Both share the public picker indices.
        viewer::ViewerCandidate result{viewer::CandidateKind::Dimension,0.,0,owner,"parameter:relief_depth",{}};
        while(const auto dimension=view->dimension_source(result)) {
            if(dimension->reference.owner_id==owner&&dimension->reference.semantic_key==result.semantic_key) {
                const auto delta=kernel::dimension_sub(dimension->witness_second,dimension->witness_first);
                check(std::abs(std::sqrt(kernel::dimension_dot(delta,delta))-std::abs(dimension->value))<1e-6,"Relief depth witnesses do not match the signed offset");
                return result;
            }
            ++result.geometry_index;
        }
        std::cerr<<"Depth diagnostic: base="<<view->mesh().dimensions.size()<<" total="<<result.geometry_index<<" wire="<<view->transient_edges().size()<<'\n';
        for(const auto& edge:view->transient_edges())if(edge.reference.semantic_key.ends_with(":wall"))
            std::cerr<<"wall "<<edge.reference.owner_id<<' '<<edge.reference.semantic_key<<' '<<edge.points.size()<<'\n';
        window.grab().save(QString::fromStdWString((file.parent_path()/"depth-missing.png").wstring()));
        throw std::runtime_error("Relief depth dimension is missing from View");
    };
    const auto before=window.execute_console_command("documents").data;const auto calls=kernel::body_calculation_count.load();
    std::cout<<"Relief depth initial inspection\n"<<std::flush;
    inspect();auto pick=candidate();const double original=*view->candidate_dimension_value(pick);
    check(window.execute_console_command("documents").data==before&&kernel::body_calculation_count.load()==calls,"Depth inspection changed history or calculated geometry");
    for(double value:{original,-1.5,2.,original}) {
        std::cout<<"Relief depth edit "<<value<<'\n'<<std::flush;
        pick=candidate();const auto label=view->candidate_dimension_label_position(pick);check(label.has_value(),"Depth dimension label cannot be picked");
        twice(view,*label);auto* input=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(input,"Depth dimension double click did not open inline editor");
        check(input->property("dimensionIdentifier").toString()==window.dimension_identifier(owner,"parameter:relief_depth"),
            "Depth double click selected a different dimension");
        input->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(input,&enter);flush();
        if(value==original&&window.execute_console_command("documents").data==before)
            check(kernel::body_calculation_count.load()==calls,"Unchanged depth edit calculated geometry");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(file,&cache);const auto* feature=saved.find_container(owner);
        check(feature&&!cache.empty()&&cache.back().calculation_errors.empty(),"Signed depth edit produced an unresolved result");
        const double height=feature->feature.type==document::FeatureType::Wrapping?feature->feature.wrapping.height:feature->feature.projection.height;
        if(!(std::abs(height-std::abs(value))<1e-8&&feature->combine_mode==(value<0?document::CombineMode::Subtract:document::CombineMode::Add))) {
            window.grab().save(QString::fromStdWString((file.parent_path()/"depth-edit-failure.png").wstring()));
            for(const auto* label:window.findChildren<QLabel*>())if(label->isVisible()&&!label->text().isEmpty())
                std::cerr<<"Depth edit status: "<<label->text().toStdString()<<'\n';
            throw std::runtime_error("Depth sign or magnitude did not persist: requested="+std::to_string(value)+
                " height="+std::to_string(height)+" mode="+std::to_string(static_cast<int>(feature->combine_mode)));
        }
        inspect();check(std::abs(*view->candidate_dimension_value(candidate())-value)<1e-8,"Depth inspection retained a stale value");
    }
    window.show_tree_item_properties(row());flush();check(editor(),"Depth Properties did not reopen");
    std::cout<<"Relief depth draft inspection\n"<<std::flush;
    const auto draft_state=window.execute_console_command("documents").data;const auto draft_calls=kernel::body_calculation_count.load();
    for(double value:{-2.,3.,-1.}) {
        check(editor()->set_inline_parameter_value("relief_depth",value),"Properties refused signed depth input");flush();
        check(std::abs(*view->candidate_dimension_value(candidate())-value)<1e-8,"Properties depth annotation did not follow the draft");
    }
    editor()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    check(window.execute_console_command("documents").data==draft_state&&kernel::body_calculation_count.load()==draft_calls,"Depth draft or Cancel calculated/committed geometry");
    window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
    inspect();check(std::abs(*view->candidate_dimension_value(candidate())-original)<1e-8,"Depth Undo/Redo lost its signed value");
    std::cout<<"Relief depth: signed witnesses, actual View double-click, successive positive/negative edits, unchanged no-op, native reopen, draft Cancel and Undo/Redo passed\n";
}

void verify_relief_failure_confirmation(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::function<QTreeWidgetItem*()>& row,const std::function<PrimitivePropertiesDialog*()>& editor,
        const std::string& owner,const char* target_table,const std::filesystem::path& file) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto unchanged=window.execute_console_command("documents").data;
    for(bool accept:{false,true}) {
        window.show_tree_item_properties(row());flush();check(editor(),"Relief failure editor did not open");
        auto* targets=editor()->findChild<QTableWidget*>(target_table);check(targets,"Relief target entry is missing");
        // A missing target retains the authored definition and owned Sketch.
        while(targets->rowCount()>1) {
            auto* clear=targets->cellWidget(0,targets->columnCount()==5?2:1)->findChild<QPushButton*>();
            check(clear&&clear->isEnabled(),"Relief target clear is unavailable");clear->click();flush();
        }
        auto* ok=editor()->buttons()->button(QDialogButtonBox::Ok);
        check(ok->isEnabled(),"Relief cannot confirm an unresolved draft");ok->click();flush();
        check(editor()&&window.execute_console_command("documents").data==unchanged,
            "First failed relief OK closed Properties or changed history");
        auto* notice=editor()->findChild<QLabel*>("propertiesSubmitError");
        check(notice&&notice->isVisible()&&!notice->text().isEmpty(),"Relief omitted the second-OK unresolved-definition notice");
        const auto calls=kernel::body_calculation_count.load();
        editor()->buttons()->button(accept?QDialogButtonBox::Ok:QDialogButtonBox::Cancel)->click();flush();
        check(!editor()&&kernel::body_calculation_count.load()==calls,"Second relief OK/Cancel recalculated or failed to close");
        if(!accept)check(window.execute_console_command("documents").data==unchanged,"Cancel retained the failed relief draft");
        else {
            check(row()&&row()->foreground(0).color()==QColor(210,75,65),"Second OK lost the red unresolved relief Tree row");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(file,&cache);
            check(saved.find_container(owner)&&!cache.empty()&&cache.back().calculation_errors.contains(owner),
                "Second OK lost the unresolved relief in the native file");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(row()&&row()->foreground(0).color()==QColor(210,75,65),"Redo lost the unresolved relief");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            cache.clear();const auto repaired=document::PartDocument::load(file,&cache);
            check(repaired.find_container(owner)&&cache.back().calculation_errors.empty(),"Undo did not restore the valid relief");
        }
    }
    std::cout<<"Relief first failed OK, Cancel, second unchanged OK, red Tree row, native save and Undo/Redo passed\n";
}
