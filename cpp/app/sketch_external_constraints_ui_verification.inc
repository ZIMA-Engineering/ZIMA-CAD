int verify_sketch_external_constraints_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    using K=sketcher::ConstraintKind;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(int scenario:{0,1,2,3,4,5,6}) {
            std::cout<<"Starting external constraint GUI scenario "<<scenario<<std::endl;
            const auto supplied=qEnvironmentVariable("ZIMA_VERIFY_EXTERNAL_CONSTRAINT_FILE");
            // The private Part retains calculated boundaries for its actual
            // Sketch. Endpoint-creation variants use the generated fixture,
            // without rewriting a calculated boundary to fabricate an input.
            if(!supplied.isEmpty()&&scenario>=4)continue;
            auto part=document::PartDocument::create_default();
            std::vector<kernel::BodyResult> calculated;
            if(!supplied.isEmpty())part=document::PartDocument::load(std::filesystem::path(supplied.toStdWString()),&calculated);
            else {
                auto source_feature=document::PartDocument::create_sketch_container();
                auto feature=document::PartDocument::create_sketch_container();
                auto source=sketcher::Sketch::create_default();source.owner_container_id=source_feature.id;
                const auto source_line=source.add_segment(-50,10,50,10);
                const auto source_vertical=source.add_segment(-20,-20,-20,20);
                const auto source_arc=source.add_arc(3,3,0,3,3,0);
                auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
                const auto ab=sketch.add_segment(0,3,-8.31,3),bc=sketch.add_segment(-8.31,3,-8.31,10);
                for(const auto& edge:source.viewer_mesh().edges) {
                    if(edge.reference.semantic_key!="segment:"+source_line&&edge.reference.semantic_key!="arc:"+source_arc&&
                        edge.reference.semantic_key!="segment:"+source_vertical)continue;
                    auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
                    ref.source_document_id=part.document_id;ref.source_owner_id=source.id;ref.source_semantic_key=edge.reference.semantic_key;
                    ref.infinite=edge.reference.semantic_key.starts_with("segment:");ref.exact_spline=edge.exact_spline;
                    for(const auto& p:edge.points)ref.cached_points.push_back(sketch.local_point(p));
                    sketch.add_external_reference(ref);
                }
                auto point=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Point);
                point.source_document_id=part.document_id;point.source_owner_id=source.id;
                point.source_semantic_key="point:"+source.arcs.front().start_point_id;point.cached_points={{0,3}};
                sketch.add_external_reference(point);
                const auto horizontal=std::ranges::find_if(sketch.external_references,[](const auto& r){return r.infinite&&std::abs(r.cached_points.front()[1]-10)<1e-8;});
                check(horizontal!=sketch.external_references.end(),"Generated horizontal reference missing");
                const auto horizontal_id=horizontal->id;
                static_cast<void>(sketch.add_point_reference_constraint(sketch.segments[0].first_point_id,point.id));
                static_cast<void>(sketch.add_point_pair_constraint(sketch.segments[0].first_point_id,sketch.segments[0].second_point_id,K::Horizontal));
                static_cast<void>(sketch.add_point_on_line_constraint(sketch.segments[1].second_point_id,horizontal_id));
                static_cast<void>(sketch.add_point_pair_constraint(sketch.segments[1].second_point_id,sketch.segments[1].first_point_id,K::Vertical));
                const auto circle=sketch.add_circle(-14.113200233087127,-3.5749458189590086,13.633686118333486);
                const auto rim=sketch.add_point(-12.848983801629856,10,1e-9);
                static_cast<void>(sketch.add_point_on_circle_constraint(rim,circle));
                static_cast<void>(sketch.add_point_on_line_constraint(rim,horizontal_id));
                part.history={source_feature,feature};part.sketches={source,sketch};
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("External constraints"));
                graph.insert({document::PartHistoryKind::Feature,source_feature.id});graph.insert({document::PartHistoryKind::Feature,feature.id});
                part.set_body_history(graph);part.resolve_constructions();
                (void)ab;(void)bc;
            }
            const auto target=std::ranges::find_if(part.sketches,[](const auto& s){return s.external_references.size()>=3&&!s.circles.empty()&&s.segments.size()>=2;});
            check(target!=part.sketches.end(),"External constraint target Sketch missing");
            const auto sketch_index=std::size_t(target-part.sketches.begin());
            auto& editable=*target;
            if(scenario>=4) {
                editable.remove_geometry(editable.segments[1].id);
                editable.remove_geometry(editable.circles.front().id);
            }
            const auto sketch=editable;
            const auto horizontal=std::ranges::find_if(sketch.external_references,[](const auto& r){return r.infinite&&r.cached_points.size()>=2&&std::abs(r.cached_points.front()[1]-10)<1e-8;});
            const auto arc=std::ranges::find_if(sketch.external_references,[](const auto& r){return sketcher::external_reference_circle(r).has_value();});
            check(horizontal!=sketch.external_references.end()&&arc!=sketch.external_references.end(),"External test references missing");
            const auto path=directory/("external-constraints-"+std::to_string(scenario)+".prtz");part.save(path,calculated);
            const auto* owner=part.body_owner_for_object(sketch.owner_container_id);
            check(owner,"External constraint Body hierarchy missing");
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"External constraint fixture failed to open");flush();
            check(activate_test_body(application,window,owner->scope.id),"External constraint Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"External constraint Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            if(!finish||!finish->isEnabled()) {
                auto* open=window.findChild<QPushButton*>("sketchOpenButton");
                if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
                check(open,"External constraint Sketch opening control missing");open->click();flush();
            }
            check(finish&&finish->isEnabled(),"External constraint Sketch editor did not open");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"External constraint View missing");
            view->fit_points({sketch.world_point(-35,-22),sketch.world_point(25,22)});flush();
            auto camera=view->camera_state();camera[4]=40;camera[5]=camera[6]=0;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"External constraint camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"External constraint camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            if(scenario<=1) {
                window.findChild<QAction*>("sketchTangentAction")->trigger();flush();
                const auto& ref=scenario==0?*horizontal:*arc;
                const auto at=scenario==0?std::array{20.,10.}:ref.cached_points[ref.cached_points.size()/2];
                mouse(screen(at[0],at[1]),QEvent::MouseMove,Qt::NoButton);
                check(view->hovered_candidate()&&view->hovered_candidate()->kind==viewer::CandidateKind::SketchExternalReference,"Tangent common picker did not offer external line/arc");
                click(screen(at[0],at[1]));
                const auto& circle=sketch.circles.front();const auto* center=sketch.find_point(circle.center_point_id);
                click(screen(center->x-circle.radius,center->y));
            } else if(scenario<=3) {
                window.findChild<QAction*>("sketchEqualAction")->trigger();flush();
                click(scenario==2?screen(-4,3):screen(-8.31,6));
                click(scenario==2?screen(-8.31,6):screen(-4,3));
            } else {
                if(scenario>=5) {
                    window.findChild<QAction*>("sketchConstraintsAction")->trigger();flush();
                    auto* dialog=window.findChild<QDialog*>("sketchConstraintsDialog");check(dialog,"Automatic constraints dialog missing");
                    dialog->findChild<QPushButton*>("sketchPerpendicularActionAutomatic")->setChecked(false);
                    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                }
                window.findChild<QAction*>("sketchSegmentAction")->trigger();flush();
                click(screen(-8.31,3));click(scenario==6?screen(-20,3):screen(-8.31,10));
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            QDialog* properties{};
            properties=window.findChild<QDialog*>("featurePropertiesDialog");
            if(!properties)properties=window.findChild<QDialog*>("sketchPropertiesDialog");
            if(!properties)for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&d->findChild<QDialogButtonBox*>())properties=d;
            check(properties,"External constraint test did not return to Properties");
            properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            const auto saved=window.execute_console_command("save");
            if(!saved.ok){window.grab().save(QString::fromStdString((directory/"external-constraint-failure.png").string()));throw std::runtime_error("External constraint save failed: "+saved.code+": "+saved.message);}
            const auto reopened=document::PartDocument::load(path);
            const auto result=reopened.sketches[sketch_index];
            if(scenario<=1)check(std::ranges::any_of(result.constraints,[](const auto& c){return c.kind==K::Tangent;}),"External Tangent was not committed by mouse selection");
            else if(scenario<=3) {
                check(std::ranges::any_of(result.constraints,[](const auto& c){return c.kind==K::EqualLength;}),"External sliding EqualLength was not committed");
                check(std::abs(result.find_point(result.segments[0].second_point_id)->x+7)<1e-8,"GUI EqualLength did not produce 7 mm arms");
            } else {
                check(result.segments.size()==2,"Endpoint line was rejected");
                const auto expected=scenario==4?K::Perpendicular:scenario==5?K::Vertical:K::Horizontal;
                check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==expected&&
                    (expected==K::Perpendicular?c.second_geometry_id==result.segments.back().id:
                        c.first_point_id==result.segments.back().first_point_id&&c.second_point_id==result.segments.back().second_point_id);}),"Endpoint C lost its P/H/V relation to the first point");
            }
            check(result.external_references==sketch.external_references,"Sketch constraint changed its external source geometry");
            check(window.execute_console_command("undo").ok&&window.execute_console_command("save").ok,"External constraint Undo failed");
            check(document::PartDocument::load(path).sketches[sketch_index].serialized()==sketch.serialized(),"External constraint Undo changed the original Sketch");
            check(window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"External constraint Redo failed");
            check(document::PartDocument::load(path).sketches[sketch_index].serialized()==result.serialized(),"External constraint Redo did not restore the saved Sketch");
            std::cout<<"External constraint GUI scenario "<<scenario<<" passed\n";
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
