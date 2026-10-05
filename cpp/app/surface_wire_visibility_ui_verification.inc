static void toggle_surface_wire_tree_ui(QApplication& application,QTreeWidget* tree,
        QTreeWidgetItem* item,bool currently_visible) {
    if(!item)throw std::runtime_error("Surface wire has no Tree row");
    for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
    tree->scrollToItem(item);application.processEvents();
    bool found=false,translated=false;
    QTimer::singleShot(0,tree,[&] {
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
        for(auto* action:menu->actions())if(action->objectName()=="surfaceWireVisibilityAction") {
            found=action->isEnabled();translated=action->text()==QObject::tr(currently_visible?"Skrýt":"Zobrazit");
            action->trigger();break;
        }
        menu->close();
    });
    QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,
        Q_ARG(QPoint,tree->visualItemRect(item).center()));
    application.processEvents();
    if(!found||!translated)throw std::runtime_error("Surface wire Show/Hide Tree action is missing or untranslated");
}

static void verify_surface_wire_reopened_ui(QApplication& application,
        const std::filesystem::path& path,const std::string& packet_owner,bool visible) {
    QTemporaryDir settings;
    if(!settings.isValid())throw std::runtime_error("Cannot isolate reopened wire settings");
    AssemblyWorkspaceWindow reopened(QString::fromStdString(path.parent_path().string()),settings.path());
    reopened.resize(1280,900);reopened.show();
    if(!reopened.open_document_path(QString::fromStdString(path.string())))
        throw std::runtime_error("Cannot reopen wire presentation fixture");
    application.processEvents();
    auto* view=dynamic_cast<viewer::MeshView*>(reopened.findChild<QWidget*>("modelWorkspace"));
    if(!view||std::ranges::any_of(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==packet_owner;})!=visible)
        throw std::runtime_error("Native wire presentation changed after real GUI reopen");
}
