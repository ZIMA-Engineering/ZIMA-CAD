static void verify_profile_origin_ui(QApplication& application, AssemblyWorkspaceWindow& window,
                                    const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
    };
    window.showMaximized();flush();
    for(const bool feature_mode:{true,false})for(const bool shortcut:{true,false}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_circle(4,7,2));
        auto feature=feature_mode?document::PartDocument::create_feature_container(sketch.id):document::PartDocument::create_sketch_container();
        sketch.owner_container_id=feature.id;feature.feature.sides[0].length=8;
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Origin alignment");
        graph.insert({document::PartHistoryKind::Feature,feature.id});
        part.history={feature};part.sketches={sketch};part.set_body_history(graph);part.resolve_constructions();
        const auto file=directory/(std::string("origin-")+(feature_mode?"feature-":"sketch-")+(shortcut?"button":"tree")+".prtz");
        part.save(file,kernel::OcctKernel{}.evaluate_history(part.kernel_operations()));
        run("open",{{"path",file.string()}});run("body.activate",{{"body",body}});
        auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"Origin test Tree missing");
        const auto open=[&]() -> QDialog* {
            QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
                (*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
            check(row,"Origin test Feature row missing");window.show_tree_item_properties(row);flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dynamic_cast<PlacementReferenceDialog*>(dialog))return dialog;
            throw std::runtime_error("Origin test Properties missing");
        };
        const auto initial=run("documents");auto* dialog=open();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Origin test View missing");
        auto revision=view->base_mesh_revision();
        const auto pick=[&] {
            if(shortcut) {
                auto* button=dialog->findChild<QPushButton*>("containerDefaultOriginButton");check(button,"Default Origin button missing");button->click();flush();
            } else {
                auto* table=dialog->findChild<QTableWidget*>(feature_mode?"primitiveReferenceTable":"sketchReferenceTable");
                check(table&&table->item(0,1),"Origin reference-entry field missing");
                const QPointF field(table->visualItemRect(table->item(0,1)).center());
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,field,QPointF(table->viewport()->mapToGlobal(field.toPoint())),Qt::LeftButton,
                        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(table->viewport(),&event);
                }
                flush();
                QTreeWidgetItem* origin{};
                const auto origin_id=part.body_history.find(body)->origin().id;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==origin_id){origin=*it;break;}
                check(origin,"Owning Body Origin row missing");
                for(auto* parent=origin->parent();parent;parent=parent->parent())tree->expandItem(parent);
                tree->scrollToItem(origin);flush();const QPointF local(tree->visualItemRect(origin).center()),global(tree->viewport()->mapToGlobal(local.toPoint()));
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,local,global,Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(tree->viewport(),&event);
                }
                flush();
            }
        };
        const auto verify=[&] {
            const auto* placement=dynamic_cast<PlacementReferenceDialog*>(dialog);
            auto p=placement->placement_seed();p.references=placement->references_without(99);
            check(document::placement_references_use_whole_origin(p.references),"Origin did not populate a complete frame");
            std::cout<<"Origin candidate "<<(feature_mode?"Feature":"Sketch")<<' '<<(shortcut?"button":"Tree")
                <<" rotation "<<p.rotation_x<<' '<<p.rotation_y<<' '<<p.rotation_z
                <<" reference scene publications "<<view->base_mesh_revision()-revision<<'\n';
            check(std::abs(p.rotation_x)+std::abs(p.rotation_y)+std::abs(p.rotation_z)<1e-7,
                "Whole Origin rotated the profile axes away from the owning Origin");
            check(std::abs(p.x)+std::abs(p.y)+std::abs(p.z)<1e-7,"Whole Origin moved the profile origin");
            // Whole-Origin input publishes only its completed reference scene;
            // all ordered solving and reference initialization still run.
            if(feature_mode)check(view->base_mesh_revision()-revision==1,
                "Whole-Origin Feature entry published redundant scenes");
            // Scene publication is not a kernel calculation counter.
            check(run("documents")==initial,"Origin draft modified the document");
        };
        pick();verify();dialog->reject();flush();check(run("documents")==initial,"Origin Cancel changed document");
        dialog=open();revision=view->base_mesh_revision();pick();verify();dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();run("save");
        const auto saved=document::PartDocument::load(file);const auto* placed=saved.find_container(feature.id);
        check(placed&&document::placement_references_use_whole_origin(placed->placement.references),"Origin OK lost frame references");
        const auto found=std::ranges::find(saved.sketches,sketch.id,&sketcher::Sketch::id);
        check(found!=saved.sketches.end()&&found->plane==sketcher::SketchPlane::XY,"Origin OK lost local XY");
        check(std::abs(found->resolved_x_axis.x-1)+std::abs(found->resolved_x_axis.y)+std::abs(found->resolved_x_axis.z)<1e-7&&
              std::abs(found->resolved_y_axis.y-1)+std::abs(found->resolved_y_axis.x)+std::abs(found->resolved_y_axis.z)<1e-7,
              "Saved Sketch axes differ from the owning Origin");
        run("undo");dialog=open();
        check(dynamic_cast<PlacementReferenceDialog*>(dialog)->references_without(99).empty(),"Origin Undo retained references");dialog->reject();flush();
        run("redo");run("save");run("open",{{"path",file.string()}});dialog=open();
        check(document::placement_references_use_whole_origin(dynamic_cast<PlacementReferenceDialog*>(dialog)->references_without(99)),"Origin reopen lost references");
        dialog->reject();flush();
    }
    std::cout<<"Whole Origin GUI: Feature/Sketch, shortcut/Tree, XY axes, preview-only entry, Cancel, OK, Save, Undo/Redo and reopen passed\n";
}
