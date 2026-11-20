int verify_profile_point_order_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
            if(!result.ok)throw std::runtime_error(result.message);flush();return result.data;
        };
        window.resize(1450,1050);window.show();flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        check(tree&&view,"Profile order View/Tree missing");
        const auto click=[&](QPointF at) {
            for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            }
        };
        const auto pick=[&](const std::string& point) {
            std::optional<QPointF> hit;
            for(int y=8;y<view->height()-8&&!hit;y+=3)for(int x=8;x<view->width()-8&&!hit;x+=3) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                if(!candidates.empty()&&candidates.front().semantic_key.ends_with(":"+point))hit=QPointF(x,y);
            }
            check(hit.has_value(),"3D Sweep profile point not offered in the View");click(*hit);
        };
        for(const bool open:{false,true}) {
            auto feature=test_support::sweep_fixture(document::FeatureKind::Sweep3D);
            feature.sweep3d.profiles.clear();feature.sweep3d.result_type=document::ProfileResultType::Surface;
            feature.placement.x=13;feature.placement.y=-7;
            feature.placement.rotation_x=feature.placement.absolute_rotation_x=23;
            feature.placement.rotation_z=feature.placement.absolute_rotation_z=37;
            const auto stations=document::curve3d_route(feature.sweep3d.path).stations;
            for(const auto& station:stations) {
                auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
                static_cast<void>(sketch.add_segment(-3,-2,3,-2));
                static_cast<void>(sketch.add_segment(3,-2,3,2));
                if(!open) {
                    static_cast<void>(sketch.add_segment(3,2,-3,2));
                    static_cast<void>(sketch.add_segment(-3,2,-3,-2));
                }
                feature.sweep3d.profiles.push_back({kernel::make_stable_id(),station.point_id,sketch.id,sketch.serialized(),station.incoming});
            }
            auto part=document::PartDocument::create_default();part.history={feature};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Translated profile ordering");
            auto placed=*graph.find(body);placed.scope.placement.references.clear();
            placed.scope.placement.x=-9;placed.scope.placement.z=17;
            placed.scope.placement.rotation_y=placed.scope.placement.absolute_rotation_y=19;graph.update_body(placed);
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            kernel::OcctKernel engine;const auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
            check(calculated.back().calculation_errors.empty(),"Profile ordering fixture failed to calculate");
            const auto path=directory/(open?"open-profile-order.prtz":"closed-profile-order.prtz");part.save(path,calculated);
            check(window.open_document_path(QString::fromStdString(path.string())),"Profile ordering fixture cannot open");flush();
            run("body.activate",{{"body",body}});
            const auto row=[&] {
                for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature.id&&
                    (*i)->data(0,Qt::UserRole+3)=="part-container")return *i;
                throw std::runtime_error("Profile ordering Tree row missing");
            };
            const auto editor=[&] {
                for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
                    if(auto* p=dynamic_cast<ConstructionPropertiesDialog*>(candidate))return p;
                throw std::runtime_error("3D Sweep Properties missing");
            };
            window.show_tree_item_properties(row());flush();auto* dialog=editor();
            const auto expected_preview=part.place_body_mesh(document::sweep3d_preview_mesh(*part.find_container(feature.id),true),body);
            for(const auto& expected:expected_preview.points)if(expected.reference.semantic_key.starts_with("profile-point:")) {
                const auto actual=std::ranges::find_if(view->mesh().points,[&](const auto& point){return point.reference.owner_id==expected.reference.owner_id&&point.reference.semantic_key==expected.reference.semantic_key;});
                check(actual!=view->mesh().points.end()&&std::hypot(actual->position.x-expected.position.x,actual->position.y-expected.position.y,actual->position.z-expected.position.z)<1e-6,
                    "3D Sweep point-order preview lost its feature or Body transform");
            }
            const auto initial=dialog->pending_sweep_value();const auto unchanged=run("documents");
            const auto mapping=document::sweep3d_profile_correspondence(sketcher::Sketch::from_serialized(initial.sweep3d.profiles.front().sketch_serialized),{},open);
            auto* table=dialog->findChild<QTableWidget*>("sweep3DProfiles");
            check(table&&table->rowCount()==2,"3D Sweep lost endpoint profile rows");
            for(const bool reverse:{false,true}) {
                qobject_cast<QPushButton*>(table->cellWidget(0,2))->click();flush();
                auto* order=dynamic_cast<SweepPointOrderDialog*>(window.findChild<QDialog*>("sweepPointOrderDialog"));
                check(order&&order->isVisible(),"3D Sweep point-order editor missing");
                view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();flush();
                QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
                pick(open&&reverse?mapping.point_ids.back():mapping.point_ids.front());
                if(!open)pick(reverse?mapping.point_ids.back():mapping.point_ids[1]);
                const auto profile=dialog->pending_sweep_value().sweep3d.profiles.front();
                check(profile.correspondence_start_point_id==(open&&reverse?mapping.point_ids.back():mapping.point_ids.front())&&
                    (open||profile.correspondence_reversed==reverse),"3D Sweep View clicks lost the selected endpoint/direction");
                check(std::ranges::count_if(view->transient_edges(),[](const auto& edge){return edge.reference.semantic_key.starts_with("profile-correspondence-link:");})==mapping.point_ids.size(),
                    "3D Sweep ordering lost adjacent profile links");
                window.grab().save(QString::fromStdString((directory/(open?"open-order-view.png":"closed-order-view.png")).string()));
                order->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                check(dialog->pending_sweep_value()==initial&&run("documents")==unchanged,"3D Sweep ordering Cancel changed the document or draft");
            }
            dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(!view->mesh().triangles.empty()&&run("documents")==unchanged,"3D Sweep Cancel lost visible input or changed revision");
            run("close");
        }
        std::cout<<"3D Sweep profile View clicks: closed first/adjacent directions, open endpoints, translated/rotated feature and Body, links and Cancel passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
