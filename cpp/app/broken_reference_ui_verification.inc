Q_NEVER_INLINE static int verify_broken_reference_ui(QApplication& application,
        AssemblyWorkspaceWindow&, const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        for(bool assembly_mode:{false,true}) {
            AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));
            const auto run=[&](const char* command,commands::Json arguments=commands::Json::object()) {
                const auto result=window.execute_console_command(QString::fromStdString(
                    commands::Json{{"command",command},{"arguments",arguments}}.dump()));
                if(!result.ok)throw std::runtime_error(result.code+": "+result.message);
                flush();return result.data;
            };
            auto sketch=test::broken_reference_fixture();
            auto owner=document::PartDocument::create_sketch_container();
            owner.feature.sketch_id=sketch.id;sketch.owner_container_id=owner.id;
            const auto file=directory/(assembly_mode?"broken-reference.asmz":"broken-reference.prtz");
            if(assembly_mode) {
                auto document=assembly::AssemblyDocument::create_default();
                document.insert_sketch(sketch,owner);document.resolve_constructions();document.save(file);
            } else {
                auto document=document::PartDocument::create_default();
                document.history={owner};document.sketches={sketch};
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Broken references"));
                graph.insert({document::PartHistoryKind::Feature,owner.id});document.set_body_history(graph);
                document.resolve_constructions();document.save(file,{});
            }
            window.showMaximized();flush();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"Broken-reference fixture did not open");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
            check(tree&&view,"Broken-reference View or Tree missing");
            const auto row=[&](const std::string& id,const QString& role)->QTreeWidgetItem* {
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3).toString()==role)return *it;
                throw std::runtime_error("Broken-reference Tree item missing: "+id);
            };
            window.show_tree_item_properties(row(assembly_mode?owner.id:sketch.id,
                assembly_mode?"assembly-sketch-container":"part-sketch"));flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Sketch edit button missing");open->click();flush();
            QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();
            view->fit_all();flush();
            check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Broken Sketch did not enter editing");
            const auto entities=[&]{return run("sketch.entities",{{"sketch",sketch.id}}).at("items");};
            const auto exists=[&](const std::string& id){const auto items=entities();return std::ranges::any_of(items,[&](const auto& value){return value.at("id")==id;});};
            const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button=Qt::LeftButton) {
                for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,point,widget->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:button,
                        type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(widget,&event);flush();
                }
            };
            const auto key=[&](int code){QKeyEvent event(QEvent::KeyPress,code,Qt::NoModifier);QApplication::sendEvent(&window,&event);flush();};
            for(unsigned index=0;index<sketch.external_references.size();++index) {
                std::cout<<"Broken-reference deletion "<<assembly_mode<<":"<<index<<std::endl;
                const auto& reference=sketch.external_references[index];
                const auto before=entities();
                if(index<2) {
                    std::optional<QPointF> hit;
                    for(int y=8;y<view->height()-8&&!hit;y+=2)for(int x=8;x<view->width()-8&&!hit;x+=2) {
                        const auto candidates=view->selection_candidates_at({double(x),double(y)});
                        if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::SketchExternalReference&&
                            candidates.front().semantic_key.ends_with(reference.id+":broken"))hit=QPointF(x,y);
                    }
                    check(hit.has_value(),"Red reference cannot be offered by the actual picker");
                    click(view,*hit);
                    check(view->confirmed_candidate()&&view->confirmed_candidate()->semantic_key.ends_with(":broken"),"Red reference click did not confirm its wire");
                    if(index==1) {
                        // Exercise the actual nested context-menu event loop.
                        QTimer::singleShot(50,[&] {
                            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
                            if(menu&&!menu->actions().isEmpty()) {
                                const auto point=menu->actionGeometry(menu->actions().front()).center();
                                click(menu,point);
                            }
                        });
                        click(view,*hit,Qt::RightButton);
                    } else key(Qt::Key_Delete);
                } else {
                    auto* item=row(reference.id,"sketch-external-reference");
                    for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
                    tree->scrollToItem(item);flush();click(tree->viewport(),tree->visualItemRect(item).center());
                    if(index!=4)check(view->confirmed_candidate()&&view->confirmed_candidate()->semantic_key.ends_with(":broken"),"Tree selection lost the red reference presentation key");
                    key(Qt::Key_Delete);
                }
                check(!exists(reference.id),"Broken reference survived Delete");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(entities()==before,"Undo did not restore the exact broken reference state");
                window.findChild<QAction*>("redoAction")->trigger();flush();
                check(!exists(reference.id),"Redo did not remove the broken reference");
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto stored=assembly_mode?assembly::AssemblyDocument::load(file).sketches:document::PartDocument::load(file).sketches;
            const auto found=std::ranges::find(stored,sketch.id,&sketcher::Sketch::id);
            check(found!=stored.end()&&found->external_references.empty(),"Removed references returned after native save/reopen");
            check(found->segments==sketch.segments&&found->points==sketch.points,"Removing references changed unrelated native geometry");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            if(auto* dialog=window.findChild<QWidget*>("featurePropertiesDialog"))
                if(auto* buttons=dialog->findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Cancel)->click();
            flush();
        }
        std::cout<<"Broken-reference GUI: Part/Assembly, red wire click/Delete, context menu, Tree deletion including hidden AxisPoint, exact Undo/Redo and save/reopen passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
