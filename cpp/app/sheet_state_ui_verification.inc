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
            {
                auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
                check(view,"Missing sheet View for Twisted Sheet creation.");
                window.findChild<QAction*>("twistedSheetAction")->trigger();flush();
                app::PrimitivePropertiesDialog* twist{};
                for(auto* item:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::PrimitivePropertiesDialog*>(item);candidate&&candidate->isVisible())twist=candidate;
                check(twist&&view->transient_edges().empty(),"Twisted Sheet appears before the first entity.");
                auto* width=twist->findChild<QDoubleSpinBox*>("twistedSheetWidth");check(width,"Missing twist width.");
                width->setValue(35);flush();
                check(view->transient_edges().empty(),"Editing a number displayed an unplaced Twisted Sheet.");
                auto geometry=calculated.back().mesh.original_references;
                twist->set_sheet_reference_geometry(geometry);
                const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e) {
                    document::ConstructionReference reference{e.reference.instance_path,e.reference.owner_id,e.reference.semantic_key};
                    return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&twist->sheet_reference_allowed(0,reference);
                });
                check(edge!=geometry.edges.end(),"No eligible profile edge for Twisted Sheet GUI test.");
                check(twist->set_reference(0,{edge->reference.instance_path,edge->reference.owner_id,edge->reference.semantic_key},"Profile edge"),"Twisted Sheet rejected its first edge.");flush();
                check(!view->transient_edges().empty(),"Twisted Sheet did not appear after the first entity.");
                const auto start=std::ranges::find_if(view->transient_edges(),[](const auto& e){return e.reference.semantic_key=="preview:twist:start";});
                const auto axis=std::ranges::find_if(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==twist->pending_value().feature_id&&a.reference.semantic_key=="axis";});
                check(start!=view->transient_edges().end()&&axis!=view->mesh().axes.end(),"Twist preview has no operation axis.");
                const auto a=start->points[0],b=start->points[2];
                check(std::hypot(std::hypot(axis->point.x-(a.x+b.x)/2,axis->point.y-(a.y+b.y)/2),axis->point.z-(a.z+b.z)/2)<1e-7,
                    "Twist axis misses the centre of its starting section.");
                window.grab().save(QString::fromStdString((directory/(context?"twist-attached-context.png":"twist-attached-profile.png")).string()));
                twist->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                check(view->transient_edges().empty(),"Cancel retained the Twisted Sheet preview.");
            }
            auto* sheet_toolbar=window.findChild<QToolBar*>("toolsToolbar");
            const auto separated=[&](const char* first,const char* last) {
                bool seen=false,separator=false;
                if(sheet_toolbar)for(const auto* action:sheet_toolbar->actions()) {
                    if(action->objectName()==first)seen=true;
                    else if(seen&&action->isSeparator())separator=true;
                    else if(action->objectName()==last)return seen&&separator;
                }
                return false;
            };
            check(separated("sheetMetalPropertiesAction","sheetFromBodyAction")&&
                separated("sheetTransitionAction","sheetCutAction")&&separated("sheetCutAction","unbendAction")&&
                separated("bendBackAction","sheetDxfAction")&&separated("sheetDxfAction","insertSymbolAction"),
                "Sheet Metal toolbar does not separate Properties, Revolved Sheet, Sheet Cut and Bend Back.");
            if(!context) {
                QImage icons(720,135,QImage::Format_ARGB32);icons.fill(window.palette().color(QPalette::Window));
                QPainter painter(&icons);painter.setPen(window.palette().color(QPalette::WindowText));
                int column=0;
                for(const auto* name:{"flatAction","bendAction","sheetRevolutionAction","sheetCutAction","unbendAction","bendBackAction"}) {
                    const auto* action=window.findChild<QAction*>(name);check(action&&!action->icon().isNull(),"Sheet command icon is missing.");
                    painter.drawPixmap(column*120+36,5,action->icon().pixmap(48,48));
                    painter.drawPixmap(column*120+48,65,action->icon().pixmap(24,24));
                    painter.drawText(QRect(column*120,100,120,28),Qt::AlignCenter,action->text());++column;
                }
                painter.end();icons.save(QString::fromStdString((directory/"sheet-command-icons.png").string()));
                auto* piping=window.findChild<QAction*>("applicationModeAction4");
                check(piping&&!piping->isVisible(),"Retired Piping mode is still visible.");
            }
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
            dialog()->findChild<QCheckBox*>("sheetStateIndividual")->setChecked(true);flush();
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
            check(std::ranges::any_of(view->mesh().axes,[&](const auto& axis){return axis.reference.instance_path==active_path&&axis.reference.semantic_key.starts_with("sheet-bend-line:from:");}),"View did not receive the active bend line.");
            window.grab().save(QString::fromStdString((fixture/"sheet-state-bend-line.png").string()));
            auto* selection_action=window.findChild<QAction*>("viewSelectionAction");
            check(selection_action,"Missing ordinary View Selection action.");
            check(selection_action->isChecked(),"Unbend did not restore ordinary View Selection.");
            view->fit_all();flush();
            QTreeWidgetItem* edited{};
            for(QTreeWidgetItemIterator item(tree);*item;++item)if((*item)->data(0,Qt::UserRole+3)=="part-container"&&
                tree_path(*item)==active_path&&(*item)->text(0).contains(unfold_action->text())) {edited=*item;break;}
            check(edited,"Unbend has no editable history row.");
            const auto state_id=edited->data(0,Qt::UserRole).toString().toStdString();
            check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face) {
                    return face.instance_path==active_path&&face.owner_id==state_id&&
                        face.display_owner_id==feature.id;
                }),"Unbend View geometry lost its authored display owner.");
            bool authored_offered=false;std::optional<QPointF> authored_hit;
            for(int y=5;y<view->height();y+=7)for(int x=5;x<view->width();x+=7)
                for(const auto& candidate:view->selection_candidates_at({double(x),double(y)}))
                    if(candidate.kind==viewer::CandidateKind::Container&&candidate.instance_path==active_path) {
                        check(candidate.owner_id!=state_id,"Unbend is selectable as an ordinary View Container.");
                        authored_offered|=candidate.owner_id==feature.id;
                        if(candidate.owner_id==feature.id&&!authored_hit)authored_hit=QPointF(x,y);
                    }
            check(authored_offered,"The authored sheet feature is not selectable through Unbend geometry.");
            check(authored_hit.has_value(),"Cannot locate the unfolded authored feature for double-click.");
            QMouseEvent unfolded_double_click(QEvent::MouseButtonDblClick,*authored_hit,
                *authored_hit,QPointF(view->mapToGlobal(authored_hit->toPoint())),
                Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&unfolded_double_click);flush();
            check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& dimension) {
                    return dimension.reference.owner_id==feature.id&&
                        dimension.reference.semantic_key.starts_with("parameter:");
                }),"Double-click displayed formed dimensions over an unfolded sheet feature.");
            window.show_tree_item_properties(edited);flush();
            check(dialog(),"Unbend editing did not reopen the shared dialog.");
            check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.instance_path==active_path&&face.owner_id==feature.id;}),
                "Unbend editing did not display the real folded input.");
            dialog()->reject();flush();
            back_action->trigger();flush();check(dialog(),"Bend Back did not open.");
            dialog()->findChild<QCheckBox*>("sheetStateIndividual")->setChecked(true);flush();
            const auto unfolded_hit=hit();check(unfolded_hit.has_value(),"Derived Unbend geometry cannot select its source for Bend Back.");
            click(*unfolded_hit,Qt::LeftButton);check(dialog()->selected()==std::vector<std::string>{feature.id},"Derived source ancestry was lost in View selection.");
            window.grab().save(QString::fromStdString((fixture/"sheet-state-properties.png").string()));
            QMouseEvent middle(QEvent::MouseButtonDblClick,*unfolded_hit,QPointF(view->mapToGlobal(unfolded_hit->toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(view,&middle);flush();check(!dialog(),"Bend Back middle-button double-click did not commit.");
            check(std::ranges::none_of(view->mesh().axes,[&](const auto& axis){return axis.reference.instance_path==active_path&&axis.reference.semantic_key.starts_with("sheet-bend-line:from:");}),"View retained a flat bend line after Bend Back.");
            check(window.active_occurrence_path_for_test()==active_path,"Sheet state changed the active occurrence.");
            if(context)check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.instance_path==passive_path;}),"Sheet state hid the passive Assembly context.");
            // Fixture teardown must not enter the unrelated unsaved-document prompt.
            window.hide();flush();
        }
        {
            auto part=document::PartDocument::load("cpp/tests/fixtures/sheet/tilted-cone-with-bends.prtz");
            auto unbend=document::PartDocument::create_sketch_container();unbend.feature_kind=document::FeatureKind::Unbend;unbend.name="Rozvinout";
            part.insert_history_entry(document::PartHistoryKind::Feature,unbend.id);part.history.push_back(unbend);
            kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
            check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Cannot calculate the tilted-cone GUI fixture.");
            const auto rotation=std::ranges::find_if(part.history,[](const auto& f){return f.revolution.sheet_metal;});
            check(rotation!=part.history.end(),"Tilted cone has no source feature.");
            const auto fixture=directory/"sheet-state-cone";std::filesystem::create_directories(fixture);
            const auto path=fixture/"tilted-cone.prtz";part.save(path,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(fixture.string()));window.resize(1200,850);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open the tilted-cone GUI fixture.");flush();
            check(activate_test_body(application,window,part.body_history.bodies().front().scope.id),"Cannot activate the tilted-cone body.");flush();
            window.findChild<QAction*>("applicationModeAction2")->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"Missing tilted-cone View.");
            view->fit_all();flush();
            const auto rotation_visible=[&] {return std::ranges::any_of(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==rotation->id&&a.reference.semantic_key=="axis:primary";});};
            check(!rotation_visible(),"Unfolded cone displays its rotation axis.");
            check(std::ranges::count_if(view->mesh().axes,[](const auto& a){return a.reference.semantic_key.starts_with("sheet-bend-line:from:");})==3,"Unfolded model is missing one of its three bend lines.");
            window.grab().save(QString::fromStdString((fixture/"tilted-cone-unfolded.png").string()));
            window.findChild<QAction*>("bendBackAction")->trigger();flush();
            app::SheetStateDialog* dialog{};
            for(auto* item:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::SheetStateDialog*>(item);candidate&&candidate->isVisible())dialog=candidate;
            check(dialog&&!dialog->findChild<QCheckBox*>("sheetStateIndividual")->isChecked(),"Bend Back did not default to processing all regions.");
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(rotation_visible(),"Bend Back did not restore the cone rotation axis in View.");
            view->fit_all();flush();window.grab().save(QString::fromStdString((fixture/"tilted-cone-folded.png").string()));
            window.hide();flush();
        }
        std::cout<<"Sheet state GUI: Part and repeated Assembly occurrences, common picking/filter, toggle, MMB, Cancel and OK passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Sheet state GUI: "<<error.what()<<'\n';return 1;}
}
