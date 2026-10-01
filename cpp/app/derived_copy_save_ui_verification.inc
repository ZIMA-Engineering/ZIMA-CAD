int verify_derived_copy_save(QApplication& application, zima::app::AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
    const auto body=graph.create_body("First");auto first=test::rectangular_feature(part,{8,6,4});
    graph.insert({document::PartHistoryKind::Feature,first.id});
    auto cut=test::rectangular_feature(part,{2,1,4});cut.placement.x=2;cut.combine_mode=document::CombineMode::Subtract;
    graph.insert({document::PartHistoryKind::Feature,cut.id});
    static_cast<void>(graph.create_body("Later"));auto later=test::rectangular_feature(part,{8,6,4});later.placement.x=80;
    graph.insert({document::PartHistoryKind::Feature,later.id});graph.activate(body);
    part.history={first,cut,later};part.set_body_history(graph);
    kernel::OcctKernel kernel;const auto path=directory/"copy-save.prtz";
    part.save(path,kernel.evaluate_history(part.kernel_operations()));
    window.resize(1200,960);window.show();
    check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open copy-save fixture");flush();
    auto* tree=window.findChild<QTreeWidget*>("documentTree");
    auto* save=window.findChild<QAction*>("saveDocumentAction");
    const auto row=[&](const std::string& id){for(QTreeWidgetItemIterator it(tree);*it;++it)
        if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
        return static_cast<QTreeWidgetItem*>(nullptr);};
    const auto start=[&](bool pattern){check(row(first.id),"Copy source row missing");tree->setCurrentItem(row(first.id));flush();
        window.findChild<QAction*>(pattern?"patternAction":"mirrorAction")->trigger();flush();
        auto* dialog=dynamic_cast<app::DerivedCopyDialog*>(window.findChild<QDialog*>(pattern?"patternDialog":"mirrorDialog"));
        check(dialog&&dialog->derived_copy.source_id==first.id,"Copy source was not prefilled");return dialog;};
    auto* mirror=start(false);const auto mirror_id=mirror->pending.id;
    mirror->findChild<QPushButton*>("mirrorPlane_yz")->click();mirror->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(row(mirror_id),"Mirror did not commit");
    window.show_tree_item_properties(row(mirror_id));flush();
    window.findChild<QDialog*>("mirrorDialog")->reject();flush();
    window.findChild<QAction*>("undoAction")->trigger();flush();check(!row(mirror_id),"Mirror Undo failed");
    auto* pattern=start(true);const auto pattern_id=pattern->pending.id;
    pattern->findChild<QComboBox*>("patternMode")->setCurrentIndex(1);
    pattern->buttons()->button(QDialogButtonBox::Ok)->click();flush();
    check(row(pattern_id),"Pattern did not commit");check(save&&save->isEnabled(),"Save is disabled after copy commit");
    save->trigger();flush();
    const auto stored=document::PartDocument::load(path);const auto* feature=stored.find_container(pattern_id);
    check(feature&&feature->derived_copy.source_id==first.id,"Saved Part is missing the committed circular Pattern");
    std::cout<<"Derived copy save after Undo and branching passed\n";return 0;
}
