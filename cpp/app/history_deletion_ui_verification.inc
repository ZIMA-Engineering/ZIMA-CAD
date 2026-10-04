Q_NEVER_INLINE static int verify_history_deletion(QApplication& application,
        AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        auto doc=document::PartDocument::create_default();
        auto source=test::rectangular_feature(doc,{20,20,20});doc.history={source};
        kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(doc.kernel_operations());
        auto fillet=document::PartDocument::create_fillet_container({calculated.back().mesh.edges.front().reference});
        doc.history.push_back(fillet);
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Deletion");
        for(const auto& f:doc.history)graph.insert({document::PartHistoryKind::Feature,f.id});doc.set_body_history(graph);
        calculated=kernel.evaluate_history_recovering(doc.kernel_operations(false,true));
        const auto path=directory/"history-deletion-ui.prtz";doc.save(path,calculated);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"Deletion fixture did not open");flush();
        const auto run=[&](const QString& command){auto result=window.execute_console_command(command);check(result.ok,result.message.c_str());flush();};
        run(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump()));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const std::string& id)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3).toString()=="part-container")return *it;
            return nullptr;
        };
        const auto original_color=row(fillet.id)->foreground(0);
        const auto toggle_suppression=[&](const std::string& id) {
            auto* item=row(id);check(item,"Suppression tree row missing");tree->setCurrentItem(item);tree->scrollToItem(item);flush();
            QTimer::singleShot(0,&window,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                auto* action=menu->findChild<QAction*>("suppressHistoryObject");if(!action){menu->close();return;}
                menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
            });
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center()));flush();
        };
        {
            toggle_suppression(source.id);
            check(row(source.id)&&row(fillet.id)&&row(source.id)->font(0).strikeOut()&&row(fillet.id)->font(0).strikeOut(),
                "Tree suppression did not mark the source and its dependent together");
            run("save");const auto suppressed=document::PartDocument::load(path);
            check(suppressed.find_container(source.id)->suppressed&&suppressed.find_container(fillet.id)->suppressed,
                "Tree suppression was not persisted");
            run("undo");check(!row(source.id)->font(0).strikeOut()&&!row(fillet.id)->font(0).strikeOut(),
                "Tree suppression Undo did not restore both rows");
            run("redo");check(row(source.id)->font(0).strikeOut()&&row(fillet.id)->font(0).strikeOut(),
                "Tree suppression Redo did not restore both rows");
            toggle_suppression(fillet.id);
            check(!row(source.id)->font(0).strikeOut()&&!row(fillet.id)->font(0).strikeOut(),
                "Restoring a dependent Tree row did not restore its prerequisite");run("undo");
            toggle_suppression(source.id);
            check(!row(source.id)->font(0).strikeOut()&&row(fillet.id)->font(0).strikeOut(),
                "Restoring a source Tree row restored its downstream dependent");run("undo");run("undo");
        }
        const auto open_confirmation=[&] {
            auto* item=row(source.id);check(item,"Source tree row missing");tree->setCurrentItem(item);tree->scrollToItem(item);flush();
            QTimer::singleShot(0,&window,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                auto* action=menu->findChild<QAction*>("deleteHistoryObject");if(!action){menu->close();return;}
                menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
            });
            QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center()));flush();
            auto* dialog=window.findChild<QDialog*>("deleteHistoryDialog");check(dialog&&dialog->isVisible(),"Internal deletion confirmation missing");
            check(dialog->windowType()==Qt::SubWindow,"Deletion uses a native dialog");
            check(row(fillet.id)->foreground(0).color()==QColor(210,75,65),"Dependent preview is not red");return dialog;
        };
        open_confirmation()->reject();flush();
        check(row(source.id)&&row(fillet.id)->foreground(0)==original_color,"Cancel did not restore tree");
        auto* dialog=open_confirmation();
        window.grab().save(QString::fromStdString((directory/"history-deletion-confirmation.png").string()));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QDialog*>("deleteHistoryDialog"),"Deletion OK failed");
        check(!row(source.id)&&row(fillet.id)&&row(fillet.id)->foreground(0).color()==QColor(210,75,65),"Dependent did not remain red after deletion");
        run("save");const auto saved=document::PartDocument::load(path);
        check(saved.removed_reference_states.contains(fillet.id),"Save lost incomplete state");
        window.show_tree_item_properties(row(fillet.id));flush();
        PrimitivePropertiesDialog* repair_dialog{};
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
            if(auto* properties=dynamic_cast<PrimitivePropertiesDialog*>(candidate))repair_dialog=properties;
        check(repair_dialog,"Incomplete Fillet Properties cannot be opened for repair");
        repair_dialog->reject();flush();
        run("undo");check(row(source.id),"Deletion Undo lost source");
        run("redo");check(!row(source.id)&&row(fillet.id),"Deletion Redo lost dependent");
        run("undo");open_confirmation();
        auto* view=window.findChild<QWidget*>("modelWorkspace");check(view,"Model View missing");
        const QPointF point(25,25),global=view->mapToGlobal(point.toPoint());
        QMouseEvent middle(QEvent::MouseButtonRelease,point,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&middle);flush();
        check(window.findChild<QDialog*>("deleteHistoryDialog"),"Short middle click committed deletion");
        QMouseEvent double_middle(QEvent::MouseButtonDblClick,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&double_middle);flush();
        check(!window.findChild<QDialog*>("deleteHistoryDialog")&&!row(source.id),"Middle double-click over View did not confirm deletion");
        std::cout<<"History deletion GUI: preview, Cancel, OK, save, Undo and Redo passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
