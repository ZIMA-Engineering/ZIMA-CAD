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
