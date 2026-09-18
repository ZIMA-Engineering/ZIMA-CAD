int verify_sheet_state_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(bool context:{false,true}) {
            auto part=document::PartDocument::create_default();
            auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Bend;
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;feature.bend.sketch_id=sketch.id;
            static_cast<void>(sketch.add_segment(0,0,40,0));feature.bend.radius=8;feature.bend.radius_follows_thickness=false;
            feature.bend.thickness_override=true;feature.bend.thickness=2;part.history={feature};part.sketches={sketch};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Sheet");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
            const auto fixture=directory/(context?"sheet-state-context":"sheet-state-part");std::filesystem::create_directories(fixture);
            const auto path=fixture/"source.prtz";part.save(path,calculated);
            auto assembly=assembly::AssemblyDocument::create_default();std::string active_path,passive_path;
            auto open_path=path;
            if(context) {
                workspace::Workspace setup;setup.add_part(part,calculated,path);
                const auto id=assembly.document_id;const auto assembly_path=fixture/"context.asmz";setup.add_assembly(assembly,assembly_path);
                const auto first=setup.insert_open_part(id,part.document_id,"Active"),second=setup.insert_open_part(id,part.document_id,"Passive");
                assembly=setup.open_assembly(id)->session.document();assembly.find_occurrence(second)->placement={85,0,0,0,0,0};assembly.save(assembly_path);
                active_path=assembly::InstancePath{{first}}.encoded();passive_path=assembly::InstancePath{{second}}.encoded();open_path=assembly_path;
            }
            app::AssemblyWorkspaceWindow window(QString::fromStdString(fixture.string()));window.resize(1200,850);window.show();
            check(window.open_document_path(QString::fromStdString(open_path.string())),"Cannot open the sheet state GUI fixture.");flush();
            if(context)check(window.activate_occurrence_for_test(active_path),"Cannot activate sheet Part in Assembly.");flush();
            check(activate_test_body(application,window,body),"Cannot activate the sheet state body.");flush();
            auto* sheet_mode=window.findChild<QAction*>("applicationModeAction2");
            check(sheet_mode,"Missing Sheet Metal application mode.");sheet_mode->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Missing sheet state workspace.");
            const auto tree_path=[](QTreeWidgetItem* row) {
                std::string path;
                for(;row&&path.empty();row=row->parent())path=row->data(0,Qt::UserRole+1).toString().toStdString();
                return path;
            };
            auto* unfold_action=window.findChild<QAction*>("unbendAction");auto* back_action=window.findChild<QAction*>("bendBackAction");
            check(unfold_action&&back_action,"Missing sheet state toolbar actions.");
            const auto dialog=[&]() -> app::SheetStateDialog* {
                for(auto* item:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::SheetStateDialog*>(item);d&&d->isVisible())return d;
                return nullptr;
            };
            const auto click=[&](QPointF point,Qt::MouseButton button) {
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,point,point,QPointF(view->mapToGlobal(point.toPoint())),
                        type==QEvent::MouseMove?Qt::NoButton:button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&event);flush();
                }
            };
            const auto hit=[&]() -> std::optional<QPointF> {
                for(int y=5;y<view->height();y+=7)for(int x=5;x<view->width();x+=7)
                    if(const auto candidates=view->selection_candidates_at({double(x),double(y)});!candidates.empty()) {
                        for(const auto& candidate:candidates)check(candidate.instance_path==active_path,"Sheet state offered a passive Assembly occurrence.");
                        return QPointF(x,y);
                    }
                return {};
            };
            unfold_action->trigger();flush();check(dialog(),"Unbend did not open its internal dialog.");
            check(dialog()->parentWidget()==&window&&dialog()->windowFlags().testFlag(Qt::SubWindow),"Unbend opened outside the internal properties contract.");
            check(!dialog()->findChild<QPushButton*>("containerOriginSelectionButton"),"Sheet state incorrectly requested an Origin placement reference.");
            dialog()->findChild<QCheckBox*>("sheetStateAll")->setChecked(false);flush();
            view->set_selection_filter(viewer::SelectionFilter::Axes);check(!hit(),"Unbend bypassed the axes-only user filter.");
            view->set_selection_filter(viewer::SelectionFilter::All);view->fit_all();flush();
            QTreeWidgetItem* source_row{};
            for(QTreeWidgetItemIterator item(tree);*item;++item)if((*item)->data(0,Qt::UserRole+3)=="part-container"&&
                (*item)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&tree_path(*item)==active_path) {source_row=*item;break;}
            check(source_row,"Sheet source has no editable Tree row.");
            for(bool selected:{true,false}) {
                tree->clearSelection();tree->setCurrentItem(source_row);source_row->setSelected(true);flush();
                check(dialog()->selected().empty()!=selected,"Tree selection did not toggle the exact active source region.");
            }
            const auto offered=hit();check(offered.has_value(),"Unbend has no common View candidate.");
            click(*offered,Qt::LeftButton);check(dialog()->selected()==std::vector<std::string>{feature.id},"View click did not select the source region.");
            click(*offered,Qt::LeftButton);check(dialog()->selected().empty(),"Second View click did not remove the source region.");
            click(*offered,Qt::LeftButton);click(*offered,Qt::MiddleButton);check(!dialog()->selecting()&&!dialog()->selected().empty(),"Short MMB committed or erased the sheet selection.");
            dialog()->reject();flush();check(!dialog(),"Cancel left the sheet state command active.");
            unfold_action->trigger();flush();check(dialog(),"Cannot restart Unbend after Cancel.");
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Unbend OK failed to commit.");
            QTreeWidgetItem* edited{};
            for(QTreeWidgetItemIterator item(tree);*item;++item)if((*item)->data(0,Qt::UserRole+3)=="part-container"&&
                tree_path(*item)==active_path&&(*item)->text(0).contains(unfold_action->text())) {edited=*item;break;}
            check(edited,"Unbend has no editable history row.");window.show_tree_item_properties(edited);flush();
            check(dialog(),"Unbend editing did not reopen the shared dialog.");
            check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.instance_path==active_path&&face.owner_id==feature.id;}),
                "Unbend editing did not display the real folded input.");
            dialog()->reject();flush();
            back_action->trigger();flush();check(dialog(),"Bend Back did not open.");
            dialog()->findChild<QCheckBox*>("sheetStateAll")->setChecked(false);flush();
            const auto unfolded_hit=hit();check(unfolded_hit.has_value(),"Derived Unbend geometry cannot select its source for Bend Back.");
            click(*unfolded_hit,Qt::LeftButton);check(dialog()->selected()==std::vector<std::string>{feature.id},"Derived source ancestry was lost in View selection.");
            window.grab().save(QString::fromStdString((fixture/"sheet-state-properties.png").string()));
            QMouseEvent middle(QEvent::MouseButtonDblClick,*unfolded_hit,QPointF(view->mapToGlobal(unfolded_hit->toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(view,&middle);flush();check(!dialog(),"Bend Back middle-button double-click did not commit.");
            check(window.active_occurrence_path_for_test()==active_path,"Sheet state changed the active occurrence.");
            if(context)check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.instance_path==passive_path;}),"Sheet state hid the passive Assembly context.");
            window.close();flush();
        }
        std::cout<<"Sheet state GUI: Part and repeated Assembly occurrences, common picking/filter, toggle, MMB, Cancel and OK passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Sheet state GUI: "<<error.what()<<'\n';return 1;}
}
