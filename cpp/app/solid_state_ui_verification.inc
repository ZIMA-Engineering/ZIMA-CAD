int verify_solid_state_ui(QApplication& application,const std::filesystem::path& directory,bool sweep_only=false) {
    using namespace zima;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto inspect_from_view=[&](viewer::MeshView* view,QTreeWidget* tree,const std::string& owner,const std::string& path) {
        view->set_selection_filter(viewer::SelectionFilter::All);view->fit_all();flush();
        std::optional<QPointF> hit;
        for(int y=5;y<view->height()&&!hit;y+=8)for(int x=5;x<view->width()&&!hit;x+=8) {
            const auto candidates=view->selection_candidates_at({double(x),double(y)});
            if(!candidates.empty()&&candidates.front().kind==viewer::CandidateKind::Container&&
               candidates.front().owner_id==owner&&candidates.front().instance_path==path)hit=QPointF(x,y);
        }
        if(!hit)std::cerr<<"Missing ordinary View candidate for "<<owner<<" in "<<path<<'\n';
        check(hit.has_value(),"Straightened feature cannot be selected in ordinary View");
        for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*hit,QPointF(view->mapToGlobal(hit->toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        const auto confirmed=view->confirmed_candidate();
        if(!confirmed||confirmed->owner_id!=owner||confirmed->instance_path!=path)
            std::cerr<<"View click expected "<<owner<<" in "<<path<<", confirmed "<<(confirmed?confirmed->owner_id:"<none>")
                <<" in "<<(confirmed?confirmed->instance_path:"")<<" kind "<<(confirmed?int(confirmed->kind):-1)<<'\n';
        check(confirmed&&confirmed->kind==viewer::CandidateKind::Container&&confirmed->owner_id==owner&&confirmed->instance_path==path,
            "Ordinary View confirmed a different feature");
        check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==owner,
            "Ordinary View did not synchronize the feature Tree row");
        QMouseEvent double_click(QEvent::MouseButtonDblClick,*hit,QPointF(view->mapToGlobal(hit->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&double_click);flush();
        check(!view->container_inspection_wire().empty(),"View double-click has no original blue feature wire");
    };
    try {
        for(int scenario=0;scenario<5;++scenario) {
            const bool context=!sweep_only&&scenario==1;
            const bool treated=sweep_only?scenario==4:scenario==2;
            kernel::OcctKernel kernel;workspace::Workspace setup;
            auto part=document::PartDocument::create_default();
            if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
            if(!sweep_only&&scenario==4) {
                auto graph=part.body_history;auto body=*graph.find(graph.active_body_id());
                body.scope.placement={80,40,25};
                body.scope.placement.absolute_rotation_y=90;body.scope.placement.rotation_y=90;
                graph.update_body(body);part.set_body_history(graph);
            }
            const auto id=part.document_id;setup.add_part(part,{});setup.activate(id);
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(100,0,120,20));
            if(!sweep_only&&scenario==3)static_cast<void>(sketch.add_rectangle(140,0,150,20));
            sketch.dimensions.push_back(sketch.create_segment_dimension(sketch.segments.front().id));
            const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
            auto source=document::PartDocument::create_revolution_container(sketch.id);
            source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
            if(!sweep_only&&scenario==0)source.revolution.centroid_centerline=true;
            if(sweep_only) {
                source=test_support::sweep_fixture(scenario==0||scenario==4?document::FeatureKind::Sweep2D:
                    scenario==1||scenario==3?document::FeatureKind::Sweep3D:document::FeatureKind::HelicalSweep);
                source.sweep_precision.custom_tolerance=1e-4;
                if(scenario==0||scenario==4) {
                    auto guide=sketcher::Sketch::from_serialized(source.sweep2d.path_sketch);
                    guide.segments.clear();guide.points.clear();
                    static_cast<void>(guide.add_bspline({{0,0},{0,100./3},{20./3,200./3},{20,100}}));
                    source.sweep2d.path_sketch=guide.serialized();source.sweep2d.profiles.clear();
                    document::PartDocument::reframe_sweep2d_sketches(source);
                    const auto station=document::PartDocument::sweep2d_route(source).stations.front();
                    const auto index=document::PartDocument::ensure_sweep2d_profile(source,station.point_id,station.incoming);
                    auto section=sketcher::Sketch::from_serialized(source.sweep2d.profiles[index].sketch_serialized);
                    if(treated)static_cast<void>(section.add_rectangle(-2,-2,2,2));
                    else static_cast<void>(section.add_circle(0,0,2));
                    source.sweep2d.profiles[index].sketch_serialized=section.serialized();
                } else if(scenario==1||scenario==3) {
                    source.sweep3d.path.curve_points.clear();source.sweep3d.path.curve_type=document::Curve3DType::InterpolatingSpline;
                    for(const auto origin:std::vector<kernel::Vec3>{{0,0,0},{5,1,25},{15,5,50},{30,15,75}}) {
                        auto point=document::PartDocument::create_construction(document::ConstructionKind::Point);
                        point.parent_construction_id=source.sweep3d.path.id;point.origin=origin;source.sweep3d.path.curve_points.push_back(point);
                    }
                    source.sweep3d.profiles.front().point_id=source.sweep3d.path.curve_points.front().id;
                    if(scenario==3) {
                        auto section=sketcher::Sketch::from_serialized(source.sweep3d.profiles.front().sketch_serialized);
                        section.circles.clear();section.points.clear();
                        static_cast<void>(section.add_rectangle(-1,-.5,1,.5));
                        source.sweep3d.profiles.front().sketch_serialized=section.serialized();
                    }
                }
                workspace::commit_sweep(setup,kernel,id,source,workspace::SweepEditMode::Create);
            } else {
                if(scenario>=3) {
                    source=document::PartDocument::create_feature_container(sketch.id);
                    source.feature.sides[0].length=100;
                    source.feature.sides[0].operation=scenario==3?document::FeatureSideOperation::Twist:document::FeatureSideOperation::Extrusion;
                    if(scenario==4)source.feature.sides[1].operation=document::FeatureSideOperation::Revolution;
                    source.feature.axis_segment_id=axis;
                    if(scenario==3)source.feature.centroid_centerline=source.feature.origin_centerline=true;
                }
                workspace::commit_profile(setup,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
            }
            const auto* state=setup.open_part(id);
            std::optional<document::HistoryContainer> authored_fillet;
            if(treated) {
                const auto& mesh=state->session.calculated_boundaries().back().mesh;
                const auto prefix=sweep_only?"sweep:"+document::PartDocument::sweep2d_request(source).path_segments.front().source_id+":profile:":"generated:";
                const auto edge=std::ranges::find_if(mesh.edges,[&](const auto& e) {
                    return e.reference.owner_id==source.id&&e.reference.semantic_key.starts_with(prefix)&&
                        e.edge_treatment_endpoint_references.size()==2;
                });
                check(edge!=mesh.edges.end(),"Treated GUI fixture has no longitudinal edge");
                auto fillet=document::PartDocument::create_fillet_container({edge->reference});
                fillet.edge_treatment.primary_size=sweep_only?.2:2;
                check(workspace::commit_edge_treatment(setup,kernel,id,fillet,workspace::EdgeTreatmentEditMode::Create),
                    "Cannot create GUI source Fillet");
                authored_fillet=*state->session.document().find_container(fillet.id);
            }
            part=state->session.document();
            const auto fixture=directory/(sweep_only?"solid-sweep-"+std::to_string(scenario):scenario>=3?"solid-group-"+std::to_string(scenario):context?"solid-context":treated?"solid-fillet":"solid-part");std::filesystem::create_directories(fixture);
            const auto path=fixture/"source.prtz";part.save(path,state->session.calculated_boundaries());
            auto open_path=path;std::string active_path,passive_path;
            if(context) {
                auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;
                const auto assembly_path=fixture/"context.asmz";
                workspace::Workspace assembly_setup;assembly_setup.add_part(part,state->session.calculated_boundaries(),path);
                assembly_setup.add_assembly(assembly,assembly_path);
                const auto first=assembly_setup.insert_open_part(assembly_id,id,"Active");
                const auto second=assembly_setup.insert_open_part(assembly_id,id,"Passive");
                assembly=assembly_setup.open_assembly(assembly_id)->session.document();
                assembly.find_occurrence(second)->placement={250,0,0,0,0,0};assembly.save(assembly_path);
                active_path=assembly::InstancePath{{first}}.encoded();passive_path=assembly::InstancePath{{second}}.encoded();open_path=assembly_path;
            }
            app::AssemblyWorkspaceWindow window(QString::fromStdString(fixture.string()));window.resize(1366,850);window.show();
            check(window.open_document_path(QString::fromStdString(open_path.string())),"Cannot open solid state fixture");flush();
            if(context)check(window.activate_occurrence_for_test(active_path),"Cannot activate solid state Part occurrence");flush();
            check(activate_test_body(application,window,part.body_history.active_body_id()),"Cannot activate solid state Body");flush();
            window.findChild<QAction*>("applicationModeAction0")->trigger();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Missing solid state workspace");
            auto* straighten=window.findChild<QAction*>("straightenAction");auto* restore=window.findChild<QAction*>("restoreShapeAction");
            check(straighten&&straighten->isEnabled()&&restore&&!restore->isEnabled(),"Solid state toolbar availability incorrect");
            const auto dialog=[&]() -> app::SolidStateDialog* {
                for(auto* item:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::SolidStateDialog*>(item);d&&d->isVisible())return d;
                return nullptr;
            };
            // Compare the calculated solid, not transient construction-Origin
            // carriers recreated by the Assembly presentation on refresh.
            const auto packet=[&]{
                kernel::BodyResult body;body.mesh=view->mesh();
                const auto serialized=document::serialize_body_result(body,false);
                nlohmann::json solid;
                for(const auto* key:{"vertices_binary","triangles_binary","triangle_references"})solid[key]=serialized.at(key);
                return solid.dump();
            };
            const auto input=packet();
            if(!sweep_only&&scenario==3)
                check(std::ranges::any_of(view->mesh().axes,[](const auto& axis){return axis.reference.semantic_key.starts_with("centerline:from:centroid:");}),
                    "Twist centroid axis is missing in the ordinary GUI scene");
            const auto check_state_display=[&] {
                if(!sweep_only&&scenario==3)
                    check(std::ranges::any_of(view->mesh().axes,[](const auto& axis){std::string storage;return kernel::solid_state_source_key(axis.reference.semantic_key,storage).starts_with("centerline:from:centroid:");}),
                        "Twist centroid axis disappeared from the state GUI scene");
                for(const auto& axis:view->mesh().axes)
                    check(axis.reference.instance_path!=active_path||axis.reference.owner_id!=source.id,
                        "Solid state still displays an authored source axis");
                for(const auto& point:view->mesh().points)
                    if(kernel::solid_state_parent(point.reference.semantic_key)&&!point.always_visible&&point.label.empty())
                        check(point.display_owner_id.empty(),"Solid state whole-container highlight reveals topology vertex dots");
            };
            const auto inspect_wire=[&] {
                QTreeWidgetItem* item=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==source.id&&
                       (*it)->data(0,Qt::UserRole+3).toString()=="part-container"){item=*it;break;}
                check(item,"Missing authored source in Tree");
                const auto before=packet();
                tree->itemDoubleClicked(item,0);flush();
                check(packet()==before,"Parameter inspection replaced the current state body");
                check(!view->container_inspection_wire().empty(),"Parameter inspection has no authored blue wire");
                if(!sweep_only) {
                    const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id;});
                    check(dimension!=view->mesh().dimensions.end(),"Authored inspection lost owned Sketch dimensions");
                    if(scenario==4) {
                        const auto p=dimension->witness_first;
                        check(std::hypot(std::hypot(p.x-80,p.y-40),p.z+75)<1e-7,
                            "Authored Sketch dimension did not receive exactly one Body transform");
                    }
                }
                nlohmann::json positions=nlohmann::json::array();
                for(const auto& edge:view->container_inspection_wire())for(const auto& p:edge.points)
                    positions.push_back({p.x,p.y,p.z});
                window.grab().save(QString::fromStdString((fixture/(restore->isEnabled()?"authored-wire-straight.png":"authored-wire-formed.png")).string()));
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
                QApplication::sendEvent(view,&escape);flush();
                check(view->container_inspection_wire().empty(),"Ending parameter inspection left the source wire visible");
                return positions;
            };
            const auto authored_wire=inspect_wire();
            if(!sweep_only&&scenario==3) {
                auto* create=window.findChild<QAction*>("modelTwistAction");
                check(create&&create->isEnabled(),"Model Twist command unavailable");
                const auto commands=window.findChild<QToolBar*>("toolsToolbar")->actions();
                check(commands.indexOf(create)==commands.indexOf(window.findChild<QAction*>("featureShortcut5Action"))+1,
                    "Model Twist is not immediately below Rotation in the command panel");
                create->trigger();flush();
                auto* properties=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(properties&&properties->pending_value().feature.sides[0].operation==document::FeatureSideOperation::Twist,
                    "Model Twist did not initialize its shared profile dialog");
                check(properties->findChild<QCheckBox*>("featureTwistSmooth0")->isVisible(),"Twist transition control missing");
                check(!properties->findChild<QComboBox*>("featureResult")->isEnabled(),"Twist offers unsupported result modes");
                properties->reject();flush();check(packet()==input,"Cancel Twist creation changed the model");
                QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==source.id&&
                    (*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
                check(row,"Twist source Tree row missing");window.show_tree_item_properties(row);flush();
                properties=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(properties&&properties->pending_value().feature.sides[0]==source.feature.sides[0],"Twist Properties changed its parameters");
                check(!view->transient_edges().empty(),"Twist has no lightweight wire preview");
                properties->findChild<QCheckBox*>("featureTwistSmooth0")->setChecked(false);
                properties->findChild<QComboBox*>("featureTwistDirection0")->setCurrentIndex(1);
                properties->reject();flush();check(packet()==input,"Cancel Twist edit changed the model");
                const bool had_undo=window.findChild<QAction*>("undoAction")->isEnabled();
                row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==source.id&&
                    (*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
                check(row,"Twist Tree row missing after Cancel");
                window.show_tree_item_properties(row);flush();
                properties=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(properties,"Twist Properties did not reopen");
                properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                check(packet()==input&&window.findChild<QAction*>("undoAction")->isEnabled()==had_undo,
                    "Unchanged Twist OK changed geometry or created Undo");
                row=nullptr;
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==source.id&&
                    (*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
                check(row,"Twist Tree row missing after unchanged OK");window.show_tree_item_properties(row);flush();
                properties=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(properties,"Disconnected Twist edit did not reopen");
                properties->findChild<QCheckBox*>("featureTwistSmooth0")->setChecked(false);
                properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                const auto edited=packet();check(edited!=input,"Changed disconnected Twist OK did not calculate geometry");
                window.findChild<QAction*>("undoAction")->trigger();flush();check(packet()==input,"Disconnected Twist Undo lost the original result");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(packet()==edited,"Disconnected Twist Redo lost the changed result");
                window.findChild<QAction*>("undoAction")->trigger();flush();
            }
            straighten->trigger();flush();check(dialog(),"Straighten GUI did not open");
            check(dialog()->parentWidget()==&window&&dialog()->windowFlags().testFlag(Qt::SubWindow),"Solid state dialog is not internal");
            check(packet()==input,"Opening Straighten changed its input geometry");
            dialog()->findChild<QCheckBox*>("solidStateIndividual")->setChecked(true);flush();
            const auto tree_path=[](QTreeWidgetItem* row) {
                std::string path;
                for(;row&&path.empty();row=row->parent())path=row->data(0,Qt::UserRole+1).toString().toStdString();
                return path;
            };
            QTreeWidgetItem* source_row{};
            for(QTreeWidgetItemIterator row(tree);*row;++row)if((*row)->data(0,Qt::UserRole+3)=="part-container"&&
                (*row)->data(0,Qt::UserRole).toString().toStdString()==source.id&&
                tree_path(*row)==active_path){source_row=*row;break;}
            check(source_row,"Solid source has no active Tree row");
            tree->clearSelection();tree->setCurrentItem(source_row);source_row->setSelected(true);flush();
            check(dialog()->selected()==std::vector<std::string>{source.id},"Tree did not enter the active solid source");
            auto* sources=dialog()->findChild<QTableWidget*>("solidStateElements");
            sources->cellWidget(0,1)->findChild<QPushButton*>()->click();flush();
            check(dialog()->selected().empty()&&dialog()->selecting(),"Removing the source did not reactivate entry");
            view->fit_all();flush();
            const auto hit=[&]() -> std::optional<QPointF> {
                for(int y=5;y<view->height();y+=8)for(int x=5;x<view->width();x+=8)
                    if(const auto candidates=view->selection_candidates_at({double(x),double(y)});!candidates.empty()) {
                        for(const auto& candidate:candidates)check(candidate.instance_path==active_path,"Solid state offers passive occurrence");
                        return QPointF(x,y);
                    }
                return {};
            };
            const auto click=[&](QPointF point,Qt::MouseButton button) {
                for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,point,point,QPointF(view->mapToGlobal(point.toPoint())),
                        type==QEvent::MouseMove?Qt::NoButton:button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&event);flush();
                }
            };
            view->set_selection_filter(viewer::SelectionFilter::Axes);check(!hit(),"Solid state bypasses user selection filter");
            view->set_selection_filter(viewer::SelectionFilter::All);
            const auto offered=hit();check(offered.has_value(),"No common candidate for Straighten");
            click(*offered,Qt::LeftButton);check(dialog()->selected()==std::vector<std::string>{source.id},"Straighten picked wrong source");
            click(*offered,Qt::MiddleButton);check(!dialog()->selecting()&&dialog()->selected().size()==1,"Short MMB erased or committed selection");
            dialog()->reject();flush();check(!dialog()&&packet()==input,"Cancel changed the input document geometry");
            straighten->trigger();flush();check(dialog(),"Cannot restart Straighten");
            dialog()->findChild<QDoubleSpinBox*>("solidStateCoefficient")->setValue(.9);
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Straighten OK failed");
            check(restore->isEnabled(),"Restore not enabled after Straighten");
            const auto straight_packet=packet();check(straight_packet!=input,"Straighten did not update View");
            check_state_display();
            window.grab().save(QString::fromStdString((fixture/"straight-display.png").string()));
            inspect_from_view(view,tree,source.id,active_path);
            check(packet()==straight_packet,"View double-click replaced the straightened solid");
            if(!sweep_only)check(!view->mesh().dimensions.empty(),"View double-click has no original feature dimensions");
            QKeyEvent end_inspection(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&end_inspection);flush();
            check(inspect_wire()==authored_wire,"Straighten moved the authored inspection wire");
            if(!sweep_only&&scenario==0) {
                const auto point_dialog=[&]{return dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
                auto* create=window.findChild<QAction*>("featureShortcut0Action");
                check(create&&create->isEnabled(),"New placed Feature unavailable after Straighten");
                create->trigger();flush();check(point_dialog(),"New placed Feature dialog did not open");
                point_dialog()->findChild<QTableWidget*>("primitiveReferenceTable")->cellClicked(0,1);flush();view->fit_all();flush();
                std::optional<QPointF> location;
                std::optional<kernel::Vec3> expected;
                std::string selected_owner,selected_key;
                std::size_t cycles=0;
                for(int y=8;y<view->height()-8&&!location;y+=5)for(int x=8;x<view->width()-8&&!location;x+=5) {
                    const QPointF screen(x,y);const auto candidates=view->selection_candidates_at(screen);
                    const auto chosen=std::ranges::find_if(candidates,[&](const auto& candidate) {
                        if(candidate.kind!=viewer::CandidateKind::Face)return false;
                        const auto parent=kernel::solid_state_parent(candidate.semantic_key);
                        return parent&&parent->first==source.id&&parent->second.starts_with("end:from:");
                    });
                    if(chosen==candidates.end())continue;
                    const auto& offered=*chosen;
                    const auto ray=view->ray_at(screen);if(!ray)continue;
                    const auto hit=app::confirmed_face_hit(view->candidate_face_triangles(offered),ray->first,ray->second);
                    if(!hit)continue;
                    location=screen;expected=*hit;selected_owner=offered.owner_id;selected_key=offered.semantic_key;
                    cycles=static_cast<std::size_t>(chosen-candidates.begin());
                }
                window.grab().save(QString::fromStdString((fixture/"new-reference.png").string()));
                check(location.has_value(),"New Feature does not offer the straightened end face");
                QMouseEvent move(QEvent::MouseMove,*location,QPointF(view->mapToGlobal(location->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&move);flush();
                const auto selected=[&] {const auto hovered=view->hovered_candidate();return hovered&&hovered->owner_id==selected_owner&&hovered->semantic_key==selected_key;};
                const auto button=[&](Qt::MouseButton which) {
                    for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                        QMouseEvent event(type,*location,QPointF(view->mapToGlobal(location->toPoint())),which,
                            type==QEvent::MouseButtonPress?which:Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(view,&event);flush();
                    }
                };
                for(std::size_t i=0;i<=cycles&&!selected();++i)button(Qt::RightButton);
                check(selected(),"New Feature could not cycle to the offered end face");
                button(Qt::LeftButton);flush();
                const auto pending=point_dialog()->pending_value();
                if(pending.placement.references.empty()||pending.placement.references.front().owner_id!=selected_owner||
                    pending.placement.references.front().semantic_key!=selected_key) {
                    std::cerr<<"New attachment offered "<<selected_owner<<'/'<<selected_key<<", stored references "<<pending.placement.references.size()<<'\n';
                    for(const auto& ref:pending.placement.references)std::cerr<<ref.owner_id<<'/'<<ref.semantic_key<<'\n';
                }
                check(!pending.placement.references.empty()&&pending.placement.references.front().owner_id==selected_owner&&
                    pending.placement.references.front().semantic_key==selected_key,"New Feature persisted a different face than it offered");
                check(std::hypot(pending.placement.x-expected->x,pending.placement.y-expected->y,pending.placement.z-expected->z)<1e-5,
                    "New Feature snapped back to the formed source face");
                point_dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(!point_dialog(),"New attached Feature did not commit");
                check(window.execute_console_command("save").ok,"Cannot save newly attached Feature");
                const auto saved=document::PartDocument::load(path);
                check(saved.find_container(pending.id)&&saved.find_container(pending.id)->placement.references==pending.placement.references,
                    "New attachment did not preserve its reference in the native file");
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(packet()==straight_packet,"Undo of the new attachment changed the straightened solid");
            }
            if(sweep_only&&treated) {
                check(window.execute_console_command("save").ok,"Cannot save treated straight Sweep");
                std::vector<kernel::BodyResult> calculated;
                const auto saved=document::PartDocument::load(path,&calculated);
                const double length=.5*std::sqrt(11600.)+125*std::asinh(.4);
                const double area=16-(1-std::numbers::pi/4)*.2*.2;
                check(!calculated.empty()&&std::abs(calculated.back().volume-area*length*.9)<1e-4,
                    "GUI Straighten lost the Sweep Fillet cross-section");
                check(saved.find_container(authored_fillet->id)&&*saved.find_container(authored_fillet->id)==*authored_fillet,
                    "GUI Straighten rewrote the Sweep Fillet definition");
            }
            QTreeWidgetItem* edited{};
            for(QTreeWidgetItemIterator row(tree);*row;++row)if((*row)->data(0,Qt::UserRole+3)=="part-container"&&(*row)->text(0)==straighten->text()&&tree_path(*row)==active_path) {edited=*row;break;}
            check(edited,"Straighten missing from Tree");
            window.show_tree_item_properties(edited);flush();check(dialog(),"Tree Properties did not reopen Straighten");
            check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.owner_id==source.id&&face.instance_path==active_path;}),"Editing did not roll back to formed input");
            dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog()&&packet()==straight_packet,"Unchanged OK altered the result");
            restore->trigger();flush();check(dialog(),"Restore GUI did not open");
            dialog()->findChild<QCheckBox*>("solidStateIndividual")->setChecked(true);flush();view->fit_all();
            const auto restored_hit=hit();check(restored_hit.has_value(),"State ancestry cannot be selected for Restore");
            click(*restored_hit,Qt::LeftButton);check(dialog()->selected()==std::vector<std::string>{source.id},"Restore lost source ancestry");
            window.grab().save(QString::fromStdString((fixture/"solid-state-properties.png").string()));
            QMouseEvent middle(QEvent::MouseButtonDblClick,*restored_hit,QPointF(view->mapToGlobal(restored_hit->toPoint())),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(view,&middle);flush();check(!dialog(),"Restore MMB double-click failed");
            check(!restore->isEnabled()&&window.active_occurrence_path_for_test()==active_path,"Restore did not finish in the active context");
            check_state_display();
            window.grab().save(QString::fromStdString((fixture/"restored-display.png").string()));
            check(inspect_wire()==authored_wire,"Restore changed the authored inspection wire");
            if(context)check(std::ranges::any_of(view->mesh().triangle_references,[&](const auto& face){return face.instance_path==passive_path;}),"Passive assembly context disappeared");
            const auto formed_packet=packet();
            window.findChild<QAction*>("undoAction")->trigger();flush();
            check(packet()==straight_packet&&restore->isEnabled(),"GUI Undo did not restore the straight state");
            check_state_display();
            window.findChild<QAction*>("redoAction")->trigger();flush();
            check(packet()==formed_packet&&!restore->isEnabled(),"GUI Redo did not restore the formed state");
            check_state_display();
            check(window.execute_console_command("save").ok,"Cannot save GUI state result");flush();
            std::vector<kernel::BodyResult> reopened_geometry;
            const auto reopened=document::PartDocument::load(path,&reopened_geometry);
            check(reopened.history.size()==(treated?4:3)&&reopened.history.back().feature_kind==document::FeatureKind::RestoreShape,
                "GUI save lost its state history");
            if(authored_fillet)check(reopened.find_container(authored_fillet->id)&&
                *reopened.find_container(authored_fillet->id)==*authored_fillet,"GUI state changed authored Fillet intent");
            check(!reopened_geometry.empty()&&std::abs(reopened_geometry.back().volume-state->session.calculated_boundaries().back().volume)<1e-5,
                "GUI restored volume did not survive native save/reopen");
            check(std::ranges::none_of(reopened_geometry.back().mesh.axes,[&](const auto& axis){return axis.reference.owner_id==source.id;}),
                "Native save/reopen restored an obsolete displayed source axis");
            window.hide();flush();
        }
        // Run the manual-correction spring case last so its independent
        // precision gate cannot conceal planar/spatial continuation results.
        for(int attached_index=0;attached_index<(sweep_only?3:8);++attached_index) {
            const int attached_kind=!sweep_only&&attached_index>=5?
                (attached_index==7?5:attached_index+1):attached_index;
            const bool formed_first=!sweep_only&&attached_kind==3;
            const bool curved_child=!sweep_only&&attached_kind==4;
            const bool carried_sweep=!sweep_only&&attached_kind>=5;
            const bool helical_child=carried_sweep&&attached_kind==5;
            const bool spatial=(sweep_only&&attached_kind==2)||(carried_sweep&&attached_kind==7);
            const bool sweep_child=sweep_only||carried_sweep;
            const bool state_face=!sweep_only&&attached_kind>0;
            const bool spring_face=state_face&&attached_kind==2;
            kernel::OcctKernel kernel;workspace::Workspace setup;
            auto part=document::PartDocument::create_default();
            if(part.body_history.bodies().empty())static_cast<void>(part.body_history.create_body("Body"));
            const auto id=part.document_id;setup.add_part(part,{});setup.activate(id);
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_rectangle(100,0,120,20));
            const auto axis=sketch.add_segment(0,-100,0,100,true);sketch.set_segment_centerline(axis,true);
            auto source=document::PartDocument::create_revolution_container(sketch.id);
            source.revolution.axis_segment_id=axis;source.revolution.angle_degrees=90;
            if(spring_face) {
                source=test_support::sweep_fixture(document::FeatureKind::HelicalSweep);
                source.helical.pitch=5;source.sweep_precision.custom_tolerance=1e-4;
                workspace::commit_sweep(setup,kernel,id,source,workspace::SweepEditMode::Create);
            } else workspace::commit_profile(setup,kernel,id,source,workspace::ProfileEditMode::Create,sketch);
            auto straight=document::PartDocument::create_solid_state_container();straight.solid_state.coefficient=.9;
            if(!formed_first)check(workspace::commit_solid_state(setup,kernel,id,straight),"Cannot prepare GUI attachment state");
            const auto& boundary=setup.open_part(id)->session.calculated_boundaries().back();
            const auto& alias=formed_first?boundary.mesh.original_references:*boundary.solid_state_reference_views.at(straight.id);
            const auto endpoint=std::ranges::find_if(alias.points,[&](const auto& p){return p.reference.owner_id==source.id&&p.reference.semantic_key.starts_with("end:");});
            check(state_face||endpoint!=alias.points.end(),"GUI attachment fixture has no endpoint");
            auto expected=state_face?kernel::Vec3{}:endpoint->position;
            auto profile=sketcher::Sketch::create_default();
            if(state_face)static_cast<void>(profile.add_rectangle(-1,-1.5,1,1.5));
            else static_cast<void>(profile.add_rectangle(0,0,1,1));
            auto child=document::PartDocument::create_extrusion_container(profile.id);child.extrusion.length_forward=2;
            if(curved_child) {
                const auto axis=profile.add_segment(-20,-50,-20,50,true);profile.set_segment_centerline(axis,true);
                child=document::PartDocument::create_revolution_container(profile.id);
                child.revolution.axis_segment_id=axis;child.revolution.angle_degrees=child.revolution.angle_reverse=90;
            }
            if(sweep_only) {
                child=test_support::sweep_fixture(spatial?document::FeatureKind::Sweep3D:
                    attached_kind==0?document::FeatureKind::Sweep2D:document::FeatureKind::HelicalSweep);
                child.sweep_precision.custom_tolerance=1e-4;
            }
            if(carried_sweep)child=test_support::continuation_sweep_fixture(spatial?document::FeatureKind::Sweep3D:
                helical_child?document::FeatureKind::HelicalSweep:document::FeatureKind::Sweep2D);
            document::ConstructionReference reference;reference.owner_id=source.id;
            if(!state_face)reference.semantic_key=endpoint->reference.semantic_key;
            child.placement.references={reference};
            kernel::Vec3 join_tangent;
            if(state_face) {
                const auto& geometry=setup.open_part(id)->session.calculated_boundaries().back().mesh.original_references;
                const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& ref) {
                    const auto parent=formed_first?std::optional{std::pair{ref.owner_id,ref.semantic_key}}:kernel::solid_state_parent(ref.semantic_key);
                    return ref.owner_id==(formed_first?source.id:straight.id)&&parent&&parent->first==source.id&&
                        (parent->second.starts_with("end:from:")||parent->second.starts_with("sweep:cap:end:from:"));
                });
                check(face!=geometry.triangle_references.end(),"GUI fixture lacks the state-owned cap");
                const auto request=setup.open_part(id)->session.document().kernel_operations().front().primitive;
                const auto plan=spring_face?kernel.prepare_straightening(std::get<kernel::Sweep3DRequest>(request),.9):
                    kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(request),.9);
                expected=formed_first?plan.source_end_centroid:plan.straight_end_centroid;
                child.placement.x=expected.x;child.placement.y=expected.y;child.placement.z=expected.z;
                reference.owner_id=face->owner_id;reference.semantic_key=face->semantic_key;reference.supports_offset=true;reference.offset=-0.;
                auto top=reference;top.orientation_only=true;top.orientation_drives_rotation=true;top.orientation_role="top";top.supports_offset=false;
                child.placement.references={reference,top};
                const auto triangle=static_cast<std::size_t>(face-geometry.triangle_references.begin())*3;
                const auto a=geometry.vertices.at(geometry.triangles.at(triangle)),b=geometry.vertices.at(geometry.triangles.at(triangle+1)),c=geometry.vertices.at(geometry.triangles.at(triangle+2));
                const kernel::Vec3 normal{(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
                    (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)};
                auto tangent=plan.start_tangent;
                if(formed_first) {
                    const auto& f=plan.end_transform;const auto t=tangent;
                    tangent={f[1].x*t.x+f[2].x*t.y+f[3].x*t.z,
                        f[1].y*t.x+f[2].y*t.y+f[3].y*t.z,f[1].z*t.x+f[2].z*t.y+f[3].z*t.z};
                }
                join_tangent=tangent;
                if(!curved_child&&!sweep_child) {
                    child.extrusion.direction=normal.x*tangent.x+normal.y*tangent.y+normal.z*tangent.z<0?
                        document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
                    child.extrusion.length_reverse=2;
                }
            }
            if(sweep_child)workspace::commit_sweep(setup,kernel,id,child,workspace::SweepEditMode::Create);
            else workspace::commit_profile(setup,kernel,id,child,workspace::ProfileEditMode::Create,profile);
            if(helical_child) {
                const auto operations=setup.open_part(id)->session.document().kernel_operations();
                const auto operation=std::ranges::find(operations,child.id,&kernel::HistoryOperation::owner_id);
                const auto plan=kernel.prepare_straightening(std::get<kernel::Sweep3DRequest>(operation->primitive),1.);
                child=*setup.open_part(id)->session.document().find_container(child.id);
                const kernel::Vec3 rotation{child.placement.rotation_x,child.placement.rotation_y,child.placement.rotation_z};
                const auto y=document::construction_direction_from_local_axis("y",rotation);
                const auto z=document::construction_direction_from_local_axis("z",rotation);
                const auto dot=[](kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
                child.placement.rotation_offset_x=(std::atan2(dot(plan.start_tangent,y),dot(plan.start_tangent,z))+
                    (dot(join_tangent,z)<0?std::numbers::pi:0))*180/std::numbers::pi;
                child.placement.x+=expected.x-plan.source_start_centroid.x;
                child.placement.y+=expected.y-plan.source_start_centroid.y;
                child.placement.z+=expected.z-plan.source_start_centroid.z;
                workspace::commit_sweep(setup,kernel,id,child,workspace::SweepEditMode::Replace);
                const auto& placement=setup.open_part(id)->session.document().find_container(child.id)->placement;
                expected={placement.x,placement.y,placement.z};
            }
            if(curved_child) {
                const auto operations=setup.open_part(id)->session.document().kernel_operations();
                const auto operation=std::ranges::find(operations,child.id,&kernel::HistoryOperation::owner_id);
                const auto plan=kernel.prepare_straightening(std::get<kernel::RevolutionRequest>(operation->primitive),1.);
                const auto t=plan.start_tangent;
                if(t.x*join_tangent.x+t.y*join_tangent.y+t.z*join_tangent.z<0) {
                    child=*setup.open_part(id)->session.document().find_container(child.id);
                    child.revolution.direction=document::ExtrusionDirection::Reverse;
                    workspace::commit_profile(setup,kernel,id,child,workspace::ProfileEditMode::Replace,profile);
                }
            }
            if(formed_first)check(workspace::commit_solid_state(setup,kernel,id,straight),"Cannot straighten existing GUI attachment");
            auto restore=document::PartDocument::create_solid_state_container(true);
            check(workspace::commit_solid_state(setup,kernel,id,restore),"Cannot restore GUI attachment fixture");
            const auto* prepared=setup.open_part(id);const auto original_child=*prepared->session.document().find_container(child.id);
            const auto fixture=directory/(sweep_only?"solid-attached-sweep-"+std::to_string(attached_kind):carried_sweep?"solid-face-sweep-"+std::to_string(attached_kind):curved_child?"solid-curved-face-properties":formed_first?"solid-formed-face-properties":spring_face?"solid-spring-face-properties":state_face?"solid-state-face-properties":"solid-attached-properties");std::filesystem::create_directories(fixture);
            const auto path=fixture/"attached.prtz";prepared->session.document().save(path,prepared->session.calculated_boundaries());
            app::AssemblyWorkspaceWindow window(QString::fromStdString(fixture.string()));window.resize(1366,850);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open GUI attachment file");flush();
            check(activate_test_body(application,window,prepared->session.document().body_history.active_body_id()),"Cannot activate attached Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
            check(tree&&view,"Attached properties workspace is missing");
            const auto packet=[&]{kernel::BodyResult b;b.mesh=view->mesh();const auto data=document::serialize_body_result(b,false);
                return nlohmann::json{{"v",data.at("vertices_binary")},{"t",data.at("triangles_binary")},{"r",data.at("triangle_references")}}.dump();};
            const auto final_packet=packet();
            const auto edit=[&]() -> ui::PropertiesSubWindow* {
                QTreeWidgetItem* selected{};
                for(QTreeWidgetItemIterator row(tree);*row;++row)if((*row)->data(0,Qt::UserRole+3)=="part-container"&&
                    (*row)->data(0,Qt::UserRole).toString().toStdString()==child.id){selected=*row;break;}
                check(selected,"Attached child has no Tree row");window.show_tree_item_properties(selected);flush();
                for(auto* item:window.findChildren<QDialog*>())if(item->isVisible()) {
                    if(spatial) {if(auto* dialog=dynamic_cast<app::ConstructionPropertiesDialog*>(item))return dialog;}
                    else if(sweep_child) {if(auto* dialog=dynamic_cast<app::SweepPlacementDialog*>(item))return dialog;}
                    else if(auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(item))return dialog;
                }
                throw std::runtime_error("Attached child Properties did not open");
            };
            auto* dialog=edit();
            const auto pending=spatial?dynamic_cast<app::ConstructionPropertiesDialog*>(dialog)->pending_sweep_value():
                sweep_child?dynamic_cast<app::SweepPlacementDialog*>(dialog)->pending:
                dynamic_cast<app::PrimitivePropertiesDialog*>(dialog)->pending_value();
            window.grab().save(QString::fromStdString((fixture/"attached-properties.png").string()));
            const std::array<double,3> coordinates{expected.x,expected.y,expected.z};
            auto fields=dialog->findChildren<QDoubleSpinBox*>("primitiveTranslation");
            if(spatial)for(const auto* name:{"constructionX","constructionY","constructionZ"})fields.push_back(dialog->findChild<QDoubleSpinBox*>(name));
            else if(sweep_child)for(int i=0;i<3;++i)fields.push_back(dialog->findChild<QDoubleSpinBox*>(QString("sweepTranslation%1").arg(i)));
            check(fields.size()==3,"Missing shared placement coordinates");
            for(std::size_t i=0;i<fields.size();++i) {
                const auto* field=fields[i];check(field,"Missing Sweep placement field");
                const double quantum=zima::ui::numeric_display_quantum(field);
                check(std::abs(field->value()-coordinates[i])<=quantum*.50001,
                    "GUI Properties displayed a coordinate outside its boundary's rounding interval");
            }
            check(pending.placement.references==original_child.placement.references,"GUI Properties rewrote attachment identity");
            if(carried_sweep&&pending.placement!=original_child.placement) {
                auto changed=prepared->session.document();changed.find_container(child.id)->placement=pending.placement;
                std::cerr<<"Sweep "<<attached_kind<<" pending placement diff: "<<nlohmann::json::diff(
                    prepared->session.document().serialized(),changed.serialized()).dump()<<'\n';
                check(false,"Sweep Properties changed the authored attachment correction");
            }
            if(carried_sweep&&!spatial) {
                const std::array corrections{original_child.placement.rotation_offset_x,
                    original_child.placement.rotation_offset_y,original_child.placement.rotation_offset_z};
                for(int i=0;i<3;++i) {
                    const auto* field=dialog->findChild<QDoubleSpinBox*>(QString("sweepRotationOffset%1").arg(i));
                    check(field&&std::abs(field->value()-corrections[i])<=zima::ui::numeric_display_quantum(field)*.50001,
                        "Sweep Properties displayed an incorrect angular correction");
                }
                auto* editor=dynamic_cast<app::SweepPlacementDialog*>(dialog);editor->changed();flush();
                check(editor->pending.placement==original_child.placement,
                    "Sweep preview refresh changed the precise authored placement");
            }
            check(packet()!=final_packet,"Attached Properties did not roll back before the child");
            window.grab().save(QString::fromStdString((fixture/"attached-properties.png").string()));
            dialog->reject();flush();check(packet()==final_packet,"Attached Properties Cancel changed final body");
            auto* undo=window.findChild<QAction*>("undoAction");check(undo,"Missing Undo action");const bool had_undo=undo->isEnabled();
            dialog=edit();dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(spatial&&(packet()!=final_packet||undo->isEnabled()!=had_undo)) {
                auto candidate=prepared->session.document();*candidate.find_container(child.id)=pending;
                std::cerr<<"3D Sweep unchanged editor diff: "<<nlohmann::json::diff(
                    prepared->session.document().serialized(),
                    candidate.serialized()).dump()<<'\n';
            }
            check(packet()==final_packet&&undo->isEnabled()==had_undo,"Unchanged attached Properties changed geometry or Undo");
            check(window.execute_console_command("save").ok,"Cannot save attached Properties result");
            std::vector<kernel::BodyResult> cached;const auto saved=document::PartDocument::load(path,&cached);
            if(*saved.find_container(child.id)!=original_child) {
                auto changed=prepared->session.document();*changed.find_container(child.id)=*saved.find_container(child.id);
                std::cerr<<"Attached Properties persisted diff: "<<nlohmann::json::diff(
                    prepared->session.document().serialized(),changed.serialized()).dump()<<'\n';
            }
            check(*saved.find_container(child.id)==original_child&&saved.history.size()==4,"Attached Properties changed persisted history");
            std::cout<<"Attached Properties kind "<<attached_kind<<": rollback, Cancel, unchanged OK and native persistence passed\n";
            window.hide();flush();
        }
        if(!sweep_only)for(const auto& source_path:qEnvironmentVariable("ZIMA_VERIFY_AUTHORED_MODELS").split(';',Qt::SkipEmptyParts)) {
            std::vector<kernel::BodyResult> calculated;
            const auto part=document::PartDocument::load(source_path.toStdString(),&calculated);
            check(!calculated.empty(),"User inspection model has no calculated geometry");
            const auto fixture=directory/("authored-user-"+std::filesystem::path(source_path.toStdString()).stem().string());
            std::filesystem::create_directories(fixture);
            const auto path=fixture/"source.prtz";part.save(path,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(fixture.string()));window.resize(1366,850);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open user inspection model");flush();
            check(activate_test_body(application,window,part.body_history.active_body_id()),"Cannot activate user inspection Body");flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(view&&tree,"Missing user inspection workspace");
            const auto packet=[&] {
                kernel::BodyResult body;body.mesh=view->mesh();const auto data=document::serialize_body_result(body,false);
                return nlohmann::json{data.at("vertices_binary"),data.at("triangles_binary"),data.at("triangle_references")}.dump();
            };
            const auto before=packet();const bool had_undo=window.findChild<QAction*>("undoAction")->isEnabled();
            const auto wire_points=[&] {
                nlohmann::json points=nlohmann::json::array();
                for(const auto& edge:view->container_inspection_wire())for(const auto& p:edge.points)points.push_back({p.x,p.y,p.z});
                return points;
            };
            std::map<std::string,nlohmann::json> original_wires;
            std::size_t inspected{};
            for(const auto& feature:part.history) {
                if(feature.feature_kind!=document::FeatureKind::Feature&&feature.feature_kind!=document::FeatureKind::Extrusion&&
                   feature.feature_kind!=document::FeatureKind::Revolution)continue;
                QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container"&&
                    (*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
                check(row,"User source feature has no Tree row");tree->itemDoubleClicked(row,0);flush();
                check(!view->container_inspection_wire().empty(),"User feature has no original inspection wire");
                original_wires.emplace(feature.id,wire_points());
                check(packet()==before&&window.findChild<QAction*>("undoAction")->isEnabled()==had_undo,
                    "User inspection changed calculated body or Undo");
                window.grab().save(QString::fromStdString((fixture/("source-wire-"+std::to_string(inspected++)+".png")).string()));
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
                check(view->container_inspection_wire().empty(),"User source wire survived Escape");
                if(std::ranges::any_of(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Straighten;})) {
                    inspect_from_view(view,tree,feature.id,{});
                    check(wire_points()==original_wires.at(feature.id),
                        "User View double-click differs from the authored wire");
                    check(packet()==before,"User View inspection changed the solid");
                    QApplication::sendEvent(view,&escape);flush();
                }
            }
            check(inspected>0,"User model has no profile feature to inspect");
            for(const auto& feature:part.history)if(feature.feature_kind==document::FeatureKind::Feature&&
                feature.feature.sides[0].operation==document::FeatureSideOperation::Twist&&
                std::ranges::any_of(feature.placement.references,[](const auto& ref){return kernel::solid_state_parent(ref.semantic_key).has_value();})) {
                QTreeWidgetItem* row{};
                for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+3)=="part-container"&&
                    (*it)->data(0,Qt::UserRole).toString().toStdString()==feature.id){row=*it;break;}
                check(row,"State-attached Twist has no Tree row");window.show_tree_item_properties(row);flush();
                auto* properties=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                check(properties&&properties->pending_value().placement.references==feature.placement.references,
                    "State-attached Twist Properties lost its references");
                auto* reference_table=properties->findChild<QTableWidget*>("primitiveReferenceTable");
                check(reference_table,"Twist Properties has no shared reference table");reference_table->cellClicked(0,1);flush();
                std::set<std::string> current_states;
                for(const auto& face:view->mesh().triangle_references)if(kernel::solid_state_parent(face.semantic_key))current_states.insert(face.owner_id);
                check(!current_states.empty(),"Twist editor did not display its state input boundary");
                view->set_selection_filter(viewer::SelectionFilter::Faces);view->fit_all();flush();
                std::size_t offered{};
                for(int y=5;y<view->height();y+=14)for(int x=5;x<view->width();x+=14)
                    for(const auto& candidate:view->selection_candidates_at({double(x),double(y)}))if(candidate.kind==viewer::CandidateKind::Face) {
                        ++offered;
                        check(current_states.contains(candidate.owner_id),"Twist editor offers a phantom face from an obsolete solid state");
                    }
                check(offered>0,"Twist editor offered no current state faces");
                window.grab().save(QString::fromStdString((fixture/"state-reference-properties.png").string()));
                properties->reject();flush();view->set_selection_filter(viewer::SelectionFilter::All);
                check(packet()==before,"State-attached Twist Cancel did not restore the body");
                auto* straighten=window.findChild<QAction*>("straightenAction");check(straighten&&straighten->isEnabled(),"User chain cannot Straighten");
                straighten->trigger();flush();app::SolidStateDialog* state_dialog{};
                for(auto* d:window.findChildren<QDialog*>())if(auto* state=dynamic_cast<app::SolidStateDialog*>(d);state&&state->isVisible())state_dialog=state;
                check(state_dialog,"User chain Straighten did not open");state_dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                check(packet()!=before,"User chain Straighten did not change the displayed body");
                window.grab().save(QString::fromStdString((fixture/"state-attached-straight.png").string()));
                window.findChild<QAction*>("undoAction")->trigger();flush();check(packet()==before,"User chain Undo did not restore the displayed body");
                std::cout<<"State-attached user Twist: Properties, current face candidates, Cancel, Straighten and Undo passed\n";
            }
            check(window.execute_console_command("save").ok,"Cannot save inspected user-model copy");
            const auto reopened=document::PartDocument::load(path);
            check(reopened.history==part.history&&std::ranges::equal(reopened.sketches,part.sketches,{},
                    [](const auto& s){return s.serialized();},[](const auto& s){return s.serialized();}),
                "Inspection modified authored features, placement or Sketches");
            if(std::ranges::any_of(part.history,[](const auto& f){return f.extrusion.sheet_cut;})) {
                window.findChild<QAction*>("applicationModeAction2")->trigger();flush();
                const bool already_unfolded=std::ranges::any_of(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::Unbend;});
                if(already_unfolded)check(window.execute_console_command("regenerate").ok,"Cannot regenerate user sheet-model copy");
                if(!already_unfolded) {
                auto* unbend=window.findChild<QAction*>("unbendAction");check(unbend&&unbend->isEnabled(),"User sheet cannot Unbend");
                unbend->trigger();flush();app::SheetStateDialog* dialog{};
                for(auto* d:window.findChildren<QDialog*>())if(auto* state=dynamic_cast<app::SheetStateDialog*>(d);state&&state->isVisible())dialog=state;
                check(dialog,"User Unbend did not open");dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
                }
                const auto unfolded=packet();check(already_unfolded||unfolded!=before,"User Unbend did not change the calculated body");
                for(const auto& f:part.history)if(f.extrusion.sheet_cut) {
                    const auto original=std::ranges::find_if(calculated.back().mesh.original_references.axes,[&](const auto& a){return a.reference.owner_id==f.id&&a.reference.semantic_key=="axis:primary";});
                    if(original==calculated.back().mesh.original_references.axes.end())continue;
                    const auto displayed=std::ranges::find_if(view->mesh().axes,[&](const auto& a){return a.reference.semantic_key.starts_with("sheet-state:from:"+f.id+":axis:primary:region:");});
                    check(displayed!=view->mesh().axes.end(),"Unfolded cut axis is not displayed in GUI");
                    check(std::ranges::none_of(view->mesh().axes,[&](const auto& a){return a.reference==original->reference;}),
                        "The original cut axis is still displayed after Unbend");
                    check(std::hypot(displayed->point.x-original->point.x,displayed->point.y-original->point.y,displayed->point.z-original->point.z)>1e-4,
                        "The unfolded cut axis did not move");
                    const auto retained=std::ranges::find(view->mesh().original_references.axes,original->reference,&kernel::ViewerAxis::reference);
                    check(retained!=view->mesh().original_references.axes.end()&&retained->point==original->point&&retained->direction==original->direction,
                        "GUI Unbend moved the authored cut axis");
                    view->set_selection_filter(viewer::SelectionFilter::Axes);view->fit_all();flush();
                    for(int y=5;y<view->height();y+=8)for(int x=5;x<view->width();x+=8)
                        for(const auto& offered:view->selection_candidates_at({double(x),double(y)}))
                            check(offered.owner_id!=f.id||offered.semantic_key!="axis:primary","Hidden source axis is still offered by the common picker");
                    view->set_selection_filter(viewer::SelectionFilter::All);
                }
                check(window.execute_console_command("save").ok,"Cannot save unfolded user-model copy");
                for(const auto& f:part.history)if(f.extrusion.sheet_cut) {
                    window.show_parameter_dimensions(f.id);flush();
                    check(packet()==unfolded,"User cut inspection changed the unfolded body");
                    const auto points=wire_points();const auto& original=original_wires.at(f.id);
                    // Native regeneration can remove a duplicate closing
                    // circle sample. Compare both point sets geometrically,
                    // so a different seam sample count cannot hide movement.
                    const auto equivalent=[](const auto& a,const auto& b) {
                        if(a.empty()||b.empty())return false;
                        for(const auto& p:a) {
                            double nearest=std::numeric_limits<double>::infinity();
                            for(const auto& q:b)nearest=std::min(nearest,std::hypot(
                                std::hypot(p[0].template get<double>()-q[0].template get<double>(),
                                           p[1].template get<double>()-q[1].template get<double>()),
                                p[2].template get<double>()-q[2].template get<double>()));
                            if(nearest>1e-7)return false;
                        }
                        return true;
                    };
                    check(equivalent(points,original)&&equivalent(original,points),"User Unbend moved the authored cut wire");
                    window.grab().save(QString::fromStdString((fixture/"cut-wire-unfolded.png").string()));
                    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
                }
                check(window.execute_console_command("save").ok,"Cannot save unfolded user-model copy");
                const auto unfolded_saved=document::PartDocument::load(path);
                for(const auto& f:part.history)check(unfolded_saved.find_container(f.id)&&*unfolded_saved.find_container(f.id)==f,
                    "User Unbend changed an original feature or Origin");
                check(std::ranges::equal(unfolded_saved.sketches,part.sketches,{},
                    [](const auto& s){return s.serialized();},[](const auto& s){return s.serialized();}),
                    "User Unbend changed an original Sketch");
                std::cout<<"User sheet cut: Unbend, identical authored wire and unchanged definitions passed\n";
            }
            std::cout<<"Authored wire: "<<source_path.toStdString()<<", "<<inspected<<" features, unchanged geometry/definitions/Undo passed\n";
            window.hide();flush();
        }
        if(sweep_only)std::cout<<"Sweep solid states GUI: 2D spline, 3D spline, H-Sweep, picking, Cancel, rollback, unchanged OK, MMB and native reopening passed\n";
        else std::cout<<"Solid states GUI: common picking, active occurrence, Cancel, rollback, unchanged OK, attached Properties and MMB passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Solid state GUI: "<<e.what()<<'\n';return 1;}
}
