void verify_mech_keyway_trials_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory){
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto click=[&](QWidget* widget,QPointF point){
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}){
            QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);flush();}
    };
    for(int trial=0;trial<3;++trial){
        const bool hub=trial!=0;
        const auto user_source=directory.parent_path()/"OTVOR-source.prtz";
        if(trial==2&&!std::filesystem::exists(user_source))continue;
        workspace::Workspace live;kernel::OcctKernel kernel;
        auto part=trial==2?document::PartDocument::load(user_source):document::PartDocument::create_default();const auto id=part.document_id;
        std::string body;
        if(trial==2){
            body=part.body_history.active_body_id();
            auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
            live.add_part(part,std::move(calculated));live.activate(id);
        }else{
            document::BodyHistoryGraph graph;body=graph.create_body(hub?"Trial hub":"Trial shaft");part.set_body_history(graph);
            live.add_part(part);live.activate(id);
            auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
            static_cast<void>(sketch.add_circle(0,0,hub?60:50));
            if(hub)static_cast<void>(sketch.add_circle(0,0,30));
            auto base=document::PartDocument::create_feature_container(sketch.id);base.feature.sides[0].length=80;
            workspace::commit_profile(live,kernel,id,base,workspace::ProfileEditMode::Create,sketch);
        }
        const auto prefix=trial==2?"user-hub":hub?"hub":"confirmation";
        auto* native=live.open_part(id);const auto file=directory/(std::string(prefix)+"-trial.prtz");
        native->session.document().save(file,native->session.calculated_boundaries());
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"Trial fixture did not open");flush();
        check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump())).ok,"Trial Body activation failed");flush();
        window.findChild<QAction*>("applicationModeAction9")->trigger();flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        const auto editor=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("mechKeywayPropertiesDialog"));};
        auto* create=window.findChild<QAction*>("mechKeywayAction");create->trigger();flush();
        auto* dialog=editor();check(dialog,"Trial keyway dialog unavailable");
        auto* panel=dialog->mech_keyway_panel();
        kernel::FaceReference support;std::vector<kernel::FaceReference> ends;
        for(const auto& face:native->session.calculated_boundaries().back().mesh.triangle_references)if(face.surface){
            if(face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&face.surface->reversed==hub)support=face;
            if(face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::ranges::none_of(ends,[&](const auto& end){return end.owner_id==face.owner_id&&end.semantic_key==face.semantic_key&&end.instance_path==face.instance_path;}))ends.push_back(face);
        }
        const auto pick=[&](const kernel::FaceReference& target){
            bool picked=false;
            for(const kernel::Vec3 direction:std::array<kernel::Vec3,4>{{{1,1,-2},{1,1,2},{-1,-1,-2},{-1,-1,2}}}){
                view->set_view_direction(direction);view->fit_all();flush();
                for(int y=25;y<view->height()-25&&!picked;y+=6)for(int x=25;x<view->width()-25&&!picked;x+=6){
                    const auto offered=view->selection_candidates_at({double(x),double(y)});
                    if(!offered.empty()&&offered.front().owner_id==target.owner_id&&offered.front().semantic_key==target.semantic_key){click(view,{double(x),double(y)});picked=true;}
                }
                if(picked)break;
            }
            check(picked,"Trial face cannot be selected with the mouse");
        };
        pick(support);check(panel->parameters().internal==hub,"Trial support material side changed");
        const auto pending_id=dialog->container_id();
        check(std::ranges::none_of(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==pending_id&&a.reference.semantic_key=="axis";}),"Keyway still displays the unrelated construction axis");
        const auto before=window.execute_console_command("documents").data;
        // Missing hub ends and unavailable shaft diameter both fail before OCCT.
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(editor()==dialog&&window.execute_console_command("documents").data==before,"First failed OK closed or committed the trial");
        auto* message=dialog->findChild<QLabel*>("propertiesSubmitError");
        check(message&&!message->text().isEmpty(),"First failed OK did not explain the second confirmation");
        window.grab().save(QString::fromStdWString((directory/(std::string(prefix)+"-first-failure.png")).wstring()));
        const auto calls=kernel::body_calculation_count.load();
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!editor()&&kernel::body_calculation_count.load()==calls,"Second unchanged keyway OK did not close without recalculation");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto saved=document::PartDocument::load(file);
        check(saved.find_container(pending_id)&&saved.reference_errors.contains(pending_id),"Trial keyway was not saved unresolved");
        QTreeWidgetItem* row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==pending_id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
        check(row,"Unresolved trial keyway is absent from Tree");window.show_tree_item_properties(row);flush();
        dialog=editor();panel=dialog->mech_keyway_panel();
        if(hub){
            check(ends.size()==2,"Trial hub has an unexpected end count");
            const auto station=[&](const auto& face){const auto delta=kernel::dimension_sub(face.surface->origin,support.surface->origin);return kernel::dimension_dot(delta,support.surface->axis);};
            std::sort(ends.begin(),ends.end(),[&](const auto& a,const auto& b){return station(a)<station(b);});
            const int forward=panel->parameters().axial_direction>0?1:0;
            panel->begin_entry(1);flush();pick(ends[forward]);
            panel->begin_entry(2);flush();pick(ends[1-forward]);
        }else{
            auto* catalogue=dialog->findChild<QComboBox*>("mechKeyCatalogue");catalogue->setCurrentIndex(catalogue->findData("zima-csn-parallel"));flush();
            check(panel->parameters().width==28,"CSN diameter 100 did not select 28 mm");
            check(dialog->set_inline_parameter_value("mech_offset",10),"Trial offset is not editable");flush();
        }
        check(!view->transient_edges().empty(),"Repaired trial keyway has no preview");
        window.grab().save(QString::fromStdWString((directory/(std::string(prefix)+"-preview.png")).wstring()));
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        if(editor())for(const auto* key:{"featureValidationError","propertiesSubmitError","mechKeyPreviewError"}){
            auto* error=editor()->findChild<QLabel*>(key);if(error)std::cerr<<error->text().toStdString()<<'\n';}
        check(!editor(),"Repaired trial keyway did not close on first OK");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();saved=document::PartDocument::load(file);
        check(!saved.reference_errors.contains(pending_id),"Repaired trial keyway remains unresolved");
        auto cold=workspace::calculate_part_with_resolved_references(kernel,saved);
        check(!cold.empty()&&cold.back().calculation_errors.empty(),"Repaired trial cold regeneration failed");
    }
    const auto failed_user=directory.parent_path()/"OTVOR-unresolved-user.prtz";
    if(std::filesystem::exists(failed_user)){
        window.findChild<QAction*>("closeDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> cache;auto part=document::PartDocument::load(failed_user,&cache);
        const auto parent=*std::ranges::find_if(part.history,[](const auto& c){return c.feature_kind==document::FeatureKind::MechKeyway;});
        const auto repaired=directory/"user-angular-hub.prtz";part.save(repaired,cache);
        check(window.open_document_path(QString::fromStdWString(repaired.wstring())),"Angular user hub did not open");flush();
        window.findChild<QAction*>("regenerateDocumentAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        auto saved=document::PartDocument::load(repaired);
        check(!saved.reference_errors.contains(parent.id),"Regenerate did not repair the saved angular hub");
        check(saved.find_container(parent.id)->placement.references==parent.placement.references,"Regenerate changed authored hub placement references");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==parent.id&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
        check(row,"Angular hub Tree row missing");window.show_tree_item_properties(row);flush();
        auto* dialog=dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("mechKeywayPropertiesDialog"));
        check(dialog,"Angular hub properties did not open");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        check(!view->transient_edges().empty(),"Angular hub preview is empty");
        auto* error=dialog->findChild<QLabel*>("mechKeyPreviewError");check(error&&!error->isVisible(),"Angular hub preview remains unresolved");
        window.grab().save(QString::fromStdWString((directory/"user-angular-hub-preview.png").wstring()));
        const auto calls=kernel::body_calculation_count.load();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        check(!window.findChild<QWidget*>("mechKeywayPropertiesDialog")&&kernel::body_calculation_count.load()==calls,"Unchanged angular hub OK calculated or failed to close");
    }
    std::cout<<"MECH trials: real mouse support/end entry, first/second OK, unresolved Tree/save, CSN repair, hub preview/cold regeneration and saved angular hub repair passed\n";
}
