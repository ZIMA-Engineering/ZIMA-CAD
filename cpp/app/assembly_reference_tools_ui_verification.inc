int verify_assembly_reference_tools(QApplication& application,
        zima::app::AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{
            {"command",command},{"arguments",std::move(args)}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
        flush();return result.data;
    };
    window.resize(1280,900);window.show();flush();
    run("new",{{"type","assembly"},{"name","reference-tools"}});
    for(const auto* name:{"extrusionAction","revolutionAction","featureShortcut4Action","featureShortcut5Action"}) {
        auto* action=window.findChild<QAction*>(name);check(action&&!action->isEnabled(),"Assembly material action remains enabled");
        for(auto* button:window.findChildren<QToolButton*>())
            check(button->defaultAction()!=action||!button->isVisible(),"Assembly material shortcut remains visible");
    }
    for(const auto* name:{"featurePrototypeAction","curve3DAction","mirrorAction","patternAction"}) {
        auto* action=window.findChild<QAction*>(name);check(action&&action->isEnabled(),"Assembly reference command was removed");
    }
    const auto count=kernel::body_calculation_count.load();
    for(int i=0;i<4;++i) {
        auto* action=window.findChild<QAction*>(QStringLiteral("featureShortcut%1Action").arg(i));
        check(action&&action->isEnabled(),"Assembly datum shortcut is disabled");action->trigger();flush();
        auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
        check(dialog&&dialog->isVisible(),"Assembly reference properties did not open");
        auto* type=dialog->findChild<QComboBox*>("featureType");
        check(type&&type->currentIndex()==i&&type->model()->flags(type->model()->index(i,0)).testFlag(Qt::ItemIsEnabled),
            "Assembly reference shortcut selected the wrong type");
        check(!type->model()->flags(type->model()->index(4,0)).testFlag(Qt::ItemIsEnabled),
            "Assembly reference properties offer material conversion");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
    }
    check(kernel::body_calculation_count.load()==count,"Opening reference commands calculated geometry");
    run("new",{{"type","part"},{"name","reference-tools-part"}});
    for(const auto* name:{"extrusionAction","revolutionAction","featureShortcut4Action","featureShortcut5Action"}) {
        auto* action=window.findChild<QAction*>(name);check(action&&action->isEnabled(),"Assembly restriction leaked into Part modeling");
    }
    const auto source=qEnvironmentVariable("ZIMA_VERIFY_FEM_REORDER_SOURCE");
    if(!source.isEmpty()) {
        const auto path=directory/"fem-reorder.prtz";
        std::filesystem::copy_file(std::filesystem::u8path(source.toStdString()),path);
        std::vector<kernel::BodyResult> initial_cache;
        const auto original=document::PartDocument::load(path,&initial_cache);
        const auto fem=std::ranges::find_if(original.body_history.bodies(),[](const auto& b){return b.fem.has_value();})->scope.id;
        const auto mirror=std::ranges::find_if(original.body_history.bodies(),[](const auto& b){return b.derived_copy.has_value();})->scope.id;
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open private reorder fixture");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const std::string& id)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3)=="part-body")return *it;
            return nullptr;
        };
        check(row(mirror)&&row(fem),"Body reorder fixture has no Tree rows");tree->expandAll();tree->scrollToItem(row(mirror));flush();
        const auto start=tree->visualItemRect(row(mirror)).center();
        const auto target=QPoint(start.x(),tree->visualItemRect(row(fem)).top()+2);
        const auto mouse=[&](QEvent::Type type,QPoint position,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,QPointF(position),QPointF(tree->viewport()->mapToGlobal(position)),button,buttons,Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(),&event);flush();
        };
        const auto before=kernel::body_calculation_count.load();QElapsedTimer timer;timer.start();
        mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
        check(kernel::body_calculation_count.load()==before,"Tree drag recalculated unchanged material");
        const auto elapsed=timer.elapsed();run("save");
        const auto saved=document::PartDocument::load(path);
        const auto& order=saved.body_history.order();
        check(std::ranges::find(order,mirror)<std::ranges::find(order,fem),"Mouse drag did not move Mirror before FEM");
        run("undo");run("redo");
        check(kernel::body_calculation_count.load()==before,"Reorder Save/Undo/Redo recalculated material");
        std::cout<<"Real copied model Tree mouse drag: "<<elapsed<<" ms, zero CAD calculations, native order and Undo/Redo passed\n";
    }
    std::cout<<"Assembly reference tools retained; material shortcuts/conversion unavailable; Part tools restored\n";
    return 0;
}
