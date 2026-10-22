int verify_opening_axis_direction_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(bool bottom:{false,true})for(bool axis_first:{false,true}) {
            std::cout<<"Opening axis case bottom="<<bottom<<" axis_first="<<axis_first<<std::endl;
            auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
            static_cast<void>(sketch.add_circle(0,0,20));
            auto solid=document::PartDocument::create_feature_container(sketch.id);
            solid.feature.symmetric=true;solid.feature.sides[0].length=20;solid.feature.origin_centerline=true;
            sketch.owner_container_id=solid.id;part.history={solid};part.sketches={sketch};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Opening axis test");
            graph.insert({document::PartHistoryKind::Feature,solid.id});part.set_body_history(graph);part.resolve_constructions();
            const auto calculated=kernel::OcctKernel{}.evaluate_history(part.kernel_operations());
            check(calculated.back().calculation_errors.empty(),"Opening axis source failed");
            const auto& geometry=calculated.back().mesh.original_references;
            const auto axis=std::ranges::find_if(geometry.axes,[&](const auto& a){return a.reference.owner_id==solid.id;});
            check(axis!=geometry.axes.end(),"Opening fixture lacks a persisted profile axis");
            const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& f){return f.surface&&
                f.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(f.surface->origin.z-(bottom?-20:20))<1e-7;});
            check(face!=geometry.triangle_references.end(),"Opening fixture lacks its entry cap");
            const auto path=directory/(std::string("opening-axis-")+(bottom?"bottom-":"top-")+(axis_first?"axis-first":"face-first")+".prtz");
            part.save(path,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1400,1000);window.show();flush();
            window.findChild<QAction*>("applicationModeAction0")->trigger();flush();
            const auto opening_dialog=[&]() -> app::PrimitivePropertiesDialog* {
                for(auto* widget:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::PrimitivePropertiesDialog*>(widget);candidate&&candidate->isVisible())return candidate;
                return nullptr;
            };
            const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
                const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
                if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
            };
            check(window.open_document_path(QString::fromStdString(path.string())),"Opening fixture cannot open");flush();
            run("body.activate",{{"body",body}});
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));check(view,"Opening View missing");
            window.findChild<QAction*>("showAxesAction")->setChecked(true);flush();
            auto* action=window.findChild<QAction*>("threadAction");check(action&&action->isEnabled(),"Opening command is disabled");action->trigger();flush();
            auto* dialog=opening_dialog();check(dialog,"Opening creation missing");
            dialog->findChild<QCheckBox*>("threadDrillPoint")->setChecked(false);
            const auto before=run("documents");
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&e);flush();
            };
            const auto pick=[&](std::size_t row,viewer::CandidateKind kind,const auto& reference) {
                dialog->set_active_reference_index(row);
                // Arm the same reference-entry callback as a field click.
                auto* table=dialog->findChild<QTableWidget*>("primitiveReferenceTable");check(table,"Opening reference table missing");
                const QPointF at(table->visualItemRect(table->item(int(row),1)).center());
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent e(type,at,QPointF(table->viewport()->mapToGlobal(at.toPoint())),Qt::LeftButton,
                        type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&e);
                }
                flush();
                std::optional<QPointF> pixel;
                for(int y=12;y<view->height()-12&&!pixel;y+=5)for(int x=12;x<view->width()-12;x+=5) {
                    const auto candidates=view->selection_candidates_at(QPointF(x,y));
                    if(std::ranges::any_of(candidates,[&](const auto& c){return c.kind==kind&&c.owner_id==reference.owner_id&&c.semantic_key==reference.semantic_key;})) {pixel=QPointF(x,y);break;}
                }
                check(pixel.has_value(),"Opening source not available in the mouse picker");
                mouse(*pixel,QEvent::MouseMove,Qt::NoButton);
                const auto offered=[&]{const auto c=view->offered_candidate();return c&&c->kind==kind&&c->owner_id==reference.owner_id&&c->semantic_key==reference.semantic_key;};
                for(int cycle=0;cycle<64&&!offered();++cycle){mouse(*pixel,QEvent::MouseButtonPress,Qt::RightButton);mouse(*pixel,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(offered(),"Opening mouse cycling lost the intended reference");
                mouse(*pixel,QEvent::MouseButtonPress,Qt::LeftButton);mouse(*pixel,QEvent::MouseButtonRelease,Qt::LeftButton);
            };
            if(axis_first){pick(0,viewer::CandidateKind::Axis,axis->reference);pick(1,viewer::CandidateKind::Face,*face);}
            else {pick(0,viewer::CandidateKind::Face,*face);pick(1,viewer::CandidateKind::Axis,axis->reference);}
            std::cout<<"Opening axis references entered"<<std::endl;
            check(run("documents")==before,"Opening reference preview changed the document");
            auto draft=dialog->pending_value();
            const auto verify_ray=[&](const document::HistoryContainer& value,bool inward) {
                std::optional<kernel::ViewerAxis> ray;static_cast<void>(part.thread_edges(value,nullptr,&ray));check(ray.has_value(),"Opening preview ray missing");
                check((ray->direction.z*(bottom?-1.:1.)<0)==inward&&std::abs(ray->direction.z)>1-1e-9,"Opening preview points to the wrong side");
                check(std::abs(value.placement.z-(bottom?-20:20))<1e-7,"Opening axis/face station moved");
            };
            verify_ray(draft,true);
            auto* direction=dialog->findChild<QComboBox*>("openingDirection");check(direction,"Opening direction control missing");
            direction->setCurrentIndex(1-direction->currentIndex());flush();verify_ray(dialog->pending_value(),false);
            direction->setCurrentIndex(1-direction->currentIndex());flush();verify_ray(dialog->pending_value(),true);
            // Opposite axis-side choices retain their own frame and receive
            // the corresponding initial bore direction, without editing them.
            for(bool flip:{false,true}) {
                auto candidate=draft;
                for(auto& ref:candidate.placement.references)if(ref.owner_id==axis->reference.owner_id&&ref.semantic_key==axis->reference.semantic_key)ref.flip=flip;
                check(document::resolve_placement(candidate.placement,geometry),"Opening flipped axis cannot resolve");
                const auto advice=workspace::opening_entry_direction(candidate,geometry);check(advice.has_value(),"Opening axis side has no direction advice");
                candidate.thread.direction=*advice;verify_ray(candidate,true);
            }
            const auto id=draft.id;dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();run("save");
            std::cout<<"Opening axis creation confirmed"<<std::endl;
            std::vector<kernel::BodyResult> cache;auto saved=document::PartDocument::load(path,&cache);
            check(saved.find_container(id)&&cache.back().calculation_errors.empty(),"Opening OK failed");verify_ray(*saved.find_container(id),true);
            const auto& p=draft.thread;const double radius=(p.enabled?p.profile_diameter:p.nominal_diameter)/2;
            const double slope=std::tan(p.chamfer_angle_degrees*std::numbers::pi/360);
            const double depth=p.chamfer_enabled?p.chamfer_depth:0;
            const double removed=std::numbers::pi*(radius*radius*p.bore_length+
                radius*depth*depth*slope+depth*depth*depth*slope*slope/3);
            check(std::abs(calculated.back().volume-cache.back().volume-removed)<1e-5,"Opening did not remove its exact inward bore/chamfer volume");
            run("undo");run("save");check(!document::PartDocument::load(path).find_container(id),"Opening Undo retained the feature");
            run("redo");run("save");saved=document::PartDocument::load(path,&cache);verify_ray(*saved.find_container(id),true);
            const auto open_edit=[&] {
                auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* item{};
                for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){item=*i;break;}
                check(item,"Opening Tree row missing");window.show_tree_item_properties(item);flush();
            };
            open_edit();
            dialog=opening_dialog();check(dialog,"Opening edit missing");
            check(dialog->pending_value().thread.direction==draft.thread.direction,"Opening edit reinterpreted stored direction");
            const auto revision=run("documents");dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(run("documents")==revision,"Unchanged Opening edit created a transaction");
            open_edit();dialog=opening_dialog();check(dialog,"Opening Cancel edit missing");
            direction=dialog->findChild<QComboBox*>("openingDirection");direction->setCurrentIndex(1-direction->currentIndex());flush();dialog->reject();flush();run("save");
            check(document::PartDocument::load(path).find_container(id)->thread==draft.thread,"Opening Cancel changed its direction");
            window.findChild<QAction*>("threadAction")->trigger();flush();
            dialog=opening_dialog();check(dialog,"Explicit Opening creation missing");
            direction=dialog->findChild<QComboBox*>("openingDirection");direction->setCurrentIndex(1);flush();
            pick(0,viewer::CandidateKind::Axis,axis->reference);pick(1,viewer::CandidateKind::Face,*face);
            check(dialog->pending_value().thread.direction==document::ExtrusionDirection::Reverse,"Opening initialization replaced an explicit direction");
            dialog->reject();flush();
            std::cout<<"Opening axis GUI bottom="<<bottom<<" axis_first="<<axis_first<<" passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
