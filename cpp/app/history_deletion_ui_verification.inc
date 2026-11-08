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
        // Delete the exact risky state reported by users: the Fillet is
        // selected in the View and its parameter dimensions are being
        // inspected when the history transaction releases its calculated
        // topology. The transaction must retire those presentation handles,
        // remain undoable and leave ordinary selection usable afterwards.
        view->confirm_container(fillet.id);
        window.show_parameter_dimensions(fillet.id);flush();
        window.delete_part_object_for_test(fillet.id,
            QStringLiteral("container"));flush();
        auto* selected_fillet_confirmation=
            window.findChild<QDialog*>("deleteHistoryDialog");
        check(selected_fillet_confirmation&&
                selected_fillet_confirmation->isVisible(),
            "Selected Fillet deletion confirmation missing");
        selected_fillet_confirmation->findChild<QDialogButtonBox*>()->button(
            QDialogButtonBox::Ok)->click();flush();
        check(!row(fillet.id)&&!view->confirmed_candidate()&&
                view->confirmed_component_wire().empty()&&
                !window.finish_parameter_dimensions(),
            "Deleting an inspected Fillet retained stale View topology or dimensions");
        run("undo");
        check(row(fillet.id),"Selected Fillet deletion Undo failed");
        view->confirm_container(fillet.id);
        check(view->confirmed_candidate()&&
                view->confirmed_candidate()->owner_id==fillet.id,
            "Fillet could not be selected after deletion Undo");
        view->clear_selection();tree->clearSelection();flush();
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

        // Reproduce the user case: Body 2 is placed from Body 1, Body 1 is
        // removed, and Body 2's first Sketch must remain enterable. The
        // removed placement reference stays reported for repair, but must not
        // poison the surviving Body's rollback boundary.
        auto linked=document::PartDocument::create_default();
        auto first_feature=test::rectangular_feature(linked,{20,20,10});
        auto linked_sketch_container=document::PartDocument::create_sketch_container();
        auto linked_sketch=sketcher::Sketch::create_default();
        linked_sketch.owner_container_id=linked_sketch_container.id;
        linked_sketch_container.feature.sketch_id=linked_sketch.id;
        auto second_feature=test::rectangular_feature(linked,{5,5,5});
        linked.history={first_feature,linked_sketch_container,second_feature};
        linked.sketches.push_back(linked_sketch);
        document::BodyHistoryGraph linked_graph;
        const auto first_body=linked_graph.create_body("First");
        linked_graph.insert({document::PartHistoryKind::Feature,first_feature.id});
        const auto second_body=linked_graph.create_body("Second");
        linked_graph.insert({document::PartHistoryKind::Feature,linked_sketch_container.id});
        linked_graph.insert({document::PartHistoryKind::Feature,second_feature.id});
        linked.set_body_history(linked_graph);
        auto linked_calculated=workspace::calculate_part_with_resolved_references(kernel,linked);
        const auto linked_face=std::ranges::find_if(
            linked_calculated.back().mesh.original_references.triangle_references,
            [&](const auto& value){return value.owner_id==first_feature.id&&value.surface&&
                value.surface->kind==kernel::SurfaceGeometry::Kind::Plane;});
        check(linked_face!=linked_calculated.back().mesh.original_references.triangle_references.end(),
            "Linked-Body GUI fixture has no source face");
        linked_graph=linked.body_history;
        auto positioned=*linked_graph.find(second_body);
        document::ConstructionReference placement;
        placement.owner_id=linked_face->owner_id;placement.semantic_key=linked_face->semantic_key;
        placement.orientation_role="front";placement.orientation_drives_rotation=true;
        positioned.scope.placement.references={placement};
        linked_graph.update_body(positioned);linked.set_body_history(linked_graph);
        linked.resolve_constructions(linked_calculated.back().mesh.original_references);
        linked_calculated=workspace::calculate_part_with_resolved_references(kernel,linked,&linked_calculated);
        const auto linked_path=directory/"history-deletion-linked-bodies.prtz";
        linked.save(linked_path,linked_calculated);
        check(window.open_document_path(QString::fromStdString(linked_path.string())),
            "Linked-Body deletion fixture did not open");flush();
        // Structural Body deletion is deliberately available only at the Part
        // boundary.  Preserve that contract, remove Body 1 there, and activate
        // the surviving Body 2 afterwards to exercise its Sketch editor.
        run("body.activate");
        tree=window.findChild<QTreeWidget*>("documentTree");
        const auto linked_row=[&](const std::string& id,const char* role)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                   (*it)->data(0,Qt::UserRole+3).toString()==role)return *it;
            return nullptr;
        };
        auto* first_body_row=linked_row(first_body,"part-body");
        check(first_body_row&&linked_row(second_body,"part-body")&&linked_row(linked_sketch.id,"part-sketch"),
            "Linked-Body deletion tree is incomplete");
        tree->setCurrentItem(first_body_row);tree->scrollToItem(first_body_row);flush();
        bool linked_delete_invoked=false;
        QTimer::singleShot(0,&window,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)return;
            auto* action=menu->findChild<QAction*>("deleteBodyAction");if(!action){menu->close();return;}
            linked_delete_invoked=true;
            menu->close();
        });
        QMetaObject::invokeMethod(tree,"customContextMenuRequested",Qt::DirectConnection,
            Q_ARG(QPoint,tree->visualItemRect(first_body_row).center()));flush();
        check(linked_delete_invoked,"Linked Body delete action missing at the Part boundary");
        window.delete_part_object_for_test(first_body,QStringLiteral("part-body"));flush();
        auto* linked_confirmation=window.findChild<QDialog*>("deleteHistoryDialog");
        check(linked_confirmation&&linked_confirmation->isVisible(),"Linked Body deletion confirmation missing");
        linked_confirmation->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        check(!linked_row(first_body,"part-body")&&linked_row(second_body,"part-body")&&
            linked_row(linked_sketch.id,"part-sketch"),"Deleting Body 1 removed Body 2 or its Sketch");
        run(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",second_body}}}}.dump()));
        window.show_tree_item_properties(linked_row(linked_sketch.id,"part-sketch"));flush();
        SketchPropertiesDialog* linked_properties{};
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
            if(auto* properties=dynamic_cast<SketchPropertiesDialog*>(candidate))linked_properties=properties;
        check(linked_properties,"Body 2 Sketch Properties did not open after deleting Body 1");
        linked_properties->findChild<QPushButton*>("sketchOpenButton")->click();flush();
        check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),
            "Body 2 Sketch cannot enter Sketcher after deleting Body 1");
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
            if(auto* properties=dynamic_cast<SketchPropertiesDialog*>(candidate))properties->reject();
        flush();run("save");
        const auto linked_saved=document::PartDocument::load(linked_path);
        check(!linked_saved.body_history.find(first_body)&&linked_saved.body_history.find(second_body)&&
            linked_saved.body_history.find(second_body)->scope.placement.references.empty()&&
            linked_saved.removed_reference_states.contains(second_body),
            "Linked-Body deletion did not persist its repairable surviving state");
        // Explicit Body activation is the newest document-history step. Undo
        // it first, then undo the preceding structural deletion.
        run("undo");check(!linked_row(first_body,"part-body")&&linked_row(second_body,"part-body"),
            "Undoing Body activation changed the deleted/surviving Body set");
        run("undo");check(linked_row(first_body,"part-body"),"Linked-Body deletion Undo lost Body 1");
        run("redo");check(!linked_row(first_body,"part-body")&&linked_row(second_body,"part-body"),
            "Linked-Body deletion Redo did not restore the surviving state");
        run("redo");check(!linked_row(first_body,"part-body")&&linked_row(linked_sketch.id,"part-sketch"),
            "Linked-Body deletion Redo lost the surviving Sketch");
        window.show_tree_item_properties(linked_row(linked_sketch.id,"part-sketch"));flush();
        check(window.findChild<QPushButton*>("sketchOpenButton"),
            "Surviving Sketch Properties cannot reopen after deletion Redo");
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())candidate->reject();flush();

        // Exact Properties contract for an ordinary retained chain:
        // Container 1 is deleted, Container 2 becomes an editable free frame,
        // and Container 3 remains attached to Container 2 throughout repair.
        auto chain=document::PartDocument::create_default();
        auto chain_first=test::rectangular_feature(chain,{20,20,10});
        auto chain_replacement=test::rectangular_feature(chain,{12,12,8});
        chain_replacement.placement.x=50;
        auto chain_second=test::rectangular_feature(chain,{8,8,6});
        auto chain_third=test::rectangular_feature(chain,{4,4,4});
        chain.history={chain_first,chain_replacement,chain_second,chain_third};
        document::BodyHistoryGraph chain_graph;
        const auto chain_body=chain_graph.create_body("Three retained containers");
        for(const auto& feature:chain.history)
            chain_graph.insert({document::PartHistoryKind::Feature,feature.id});
        chain.set_body_history(chain_graph);
        auto chain_calculated=workspace::calculate_part_with_resolved_references(kernel,chain);
        const auto chain_face=[&](const std::string& owner) {
            const auto& references=chain_calculated.back().mesh.original_references;
            const kernel::FaceReference* found{};
            for(const auto& reference:references.triangle_references)
                if(reference.owner_id==owner&&reference.surface&&
                        reference.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                        std::abs(reference.surface->axis.x)>.9&&
                        (!found||reference.surface->origin.x>
                            found->surface->origin.x))
                    found=&reference;
            check(found,
                "Retained-chain GUI fixture has no X-normal planar face");
            document::ConstructionReference support;
            support.owner_id=found->owner_id;support.semantic_key=found->semantic_key;
            support.orientation_role="front";support.orientation_drives_rotation=true;
            return support;
        };
        chain.find_container(chain_second.id)->placement.references={chain_face(chain_first.id)};
        chain.resolve_constructions(chain_calculated.back().mesh.original_references);
        chain_calculated=workspace::calculate_part_with_resolved_references(
            kernel,chain,&chain_calculated);
        chain.find_container(chain_third.id)->placement.references={chain_face(chain_second.id)};
        chain.resolve_constructions(chain_calculated.back().mesh.original_references);
        chain_calculated=workspace::calculate_part_with_resolved_references(
            kernel,chain,&chain_calculated);
        const auto chain_third_before=chain.find_container(chain_third.id)->placement;
        const auto chain_path=directory/"history-deletion-three-containers-ui.prtz";
        chain.save(chain_path,chain_calculated);
        AssemblyWorkspaceWindow chain_window(
            QString::fromStdString(document::path_to_utf8(directory)));
        chain_window.showMaximized();
        check(chain_window.open_document_path(QString::fromStdString(chain_path.string())),
            "Retained-chain GUI fixture did not open");flush();
        const auto chain_run=[&](const QString& command) {
            auto result=chain_window.execute_console_command(command);
            check(result.ok,result.message.c_str());flush();
        };
        chain_run(QString::fromStdString(commands::Json{{"command","body.activate"},
            {"arguments",{{"body",chain_body}}}}.dump()));
        tree=chain_window.findChild<QTreeWidget*>("documentTree");
        const auto chain_row=[&](const std::string& id)->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&
                   (*it)->data(0,Qt::UserRole+3).toString()=="part-container")return *it;
            return nullptr;
        };
        chain_window.delete_part_object_for_test(chain_first.id,
            QStringLiteral("part-container"));flush();
        auto* chain_confirmation=chain_window.findChild<QDialog*>("deleteHistoryDialog");
        check(chain_confirmation&&chain_confirmation->isVisible(),
            "Retained-chain deletion confirmation missing");
        chain_confirmation->findChild<QDialogButtonBox*>()->button(
            QDialogButtonBox::Ok)->click();flush();
        check(!chain_row(chain_first.id)&&chain_row(chain_second.id)&&
                chain_row(chain_third.id)&&
                chain_row(chain_second.id)->foreground(0).color()==QColor(210,75,65)&&
                chain_row(chain_third.id)->foreground(0).color()!=QColor(210,75,65),
            "Deleting Container 1 did not retain the exact 2 -> 3 boundary");
        auto* chain_view=dynamic_cast<viewer::MeshView*>(
            chain_window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(chain_view,"Retained-chain View is missing");
        chain_view->confirm_container(chain_second.id);
        check(chain_view->confirmed_candidate()&&
                chain_view->confirmed_candidate()->owner_id==chain_second.id,
            "Retained Container 2 cannot be confirmed in the View");
        const QPointF inspection_point(chain_view->rect().center());
        QMouseEvent inspect_retained(QEvent::MouseButtonDblClick,
            inspection_point,QPointF(chain_view->mapToGlobal(
                inspection_point.toPoint())),Qt::LeftButton,Qt::LeftButton,
            Qt::NoModifier);
        QApplication::sendEvent(chain_view,&inspect_retained);flush();
        const bool view_cleared_red=
            chain_row(chain_second.id)->foreground(0).color()!=QColor(210,75,65);
        const bool view_inspection_started=chain_window.finish_parameter_dimensions();
        flush();
        check(view_inspection_started,
            "View double-click did not inspect retained Container 2");
        check(view_cleared_red,
            "View double-click left retained Container 2 red");
        check(chain_row(chain_second.id)->foreground(0).color()==QColor(210,75,65),
            "Unchanged View inspection acknowledged a deleted reference");
        const auto visible_chain_properties=[&] {
            PrimitivePropertiesDialog* properties{};
            for(auto* candidate:chain_window.findChildren<QDialog*>())
                if(auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(candidate);
                        dialog&&dialog->isVisible())properties=dialog;
            return properties;
        };
        const auto open_chain_properties=[&] {
            chain_window.show_tree_item_properties(chain_row(chain_second.id));flush();
            auto* properties=visible_chain_properties();
            if(!properties)for(auto* candidate:chain_window.findChildren<QMessageBox*>())
                if(candidate->isVisible())std::cerr<<
                    "Retained Properties warning: "<<
                    candidate->text().toStdString()<<'\n';
            check(properties&&properties->isVisible(),
                "Retained Container 2 Properties did not open");
            check(chain_row(chain_second.id)->foreground(0).color()!=QColor(210,75,65),
                "Entering retained Container 2 Properties left it red");
            return properties;
        };
        auto* chain_properties=open_chain_properties();
        auto* sketch_button=chain_properties->findChild<QPushButton*>(
            "primitiveOwnSketchButton");
        if(!sketch_button)sketch_button=chain_properties->findChild<QPushButton*>(
            "featureSketchButton");
        if(!sketch_button)sketch_button=chain_properties->findChild<QPushButton*>(
            "sketchOpenButton");
        check(sketch_button&&sketch_button->isEnabled(),
            "Retained Container 2 cannot enter Sketcher");
        sketch_button->click();flush();
        auto* finish_sketch=chain_window.findChild<QAction*>("finishSketchAction");
        check(finish_sketch&&finish_sketch->isEnabled()&&
                !visible_chain_properties(),
            "Retained Container 2 did not enter its Sketcher");
        finish_sketch->trigger();flush();
        chain_properties=visible_chain_properties();
        check(chain_properties&&chain_properties->pending_value().id==chain_second.id,
            "Retained Container 2 did not return from Sketcher to Properties");
        const double moved_x=chain_properties->pending_value().placement.x+7;
        check(chain_properties->set_inline_parameter_value("placement:x",moved_x),
            "Retained Container 2 placement X is not editable");
        chain_properties->findChild<QDoubleSpinBox*>("extrusionHeight")->setValue(
            chain_properties->pending_value().extrusion.length_forward+1);
        flush();
        check(chain_properties->pending_value().placement.x==moved_x,
            "Retained Container 2 placement edit did not react");
        chain_properties->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(chain_row(chain_second.id)->foreground(0).color()==QColor(210,75,65),
            "Cancel did not restore Container 2's deleted-reference state");
        chain_run("save");
        const auto cancelled_chain=document::PartDocument::load(chain_path);
        check(cancelled_chain.removed_reference_states.contains(chain_second.id),
            "Cancel committed the retained-container repair");
        chain_properties=open_chain_properties();
        auto* default_origin=chain_properties->findChild<QPushButton*>(
            "containerDefaultOriginButton");
        check(default_origin&&default_origin->isEnabled(),
            "Reopened retained Container 2 has no usable Default placement");
        default_origin->click();flush();
        const auto default_placement=chain_properties->pending_value().placement;
        const auto chain_origin=chain.body_history.find(chain_body)->origin().id;
        check(!default_placement.references.empty()&&
                std::ranges::all_of(default_placement.references,
                    [&](const auto& reference) {
                        return reference.owner_id==chain_origin;
                    }),"Default did not attach retained Container 2 to its Body Origin");
        const double moved_height=chain_properties->pending_value().extrusion.length_forward+1;
        chain_properties->findChild<QDoubleSpinBox*>("extrusionHeight")->setValue(moved_height);
        chain_properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(std::ranges::none_of(chain_window.findChildren<QDialog*>(),
                [](auto* candidate) {
                    const auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(candidate);
                    return dialog&&dialog->isVisible();
                }),"Retained Container 2 OK did not finish");
        chain_run("save");
        std::vector<kernel::BodyResult> repaired_chain_geometry;
        const auto repaired_chain=document::PartDocument::load(
            chain_path,&repaired_chain_geometry);
        check(!repaired_chain.removed_reference_states.contains(chain_second.id)&&
                !repaired_chain.removed_reference_states.contains(chain_third.id)&&
                repaired_chain.find_container(chain_second.id)->extrusion.length_forward==moved_height&&
                repaired_chain.find_container(chain_second.id)->placement.reference_valid&&
                !repaired_chain.find_container(chain_second.id)->placement.references.empty()&&
                std::ranges::all_of(
                    repaired_chain.find_container(chain_second.id)->placement.references,
                    [&](const auto& reference) {
                        return reference.owner_id==chain_origin;
                    })&&
                repaired_chain.find_container(chain_third.id)->placement.reference_valid&&
                repaired_chain.find_container(chain_third.id)->placement!=chain_third_before&&
                !repaired_chain_geometry.empty()&&
                repaired_chain_geometry.back().calculation_errors.empty(),
            "Container 2 repair did not persist or Container 3 stopped following it");
        chain_run("undo");
        check(chain_row(chain_second.id)->foreground(0).color()==QColor(210,75,65)&&
                chain_row(chain_third.id)->foreground(0).color()!=QColor(210,75,65),
            "Container 2 repair Undo changed the retained dependency boundary");
        chain_run("redo");
        check(chain_row(chain_second.id)->foreground(0).color()!=QColor(210,75,65)&&
                chain_row(chain_third.id)->foreground(0).color()!=QColor(210,75,65),
            "Container 2 repair Redo did not restore the repaired chain");
        std::cout<<"History deletion GUI: Tree/View menu parity, exact target, preview, Cancel, OK, save, Undo/Redo, linked Body Sketch entry and retained 1 -> 2 -> 3 Properties repair passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
