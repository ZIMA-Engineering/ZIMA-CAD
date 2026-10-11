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
        for(const auto& f:doc.history)graph.insert({document::PartHistoryKind::Feature,f.id});
        for(const auto kind:{document::ConstructionKind::Point,document::ConstructionKind::Axis,
                document::ConstructionKind::Plane,document::ConstructionKind::Curve3D}) {
            auto object=document::PartDocument::create_construction(kind);
            if(kind==document::ConstructionKind::Curve3D)for(int i=0;i<2;++i) {
                auto point=document::create_owned_point(object.id);point.origin={double(i*10),30,0};
                object.curve_points.push_back(point);
            }
            graph.insert({document::PartHistoryKind::Construction,object.id});doc.constructions.push_back(object);
        }
        doc.set_body_history(graph);
        calculated=kernel.evaluate_history_recovering(doc.kernel_operations(false,true));
        const auto path=directory/"history-deletion-ui.prtz";doc.save(path,calculated);
        window.showMaximized();check(window.open_document_path(QString::fromStdString(path.string())),"Deletion fixture did not open");flush();
        const auto run=[&](const QString& command){auto result=window.execute_console_command(command);check(result.ok,result.message.c_str());flush();};
        run(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump()));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto row=[&](const std::string& id)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                   ((*it)->data(0,Qt::UserRole+3).toString()=="part-container"||
                    (*it)->data(0,Qt::UserRole+3).toString()=="part-construction"))return *it;
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
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Model View missing");
        const auto view_menu=[&](const std::string& id) {
            view->confirm_container(id);
            check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==id,"View target was not confirmed");
            const QPointF point=view->rect().center(),global=view->mapToGlobal(point.toPoint());
            QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
        };
        // Compare every action, including enabled states, through actual Tree
        // and View context-menu entry. Merely exercising deletion misses
        // Properties, Edit, suppression and visibility regressions.
        const auto menu_ids=[&] {
                std::vector<std::string> ids{source.id,fillet.id};
                for(const auto& object:doc.constructions)ids.push_back(object.id);return ids;
            }();
        const auto original_language=app::ApplicationSettings::load();
        QTemporaryDir language_directory;check(language_directory.isValid(),"Language test configuration missing");
        for(const QString language:{QString("cs"),QString("en"),QString("de"),QString("fr"),QString("ru")}) {
            QSettings config(language_directory.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString((std::filesystem::current_path()/"config/localization").generic_string()));config.sync();
            const auto language_settings=app::ApplicationSettings::load(language_directory.path());
            app::apply_application_translations(application,language_settings);
            check(window.tr("Odstranit")==language_settings.qt_translations.value("Odstranit"),"Delete is not localized");
        for(const auto& id:menu_ids) {
            const auto inspect_menu=[&](bool from_view) {
                QStringList actions;bool opened=false;
                auto* item=row(id);check(item,"Context-menu row missing");
                tree->setCurrentItem(item);tree->scrollToItem(item);flush();
                QTimer::singleShot(0,&window,[&] {
                    auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                    opened=true;for(auto* action:menu->actions())if(!action->isSeparator())
                        actions.push_back(action->text()+QString::number(action->isEnabled()));
                    menu->close();
                });
                if(from_view)view_menu(id);
                else {
                    QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center()));flush();
                }
                check(opened,"Context menu was not opened");
                check(actions.contains(window.tr("Odstranit")+"1"),"Removable View/Tree container lacks Delete");return actions;
            };
            const auto tree_actions=inspect_menu(false);check(inspect_menu(true)==tree_actions,"View and Tree context actions differ");
        }
        }
        app::apply_application_translations(application,original_language);
        const auto open_confirmation=[&](bool from_view=false) {
            auto* item=row(source.id);check(item,"Source tree row missing");tree->setCurrentItem(item);tree->scrollToItem(item);flush();
            QTimer::singleShot(0,&window,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
                auto* action=menu->findChild<QAction*>("deleteHistoryObject");if(!action){menu->close();return;}
                menu->setActiveAction(action);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
            });
            if(from_view) {
                // An unrelated Ctrl-selected row must never join a View delete.
                row(fillet.id)->setSelected(true);view_menu(source.id);
                check(tree->selectedItems().size()==1&&tree->selectedItems().front()==row(source.id),"View Delete retained unrelated Tree targets");
            } else {
                QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center()));flush();
            }
            auto* dialog=window.findChild<QDialog*>("deleteHistoryDialog");check(dialog&&dialog->isVisible(),"Internal deletion confirmation missing");
            check(dialog->windowType()==Qt::SubWindow,"Deletion uses a native dialog");
            check(row(fillet.id)->foreground(0).color()==QColor(210,75,65),"Dependent preview is not red");return dialog;
        };
        open_confirmation()->reject();flush();
        check(row(source.id)&&row(fillet.id)->foreground(0)==original_color,"Cancel did not restore tree");
        open_confirmation(true)->reject();flush();
        check(row(source.id)&&row(fillet.id)->foreground(0)==original_color,"View Delete Cancel did not restore tree");
        auto* dialog=open_confirmation(true);
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
        const QPointF point(25,25),global=view->mapToGlobal(point.toPoint());
        QMouseEvent middle(QEvent::MouseButtonRelease,point,global,Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&middle);flush();
        check(window.findChild<QDialog*>("deleteHistoryDialog"),"Short middle click committed deletion");
        QMouseEvent double_middle(QEvent::MouseButtonDblClick,point,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&double_middle);flush();
        check(!window.findChild<QDialog*>("deleteHistoryDialog")&&!row(source.id),"Middle double-click over View did not confirm deletion");
        std::cout<<"History deletion GUI: Tree/View menu parity (Feature, Fillet, Point, Axis, Plane, Curve3D), exact target, preview, Cancel, OK, save, Undo and Redo passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
