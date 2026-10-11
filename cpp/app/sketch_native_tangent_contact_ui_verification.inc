int verify_sketch_native_tangent_contact_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        for(unsigned scenario=unsigned(std::max(0,qEnvironmentVariableIntValue("ZIMA_NATIVE_CONTACT_SCENARIO_START")));scenario<6;++scenario) {
            std::cout<<"Native contact GUI scenario="<<scenario<<std::endl;
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            std::optional<sketcher::SketchDimension> tested_dimension;
            if(scenario<2 || scenario>=4) {
                if(scenario==5)static_cast<void>(sketch.add_arc(-8,-5,-8+4*std::cos(.1),-5+4*std::sin(.1),-8+4*std::cos(6.1),-5+4*std::sin(6.1)));
                else static_cast<void>(sketch.add_circle(-8,-5,4));
                if(scenario<2)static_cast<void>(sketch.add_circle(-1.7,-5,2));
            } else {
                const auto input=std::filesystem::path(qEnvironmentVariable("ZIMA_NATIVE_CONTACT_SOURCE").toStdString());
                auto source=sketcher::Sketch::create_default();
                if(input.extension()==".json") {
                    std::ifstream file(input);check(bool(file),"Native arc GUI fixture missing");source=sketcher::Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(file),{}));
                } else {
                    const auto part=document::PartDocument::load(input);
                    const auto found=std::ranges::find_if(part.sketches,[](const auto& s){return s.arcs.size()==2&&s.circles.empty();});
                    check(found!=part.sketches.end(),"FORM-KOULE native arc fixture missing");source=*found;
                }
                sketch.points=source.points;sketch.arcs=source.arcs;sketch.constraints=source.constraints;
            }
            if(scenario<4) {
                tested_dimension=scenario<2?(scenario==0?sketch.create_circle_radius_dimension(sketch.circles[1].id):sketch.create_circle_diameter_dimension(sketch.circles[1].id)):
                    scenario==2?sketch.create_arc_radius_dimension(sketch.arcs[0].id):sketch.create_arc_diameter_dimension(sketch.arcs[1].id);
                sketch.apply_dimension(*tested_dimension);
            }
            const auto original_points=sketch.points.size(),original_constraints=sketch.constraints.size();
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Native tangent contact");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("native-contact-"+std::to_string(scenario)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Native contact fixture did not open");flush();
            check(activate_test_body(application,window,body),"Native contact Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto open=[&] {
                QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)
                    if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"Native contact Sketch row missing");window.show_tree_item_properties(row);flush();
                auto* button=window.findChild<QPushButton*>("sketchOpenButton");check(button,"Native contact Sketch open button missing");button->click();flush();
                QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            };open();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Native contact View missing");
            view->fit_points({sketch.world_point(-20,-16),sketch.world_point(20,10)});flush();
            auto camera=view->camera_state();camera[4]=40;camera[5]=camera[6]=0;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Native camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Native camera not facing Sketch");return *q;};
            const auto screen=[&](double x,double y) {
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);
            };
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();
            };
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove,Qt::NoButton);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto pick=[&](const std::string& key) {
                view->clear_selection();std::optional<QPointF> pos;unsigned active{};
                for(int y=16;y<view->height()-16&&!pos;y+=3)for(int x=16;x<view->width()-16;x+=3) {
                    const auto offered=view->selection_candidates_at(QPointF(x,y));
                    for(unsigned n=0;n<offered.size();++n)if(offered[n].owner_id==sketch.id&&offered[n].semantic_key==key){pos=QPointF(x,y);active=n;break;}
                    if(pos)break;
                }
                if(!pos)throw std::runtime_error("Native common picker candidate missing: "+key);
                mouse(*pos,QEvent::MouseMove,Qt::NoButton);
                for(unsigned n=0;n<active;++n){mouse(*pos,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Native contact hover/click mismatch");
                mouse(*pos,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::LeftButton);return *pos;
            };
            const auto count=[&](const char* label){return std::ranges::count_if(view->mesh().constraint_markers,[&](const auto& m){return m.label==label;});};
            const auto before_t=count("T"),before_c=count("C");
            if(scenario>=4) {
                window.findChild<QAction*>("sketchCircleAction")->trigger();flush();
                // Keep this free C+T contact outside the K/axis capture band.
                const auto distance=std::hypot(13.,9.);const auto contact=screen(-8+4*13/distance,-5+4*9/distance);
                click(screen(5,4));mouse(contact,QEvent::MouseMove,Qt::NoButton);
                auto appearance=app::ApplicationSettings::load(QString::fromStdString(directory.string()));
                const auto original_theme=appearance.theme;
                for(const QString theme:{"light","dark","light"}) {
                    appearance.theme=theme;app::apply_application_appearance(application,appearance);flush();
                    mouse(contact,QEvent::MouseMove,Qt::NoButton);
                    check(std::ranges::any_of(view->transient_labels(),[](const auto& label){return label.second=="C+T";}),"Circular C+T preview missing after theme change");
                    check(view->grabFramebuffer().save(QString::fromStdString((directory/("native-contact-preview-"+std::to_string(scenario)+"-"+theme.toStdString()+".png")).string())),"Circular contact preview capture failed");
                }
                appearance.theme=original_theme;app::apply_application_appearance(application,appearance);flush();click(contact);
            } else {
                window.findChild<QAction*>("sketchTangentAction")->trigger();flush();
                const auto first=scenario<2?"circle:"+sketch.circles[0].id:"arc:"+sketch.arcs[0].id;
                const auto second=scenario<2?"circle:"+sketch.circles[1].id:"arc:"+sketch.arcs[1].id;
                pick(scenario%2?second:first);pick(scenario%2?first:second);
            }
            check(count("T")==before_t+1,"Native GUI did not create T");
            check(scenario>=4?count("C")>=before_c+1:count("C")==before_c+((scenario==2||scenario==3)?0:2),"Native GUI C count disagrees with point ownership");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto finish=[&] {
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            };finish();
            auto saved=document::PartDocument::load(path);const auto connected=saved.sketches.front();
            const auto tangent=std::ranges::find_if(connected.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::Tangent&&
                std::ranges::none_of(sketch.constraints,[&](const auto& old){return old.id==c.id;});});
            check(tangent!=connected.constraints.end()&&!tangent->first_point_id.empty(),"GUI T has no persisted native contact");
            if(scenario<2||scenario>=4)for(const auto& geometry:{tangent->geometry_id,tangent->second_geometry_id})
                check(std::ranges::any_of(connected.constraints,[&](const auto& relation){return relation.kind==sketcher::ConstraintKind::PointOnCircle&&relation.first_point_id==tangent->first_point_id&&relation.geometry_id==geometry;}),"Created circular contact lacks an independent persisted C");
            check(connected.points.size()==original_points+((scenario==2||scenario==3)?0:scenario>=4?2:1),"GUI created an unexpected native point");
            if(scenario==2||scenario==3)check(connected.constraints.size()==original_constraints+1,"Shared native arc GUI must add only T");
            const auto verify=[&](const sketcher::Sketch& actual) {
                const auto* p=actual.find_point(tangent->first_point_id);check(p,"GUI drag lost native contact identity");
                std::array<std::array<double,3>,2> circles;
                for(unsigned n=0;n<2;++n) {
                    const auto geometry=n?tangent->second_geometry_id:tangent->geometry_id;
                    const auto circle=std::ranges::find(actual.circles,geometry,&sketcher::SketchCircle::id);
                    const auto arc=std::ranges::find(actual.arcs,geometry,&sketcher::SketchArc::id);
                    check(circle!=actual.circles.end()||arc!=actual.arcs.end(),"GUI circular contact geometry missing");
                    const auto* c=actual.find_point(circle!=actual.circles.end()?circle->center_point_id:arc->center_point_id);
                    const auto r=circle!=actual.circles.end()?circle->radius:arc->radius;
                    circles[n]={c->x,c->y,r};check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-r)<1e-6,"GUI drag broke native contact incidence");
                }
                check(std::abs((p->x-circles[0][0])*(p->y-circles[1][1])-(p->y-circles[0][1])*(p->x-circles[1][0]))/(circles[0][2]*circles[1][2])<1e-6,"GUI drag broke native T");
            };verify(connected);
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            open();
            if(tested_dimension)for(const double factor:{1.05,.95,1.02}) {
                const auto key="dimension:"+tested_dimension->id;
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key=key;
                const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});
                check(dimension!=view->mesh().dimensions.end(),"Native GUI dimension missing");candidate.geometry_index=unsigned(dimension-view->mesh().dimensions.begin());
                const auto before=dimension->value;const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Native GUI dimension label missing");
                click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Native dimension double-click did not open editor");
                field->setText(QString::number(tested_dimension->value*factor,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                const auto value=[&]{const auto d=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});check(d!=view->mesh().dimensions.end(),"Native dimension identity disappeared");return d->value;};
                check(std::abs(value()-tested_dimension->value*factor)<1e-6,"Native GUI R/D edit failed");
                window.findChild<QAction*>("undoAction")->trigger();flush();check(std::abs(value()-before)<1e-6,"Native dimension Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(std::abs(value()-tested_dimension->value*factor)<1e-6,"Native dimension Redo failed");
            }
            std::vector<std::string> point_ids{tangent->first_point_id};
            for(const auto& c:connected.circles)point_ids.push_back(c.center_point_id);
            for(const auto& a:connected.arcs)for(const auto& id:{a.center_point_id,a.start_point_id,a.end_point_id})if(std::ranges::find(point_ids,id)==point_ids.end())point_ids.push_back(id);
            for(const auto& id:point_ids)for(unsigned drag=0;drag<3;++drag) {
                std::cout<<"Native GUI drag scenario="<<scenario<<" point="<<id<<" repeat="<<drag<<std::endl;
                std::vector<kernel::Vec3> fit;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id)fit.push_back(p.position);
                view->fit_points(fit);flush();
                const auto point=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+id;});
                check(point!=view->mesh().points.end(),"GUI native point missing");const auto before=point->position;
                const bool open_arc_endpoint=scenario==5&&std::ranges::any_of(connected.arcs,[&](const auto& arc){return arc.start_point_id==id||arc.end_point_id==id;});
                // Move finite-arc ends along their radial direction, preserving
                // the contact domain rather than sweeping an end across T.
                auto delta=QPointF(9,-6);
                if(open_arc_endpoint) {
                    const auto& arc=connected.arcs.front();
                    const auto center=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+arc.center_point_id;});
                    check(center!=view->mesh().points.end(),"Finite arc center missing");
                    const auto p=connected.local_point(before),c=connected.local_point(center->position);
                    const auto radial=screen(p[0],p[1])-screen(c[0],c[1]);
                    delta=radial*(12/std::hypot(radial.x(),radial.y()));
                }
                auto start=pick("point:"+id);
                if(open_arc_endpoint) {
                    const auto p=connected.local_point(before);start=screen(p[0],p[1]);
                }
                const auto to=start+delta;
                mouse(start,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto moved=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+id;});
                if(moved==view->mesh().points.end()||std::hypot(moved->position.x-before.x,moved->position.y-before.y,moved->position.z-before.z)<=1e-5) {
                    const auto target=local(to);finish();
                    std::cerr<<"Rejected GUI native drag point="<<id<<" target="<<target[0]<<","<<target[1]<<" fixture="<<path.string()<<'\n';
                    throw std::runtime_error("GUI native point drag did not move geometry");
                }
            }
            finish();saved=document::PartDocument::load(path);verify(saved.sketches.front());open();finish();
            std::cout<<"Native contact GUI scenario="<<scenario<<" passed"<<std::endl;
        }
        std::cout<<"Native C+T creation, manual circle T and shared native arc T-only: GUI selection, repeated point drags and persistence passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
