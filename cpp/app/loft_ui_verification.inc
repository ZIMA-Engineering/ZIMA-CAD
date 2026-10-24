int verify_loft_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto click=[&](QWidget* widget,QPoint point,Qt::MouseButton button=Qt::LeftButton){
        QMouseEvent move(QEvent::MouseMove,QPointF(point),QPointF(widget->mapToGlobal(point)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(widget,&move);flush();
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,QPointF(point),QPointF(widget->mapToGlobal(point)),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();
        }
    };
    try {
        window.resize(1400,1050);window.show();flush();
        auto part=document::PartDocument::create_default();part.history.clear();part.sketches.clear();part.constructions.clear();part.history_order.clear();
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Loft GUI Body");
        std::vector<std::string> profiles,guides;
        part.set_body_history(graph);part.resolve_constructions();const auto sources=part.serialized().at("sketches");
        const auto path=directory/"loft-ui.prtz";part.save(path);
        check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open Loft GUI fixture");flush();
        const auto activate=[&](bool on){
            commands::Json args=on?commands::Json{{"body",body}}:commands::Json::object();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",args}}.dump())).ok,"Cannot change Loft Body activation");flush();
        };
        activate(true);
        auto* action=window.findChild<QAction*>("loftAction");check(action&&action->isEnabled(),"Loft action missing");
        bool grouped=false;
        for(auto* toolbar:window.findChildren<QToolBar*>()) {
            const auto actions=toolbar->actions();const auto index=actions.indexOf(action);
            if(index>0&&actions[index-1]->objectName()=="helicalSweepAction")grouped=true;
        }
        check(grouped,"Loft is not immediately below Helical Sweep");
        activate(false);check(!action->isEnabled(),"Loft is enabled outside an editable Body");activate(true);
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(tree&&view,"Loft View or Tree missing");
        const auto row=[&](const std::string& id,const char* type="part-container")->QTreeWidgetItem* {
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==id&&(*it)->data(0,Qt::UserRole+3).toString()==type)return *it;
            return nullptr;
        };
        const auto editor=[&]()->LoftDialog* {return dynamic_cast<LoftDialog*>(window.findChild<QWidget*>("loftDialog"));};
        const auto pick_tree=[&](QTreeWidgetItem* item){
            check(item,"Loft input Tree row missing");tree->scrollToItem(item);flush();
            click(tree->viewport(),tree->visualItemRect(item).center());
        };
        const auto field=[&](QTableWidget* table,int row,int column){
            auto* item=table->item(row,column);check(item,"Loft table field missing");table->scrollToItem(item);flush();
            click(table->viewport(),table->visualItemRect(item).center());
        };
        const auto initial=window.execute_console_command("documents").data;
        const auto original_settings=ApplicationSettings::load(QDir::currentPath());
        auto restore_language=qScopeGuard([&]{apply_application_translations(application,original_settings);});
        QTemporaryDir translations;check(translations.isValid(),"Cannot isolate Loft translation settings");
        const std::array<const char*,5> languages{"cs","en","de","fr","ru"};
        const std::array<const char*,5> titles{"Vlastnosti přechodu / Loftu","Loft Properties","Loft-Eigenschaften","Propriétés du lissage / Loft","Свойства перехода / Loft"};
        const std::array<const char*,5> profile_labels{"Skici profilů","Profile sketches","Profilskizzen","Esquisses de profil","Эскизы профилей"};
        for(unsigned language=0;language<languages.size();++language) {
            QSettings settings(translations.filePath("config.ini"),QSettings::IniFormat);
            settings.setValue("Application/Language",languages[language]);
            settings.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").generic_string()));settings.sync();
            apply_application_translations(application,ApplicationSettings::load(translations.path()));
            action->trigger();flush();auto* dialog=editor();check(dialog,"Localized Loft creation did not open");
            check(dialog->windowTitle()==QString::fromUtf8(titles[language]),"Loft title is not translated");
            bool found=false;for(auto* label:dialog->findChildren<QLabel*>())found|=label->text()==QString::fromUtf8(profile_labels[language]);
            check(found,"Loft profile section is not translated");
            check(dialog->parentWidget()==&window&&dialog->windowType()==Qt::SubWindow,"Loft did not reuse internal Properties");
            check(!dialog->buttons()->button(QDialogButtonBox::Apply),"Loft exposes Apply");
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==initial,"Localized Loft Cancel changed the document");
        }
        apply_application_translations(application,original_settings);
        const auto opening_scene=view->base_mesh_revision();
        action->trigger();flush();auto* dialog=editor();check(dialog,"Loft dialog missing");
        check(view->base_mesh_revision()==opening_scene+1,"Loft opening published an unused intermediate scene");
        const auto id=dialog->pending.id;auto* profile_table=dialog->findChild<QTableWidget*>("loftProfiles");
        auto* guide_table=dialog->findChild<QTableWidget*>("loftGuides");
        check(profile_table&&guide_table&&profile_table->columnCount()==5&&guide_table->columnCount()==5,"Loft reference list structure is wrong");
        check(profile_table->mapTo(dialog,QPoint(0,0)).y()<guide_table->mapTo(dialog,QPoint(0,0)).y(),"Loft lists are side by side or reversed");
        dialog->pending.loft.profiles.clear();dialog->pending.loft.guides.clear();dialog->pending.loft.sketches.clear();
        for(unsigned i=0;i<5;++i) {
            auto value=document::create_general_surface_sketch(dialog->pending);
            auto sketch=sketcher::Sketch::from_serialized(value.sketch_serialized);sketch.plane_auto=false;
            if(i<3) {value.sketch_feature->placement.z=5.*i;static_cast<void>(sketch.add_circle(0,0,10));profiles.push_back(value.sketch_feature->id);}
            else {sketch.plane=sketcher::SketchPlane::XZ;static_cast<void>(sketch.add_segment(i==3?10:-10,0,i==3?10:-10,-10));guides.push_back(value.sketch_feature->id);}
            value.sketch_serialized=sketch.serialized();dialog->pending.loft.sketches.push_back(std::move(value));
        }
        dialog->pending.loft.profiles=profiles;dialog->pending.loft.guides=guides;
        document::resolve_loft_sketches(dialog->pending,{});dialog->refresh();dialog->changed();flush();
        const auto base_revision=view->base_mesh_revision();const auto base_count=dialog->property("loftBaseSceneCount").toUInt();
        const auto builds=dialog->property("loftPreviewBuildCount").toUInt();
        const auto preview_edge_count=view->transient_edges().size();
        auto* eye=profile_table->cellWidget(0,4)->findChild<QToolButton*>();check(eye,"Loft inspection eye missing");
        click(eye,eye->rect().center());
        check(dialog->inspected_sources().contains(profiles[0]),"Loft eye missed the owned Sketch");
        check(view->transient_edges().size()==preview_edge_count,"Loft inspection duplicated authored geometry");
        check(dialog->property("loftPreviewBuildCount").toUInt()==builds&&view->base_mesh_revision()==base_revision,"Loft eye recalculated its definition or scene");
        auto* order=profile_table->cellWidget(0,1)->findChild<QCheckBox*>();click(order,order->rect().center());
        dialog->findChild<QPushButton*>("loftProfilesDown")->click();flush();
        check(dialog->pending.loft.profiles[1]==profiles[0]&&profile_table->cellWidget(1,1)->findChild<QCheckBox*>()->isChecked(),"Loft ordering lost selected identity");
        dialog->findChild<QPushButton*>("loftProfilesUp")->click();flush();
        const auto pending_before_editor=dialog->pending.loft;
        field(profile_table,0,3);flush();
        auto* child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(child&&!dialog->isVisible(),"Owned Loft Sketch did not open shared Properties");
        check(!child->findChild<QComboBox*>("featureType")->isEnabled(),"Loft permits converting its owned Sketch to 3D geometry");
        child->set_inline_parameter_value("placement:z",1);child->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(dialog->isVisible()&&dialog->pending.loft==pending_before_editor,"Owned Sketch Cancel modified Loft draft");
        field(profile_table,0,3);flush();child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        child->findChild<QPushButton*>("featureSketchButton")->click();flush();
        auto* finish=window.findChild<QAction*>("finishSketchAction");check(finish&&finish->isEnabled(),"Owned Loft 2D Sketch did not enter ordinary Sketcher");
        finish->trigger();flush();child->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(dialog->isVisible()&&window.execute_console_command("documents").data==initial,"Owned Sketch editing committed Loft before parent OK");
        field(guide_table,guide_table->rowCount()-1,3);flush();
        child=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("generalSurfaceSketchProperties"));
        check(child&&dialog->pending.loft.guides.size()==3,"Loft could not create an owned guide Sketch");
        child->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        guide_table->cellWidget(2,2)->findChild<QPushButton*>()->click();flush();
        check(dialog->pending.loft.guides==guides&&dialog->pending.loft.sketches.size()==5,"Removing owned guide retained its native definition");
        const auto unchanged=dialog->pending.loft;
        click(view,view->rect().center(),Qt::MiddleButton);
        check(editor()&&dialog->pending.loft==unchanged,"Short middle click committed or deleted a Loft reference");
        auto* placement=dialog->findChild<QTableWidget*>("sweepPlacementReferences");check(placement,"Loft shared placement missing");
        field(placement,0,1);pick_tree(row(body+":origin","document-origin"));
        check(dialog->first_empty_position_index()==3,"Loft whole-Origin entry did not use the complete placement contract");
        check(dialog->set_inline_parameter_value("placement:reference_offset:0",3),"Loft placement correction is unavailable");flush();
        auto* name=dialog->findChild<QLineEdit*>("loftName");const auto top=name->mapTo(dialog,QPoint(0,0)).y();
        dialog->resize(dialog->width()+80,std::max(dialog->minimumHeight(),800));flush();
        dialog->resize(dialog->width(),900);flush();
        check(name->mapTo(dialog,QPoint(0,0)).y()==top&&profile_table->rowHeight(0)==34&&guide_table->rowHeight(0)==34,"Loft resizing redistributed upper fields or row height");
        const QPointF center(view->rect().center());
        QMouseEvent accept(QEvent::MouseButtonDblClick,center,QPointF(view->mapToGlobal(center.toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&accept);flush();if(auto* failed=editor())throw std::runtime_error("Middle double-click did not commit Loft: "+failed->findChild<QLabel*>("loftStatus")->text().toStdString());
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> calculated;auto saved=document::PartDocument::load(path,&calculated);
        check(saved.find_container(id)&&saved.find_container(id)->loft==unchanged,"Loft native file lost source lists");
        check(saved.serialized().at("sketches")==sources,"Loft changed its ordinary source Sketches");
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Loft GUI committed a failed calculation");
        check(std::abs(calculated.back().volume-1000*std::numbers::pi)<1e-4,"Loft GUI solid volume is wrong");
        const auto selection_state=window.execute_console_command("documents").data;
        view->fit_all();flush();std::optional<QPoint> feature_pick;
        for(int y=20;y<view->height()-20&&!feature_pick;y+=6)for(int x=20;x<view->width()-20&&!feature_pick;x+=6) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if(!candidates.empty()&&candidates.front().owner_id==id&&
               (candidates.front().kind==viewer::CandidateKind::Face||candidates.front().kind==viewer::CandidateKind::Container))
                feature_pick=QPoint(x,y);
        }
        check(feature_pick.has_value(),"Loft solid is not selectable in View");
        click(view,*feature_pick);
        check(view->confirmed_candidate()&&view->confirmed_candidate()->owner_id==id,"Loft face selected a source feature");
        check(row(id)&&row(id)->isSelected(),"Loft View selection did not synchronize the Tree");
        check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& dim){return dim.reference.owner_id==id;}),"Loft single click exposed parameter dimensions");
        QMouseEvent inspect(QEvent::MouseButtonDblClick,QPointF(*feature_pick),QPointF(view->mapToGlobal(*feature_pick)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&inspect);flush();
        check(!editor(),"Loft double click opened Properties instead of ordinary inspection");
        const auto authored=document::loft_definition_mesh(saved,*saved.find_container(id)).edges;
        const auto& inspection=view->container_inspection_wire();
        check(inspection.size()==authored.size(),"Loft inspection added calculated result edges");
        for(std::size_t i=0;i<authored.size();++i)
            check(inspection[i].points==authored[i].points,"Loft ordinary inspection differs from its authored preview");
        check(window.execute_console_command("documents").data==selection_state,"Loft ordinary selection or inspection changed the document");
        const auto stored=*saved.find_container(id);const auto state=window.execute_console_command("documents").data;
        auto* item=row(id);check(item,"Loft Tree item missing");window.show_tree_item_properties(item);flush();
        dialog=editor();check(dialog&&dialog->pending.loft==stored.loft,"Loft Properties did not reopen the same definition");
        check(std::ranges::none_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==id;}),"Loft Properties retained its final solid instead of rollback");
        dialog->findChild<QComboBox*>("loftResultType")->setCurrentIndex(1);
        dialog->findChild<QDoubleSpinBox*>("loftThickness")->setValue(2);
        dialog->findChild<QComboBox*>("loftThinSide")->setCurrentIndex(2);flush();
        check(std::ranges::any_of(view->mesh().dimensions,[&](const auto& dim){return dim.reference.owner_id==id&&dim.reference.semantic_key=="parameter:thickness";}),"Loft Thin parameter inspection is missing");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==state,"Loft Cancel committed pending result changes");
        window.show_tree_item_properties(row(id));flush();dialog=editor();check(dialog,"Loft no-op Properties missing");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(window.execute_console_command("documents").data==state,"Unchanged Loft OK changed the revision");
        window.show_tree_item_properties(row(id));flush();dialog=editor();
        dialog->findChild<QComboBox*>("loftResultType")->setCurrentIndex(1);
        dialog->findChild<QDoubleSpinBox*>("loftThickness")->setValue(2);
        dialog->findChild<QComboBox*>("loftThinSide")->setCurrentIndex(2);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();if(auto* failed=editor())throw std::runtime_error("Thin Loft OK failed: "+failed->findChild<QLabel*>("loftStatus")->text().toStdString());
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        saved=document::PartDocument::load(path,&calculated);
        check(saved.find_container(id)->loft.result_type==document::ProfileResultType::Thin&&
              saved.find_container(id)->loft.thin_mode==document::ThinMode::Symmetric&&
              std::abs(calculated.back().volume-400*std::numbers::pi)<1e-4,"Thin Loft save/reopen lost thickness, side or volume");
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).find_container(id)->loft.result_type==document::ProfileResultType::Solid,"Loft Undo did not restore result type");
        window.findChild<QAction*>("redoAction")->trigger();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        check(document::PartDocument::load(path).find_container(id)->loft.result_type==document::ProfileResultType::Thin,"Loft Redo lost result type");
        const auto thin_selection_state=window.execute_console_command("documents").data;
        view->fit_all();flush();feature_pick.reset();
        for(int y=20;y<view->height()-20&&!feature_pick;y+=6)for(int x=20;x<view->width()-20&&!feature_pick;x+=6) {
            const auto candidates=view->selection_candidates_at(QPointF(x,y));
            if(!candidates.empty()&&candidates.front().owner_id==id&&
               (candidates.front().kind==viewer::CandidateKind::Face||candidates.front().kind==viewer::CandidateKind::Container))feature_pick=QPoint(x,y);
        }
        check(feature_pick.has_value(),"Thin Loft has no selectable result face");click(view,*feature_pick);
        check(std::ranges::none_of(view->mesh().dimensions,[&](const auto& dim){return dim.reference.owner_id==id;}),"Thin Loft single click exposed dimensions");
        QMouseEvent inspect_thin(QEvent::MouseButtonDblClick,QPointF(*feature_pick),QPointF(view->mapToGlobal(*feature_pick)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&inspect_thin);flush();
        check(!editor()&&std::ranges::any_of(view->mesh().dimensions,[&](const auto& dim){return dim.reference.owner_id==id&&dim.reference.semantic_key=="parameter:thickness";}),"Thin Loft double click did not expose thickness");
        check(row(id)->isSelected()&&window.execute_console_command("documents").data==thin_selection_state,"Thin Loft inspection lost selection or changed the document");
        std::cout<<"Loft GUI: five languages, stacked lists, owned 2D Sketch edit/add/remove, eye/order, whole Origin, numeric placement, resize, rollback, no-op, middle confirmation, View/Tree single/double inspection, Thin sides, native reopen and Undo/Redo passed"<<std::endl;
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
