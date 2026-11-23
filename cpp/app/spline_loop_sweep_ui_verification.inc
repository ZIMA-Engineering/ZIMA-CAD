int verify_spline_loop_sweep_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    std::cout<<"Spline-loop GUI: prepare isolated copy"<<std::endl;
    auto doc=test_support::spline_loop_cut_document();
    std::vector<kernel::BodyResult> cached;
    const auto input=qEnvironmentVariable("ZIMA_VERIFY_SPLINE_LOOP_SWEEP_DOCUMENT");
    if(!input.isEmpty())doc=document::PartDocument::load(std::filesystem::path(input.toStdWString()),&cached);
    kernel::OcctKernel engine;const auto expected=engine.evaluate_history(doc.kernel_operations());
    if(cached.empty())cached=expected;
    const auto file=directory/"spline-loop-gui-copy.prtz";doc.save(file,cached);
    app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));
    if(qEnvironmentVariableIsSet("ZIMA_VERIFY_HIDDEN_WINDOWS"))window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1600,1050);window.show();
    const auto flush=[&]{for(int i=0;i<4;++i){application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}};
    if(!verify(window.open_document_path(QString::fromStdWString(file.wstring())),"Cannot open spline-loop GUI copy"))return 1;
    flush();
    std::cout<<"Spline-loop GUI: opened"<<std::endl;
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    if(!verify(view&&tree,"Spline-loop GUI view is missing"))return 1;
    window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
    std::cout<<"Spline-loop GUI: regenerated"<<std::endl;
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    std::vector<kernel::BodyResult> stored;const auto reopened=document::PartDocument::load(file,&stored);
    if(!verify(stored.size()==expected.size()&&std::abs(stored.back().volume-expected.back().volume)<1e-5,
            "GUI regeneration did not publish the complete spline-loop cut"))return 1;
    const auto owner=doc.history.back().id;
    const auto points_valid=[](const auto& document) {
        return document.history.back().sweep3d.path.reference_valid&&
            std::ranges::all_of(document.history.back().sweep3d.path.curve_points,
                [](const auto& point){return point.reference_valid;});
    };
    if(!verify(points_valid(reopened),"Regeneration invalidated the spline-loop point supports"))return 1;
    const auto edit=[&]() -> app::ConstructionPropertiesDialog* {
        QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner) {
            row=*it;break;
        }
        if(row){window.show_tree_item_properties(row);flush();}
        for(auto* child:window.findChildren<QDialog*>())if(auto* dialog=dynamic_cast<app::ConstructionPropertiesDialog*>(child);dialog&&dialog->isVisible())return dialog;
        return nullptr;
    };
    auto before=kernel::body_calculation_count.load();QPointer<app::ConstructionPropertiesDialog> dialog=edit();
    if(!verify(dialog!=nullptr,"Spline-loop Properties did not open"))return 1;
    dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    std::cout<<"Spline-loop GUI: cancelled Properties"<<std::endl;
    if(!verify(kernel::body_calculation_count.load()==before,"Spline-loop Cancel recalculated geometry"))return 1;
    dialog=edit();if(!verify(dialog!=nullptr,"Cannot reopen spline-loop Properties"))return 1;
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    if(!verify((!dialog||!dialog->isVisible())&&kernel::body_calculation_count.load()==before,"Unchanged spline-loop OK recalculated geometry"))return 1;
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto unchanged=document::PartDocument::load(file,&stored);
    if(!verify(unchanged.history.back().combine_mode==document::CombineMode::Subtract&&
            unchanged.history.back().sweep3d==reopened.history.back().sweep3d,
            "Unchanged spline-loop Properties changed the definition"))return 1;
    dialog=edit();if(!verify(dialog!=nullptr,"Cannot edit the spline-loop section"))return 1;
    auto profile=sketcher::Sketch::from_serialized(dialog->sweep_profile(0)->sketch_serialized);
    if(!verify(profile.circles.size()==1,"Spline-loop GUI fixture needs one circular section"))return 1;
    profile.circles.front().radius*=.8;
    dialog->set_sweep_profile_sketch(0,profile);flush();
    if(!verify(kernel::body_calculation_count.load()==before,"Spline-loop draft calculated a Body"))return 1;
    dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    if(!verify(!dialog||!dialog->isVisible(),"Changed spline-loop OK did not close Properties"))return 1;
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto changed=document::PartDocument::load(file,&stored);
    if(!verify(points_valid(changed),"Section editing invalidated the spline-loop point supports"))return 1;
    const auto changed_expected=engine.evaluate_history(changed.kernel_operations());
    if(!verify(changed.history.back().sweep3d!=unchanged.history.back().sweep3d&&
            stored.back().volume>expected.back().volume&&
            std::abs(stored.back().volume-changed_expected.back().volume)<1e-5,
            "Editing the spline-loop section did not update the complete cut"))return 1;
    window.findChild<QAction*>("undoAction")->trigger();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto undone=document::PartDocument::load(file,&stored);
    if(!verify(undone.history.back().sweep3d==unchanged.history.back().sweep3d&&
            std::abs(stored.back().volume-expected.back().volume)<1e-5,
            "Spline-loop Undo lost its original section or cut"))return 1;
    window.findChild<QAction*>("redoAction")->trigger();flush();
    window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
    const auto redone=document::PartDocument::load(file,&stored);
    if(!verify(redone.history.back().sweep3d==changed.history.back().sweep3d&&
            std::abs(stored.back().volume-changed_expected.back().volume)<1e-5,
            "Spline-loop Redo lost its edited section or cut"))return 1;
    if(!verify(points_valid(undone)&&points_valid(redone),"Spline-loop Undo/Redo lost valid point supports"))return 1;
    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner)
        if(!verify(!(*it)->data(0,app::missing_reference_role).toBool(),"Spline-loop Tree reports a missing reference"))return 1;
    view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();flush();
    if(!verify(window.grab().save(QString::fromStdWString((directory/"spline-loop-cut.png").wstring())),"Cannot capture spline-loop cut"))return 1;
    std::cout<<"Spline-loop GUI regeneration, save/reopen, Properties Cancel, unchanged OK, section edit and Undo/Redo passed\n";
    return 0;
}
