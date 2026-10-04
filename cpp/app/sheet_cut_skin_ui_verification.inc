int verify_sheet_cut_skin_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        window.resize(1280,900);window.show();flush();
        for(const bool from_top:{false,true})for(const bool clearance:{false,true}) {
            std::cout<<"Sheet skin GUI: reverse="<<from_top<<", clearance="<<clearance<<std::endl;
            kernel::OcctKernel kernel;workspace::Workspace live;
            auto source=document::PartDocument::create_default();const auto document_id=source.document_id;live.add_part(source);
            auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
            auto outline=sketcher::Sketch::create_default();outline.owner_container_id=flat.id;flat.flat.sketch_id=outline.id;
            flat.flat.thickness=2;flat.flat.thickness_override=true;static_cast<void>(outline.add_rectangle(0,0,20,20));
            static_cast<void>(workspace::commit_flat(live,kernel,document_id,flat,outline));
            auto* state=live.open_part(document_id);
            const auto& originals=state->session.calculated_boundaries().back().mesh.original_references;
            kernel::FaceReference start,target;
            for(std::size_t i=0;i<originals.triangle_references.size();++i) {
                const auto& ref=originals.triangle_references[i];
                if(ref.owner_id!=flat.id||(ref.sheet_role!=kernel::SheetFaceRole::SideA&&ref.sheet_role!=kernel::SheetFaceRole::SideB))continue;
                const double z=originals.vertices[originals.triangles[3*i]].z;
                if(std::abs(z-(from_top?2.:0.))<1e-8)start=ref;
                if(std::abs(z-(from_top?0.:2.))<1e-8)target=ref;
            }
            check(!start.owner_id.empty()&&!target.owner_id.empty()&&start!=target,"GUI fixture has no distinct sheet skins");
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(2,2,6,6));
            auto cut=document::PartDocument::create_extrusion_container(sketch.id);
            cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;cut.extrusion.sheet_cut_clearance=clearance;
            cut.placement.z=from_top?2.:0.;cut.placement.references={{{},start.owner_id,start.semantic_key}};
            cut.extrusion.length_forward=3;cut.extrusion.direction=from_top?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
            workspace::commit_profile(live,kernel,document_id,cut,workspace::ProfileEditMode::Create,sketch);
            const auto path=directory/(std::string("sheet-skin-ui-")+(from_top?"top-":"bottom-")+(clearance?"clearance":"normal")+".prtz");
            state->session.document().save(path,state->session.calculated_boundaries());
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open GUI sheet cut fixture");flush();
            const auto body=state->session.document().body_history.active_body_id();
            const auto activate=window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",body}}}}.dump()));
            check(activate.ok,"Cannot activate Sheet Cut GUI Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto row=[&]()->QTreeWidgetItem* {
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==cut.id&&(*it)->data(0,Qt::UserRole+3)=="part-container")return *it;
                return nullptr;
            };
            check(row(),"Sheet Cut Tree row missing");window.show_tree_item_properties(row());flush();
            const auto dialog=[&]()->PrimitivePropertiesDialog* {
                for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<PrimitivePropertiesDialog*>(child);d&&d->isVisible())return d;
                return nullptr;
            };
            check(dialog(),"Sheet Cut Properties missing");
            auto* mode=dialog()->findChild<QComboBox*>("extrusionForwardEndCondition");
            check(mode,"Sheet Cut extent control missing");mode->setCurrentIndex(mode->findData("up_to"));flush();
            auto* field=dialog()->findChild<QLineEdit*>("extrusionForwardEndTarget");check(field,"Sheet Cut end reference missing");
            const auto click=[&](QWidget* widget,QPointF point,Qt::MouseButton button) {
                for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,point,QPointF(widget->mapToGlobal(point.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(widget,&event);flush();
                }
            };
            click(field,QPointF(field->width()/2,field->height()/2),Qt::LeftButton);
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Sheet Cut View missing");view->fit_all();flush();
            std::optional<QPointF> position;
            for(int y=20;y<view->height()-20&&!position;y+=6)for(int x=20;x<view->width()-20&&!position;x+=6)
                if(std::ranges::any_of(view->selection_candidates_at(QPointF(x,y)),[&](const auto& c){return c.kind==viewer::CandidateKind::Face&&c.owner_id==target.owner_id&&c.semantic_key==target.semantic_key;}))position=QPointF(x,y);
            check(position.has_value(),"Opposite original sheet skin absent from common picker");
            QMouseEvent move(QEvent::MouseMove,*position,QPointF(view->mapToGlobal(position->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            for(std::size_t i=0;i<view->selection_candidates_at(*position).size();++i) {
                const auto offered=view->offered_candidate();
                if(offered&&offered->owner_id==target.owner_id&&offered->semantic_key==target.semantic_key)break;
                click(view,*position,Qt::RightButton);
            }
            const auto offered=view->offered_candidate();check(offered&&offered->owner_id==target.owner_id&&offered->semantic_key==target.semantic_key,"Sheet Cut cycling did not offer exact target skin");
            click(view,*position,Qt::LeftButton);
            check(dialog()->pending_value().extrusion.end_targets_forward.size()==1&&
                dialog()->pending_value().extrusion.end_targets_forward.front().reference==target,"Sheet Cut View pick lost native skin identity");
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Exact skin-to-skin Sheet Cut OK failed");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            std::vector<kernel::BodyResult> cache;const auto saved=document::PartDocument::load(path,&cache);
            check(!cache.empty()&&cache.back().calculation_errors.empty()&&std::abs(cache.back().volume-768.)<1e-5,"GUI skin cut retained material or failed calculation");
            const auto* feature=saved.find_container(cut.id);check(feature&&feature->extrusion.end_targets_forward.front().reference==target&&
                feature->placement.references.front().semantic_key==start.semantic_key,"GUI native reopening lost source/target side choices");
            const auto documents=window.execute_console_command("documents").data;
            window.show_tree_item_properties(row());flush();check(dialog(),"Sheet Cut edit cannot reopen");
            dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==documents,"Sheet Cut Cancel changed document");
            window.show_tree_item_properties(row());flush();check(dialog(),"Sheet Cut unchanged editor missing");
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Sheet Cut unchanged OK failed");
            check(window.execute_console_command("documents").data==documents,"Sheet Cut unchanged OK created a transaction");
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
}
