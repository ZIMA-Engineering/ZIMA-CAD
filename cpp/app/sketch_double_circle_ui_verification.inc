int verify_sketch_double_circle_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        QString modal_error;
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();if(!modal_error.isEmpty())throw std::runtime_error(modal_error.toStdString());};
        for(bool native:{false,true})for(bool reverse:{false,true}) {
            std::cout<<"Double circular GUI native="<<native<<" reverse="<<reverse<<std::endl;
            auto part=document::PartDocument::create_default();
            sketcher::Sketch sketch;std::string body;
            const auto input=qEnvironmentVariable("ZIMA_DOUBLE_CIRCLE_PART");
            if(!input.isEmpty()) {
                part=document::PartDocument::load(std::filesystem::path(input.toStdWString()));sketch=part.sketches.back();
                const auto* owner=part.body_history.owner(sketch.owner_container_id);check(owner,"Double circular Body missing");body=owner->scope.id;
            } else {
                std::ifstream file(std::filesystem::path(__FILE__).parent_path()/"../tests/fixtures/sketch/double-tangent-projected-circles.json");
                check(bool(file),"Double circular Sketch fixture missing");sketch=sketcher::Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(file),{}));
                // Give the projected supports real earlier native ownership;
                // the GUI must exercise live read-only references, not orphans.
                auto source=sketcher::Sketch::create_default();
                auto source_feature=document::PartDocument::create_sketch_container();source.owner_container_id=source_feature.id;
                for(unsigned i=0;i<3;++i) {
                    const auto& circle=sketch.circles[i];const auto* centre=sketch.find_point(circle.center_point_id);
                    const auto id=source.add_circle(centre->x,centre->y,circle.radius);
                    auto& ref=sketch.external_references[i];ref.kind=sketcher::ExternalReferenceKind::Edge;
                    ref.source_document_id=part.document_id;ref.source_owner_id=source.id;ref.source_semantic_key="circle:"+id;
                    ref.cached_paths.clear();ref.cached_points.clear();
                    for(unsigned n=0;n<=64;++n)ref.cached_points.push_back({centre->x+circle.radius*std::cos(n*std::numbers::pi/32),centre->y+circle.radius*std::sin(n*std::numbers::pi/32)});
                    ref.exact_spline=sketcher::sketch_curve_geometry(source,id);
                }
                auto feature=document::PartDocument::create_sketch_container();sketch.owner_container_id=feature.id;
                part.history={source_feature,feature};part.sketches={source,sketch};document::BodyHistoryGraph graph;body=graph.create_body("Double circular contact");
                graph.insert({document::PartHistoryKind::Feature,source_feature.id});graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            }
            auto dimension=reverse?sketch.create_circle_diameter_dimension(sketch.circles.back().id):sketch.create_circle_radius_dimension(sketch.circles.back().id);
            sketch.apply_dimension(dimension);
            *std::ranges::find(part.sketches,sketch.id,&sketcher::Sketch::id)=sketch;
            const auto path=directory/("double-circle-"+std::to_string(native)+std::to_string(reverse)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            unsigned image_index{};
            const auto capture=[&](const char* stage) {
                std::cout<<"Double circular GUI "<<stage<<std::endl;
                window.grab().save(QString::fromStdString((directory/("step-"+std::to_string(native)+std::to_string(reverse)+"-"+std::to_string(image_index++)+".png")).string()));
            };
            QTimer modal_guard;modal_guard.setInterval(250);
            QObject::connect(&modal_guard,&QTimer::timeout,&window,[&] {
                for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()) {
                    std::cerr<<"Double circular GUI modal: "<<box->text().toStdString()<<std::endl;
                    modal_error=box->text();capture("modal");box->reject();
                }
            });modal_guard.start();
            check(window.open_document_path(QString::fromStdString(path.string())),"Native contact fixture did not open");flush();
            check(activate_test_body(application,window,body),"Native contact Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto open=[&] {
                QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)
                    if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"Native contact Sketch row missing");window.show_tree_item_properties(row);flush();
                if(auto* button=window.findChild<QPushButton*>("sketchOpenButton")){button->click();flush();}
                check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Native contact Sketch did not open");
                QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            };open();capture("opened");
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Native contact View missing");
            std::vector<kernel::Vec3> bounds;for(const auto& p:sketch.points)bounds.push_back(sketch.world_point(p.x,p.y));view->fit_points(bounds);flush();

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
            const auto pick=[&](const std::string& key,std::optional<QPointF> preferred=std::nullopt,bool confirm=true) {
                view->clear_selection();std::optional<QPointF> pos;unsigned active{};
                if(preferred) {
                    const auto offered=view->selection_candidates_at(*preferred);
                    for(unsigned n=0;n<offered.size();++n)if(offered[n].owner_id==sketch.id&&offered[n].semantic_key==key){pos=*preferred;active=n;break;}
                }
                for(int y=16;y<view->height()-16&&!pos;y+=3)for(int x=16;x<view->width()-16;x+=3) {
                    const auto offered=view->selection_candidates_at(QPointF(x,y));
                    for(unsigned n=0;n<offered.size();++n)if(offered[n].owner_id==sketch.id&&offered[n].semantic_key==key){pos=QPointF(x,y);active=n;break;}
                    if(pos)break;
                }
                if(!pos)throw std::runtime_error("Native common picker candidate missing: "+key);
                mouse(*pos,QEvent::MouseMove,Qt::NoButton);
                for(unsigned n=0;n<active;++n){mouse(*pos,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Native contact hover/click mismatch");
                if(confirm){mouse(*pos,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::LeftButton);}return *pos;
            };

            const auto tangent_count=[&]{return std::ranges::count_if(view->mesh().constraint_markers,[](const auto& m){return m.label=="T";});};
            const auto before=tangent_count();
            const auto first=(native?"circle:"+sketch.circles[1].id:"external_edge:"+sketch.external_references[1].id);
            const auto second="circle:"+sketch.circles.back().id;
            window.findChild<QAction*>("sketchTangentAction")->trigger();flush();capture("T started");pick(reverse?second:first);capture("T first selected");pick(reverse?first:second);capture("T second selected");
            check(tangent_count()==before+1,"GUI did not add second circular T");
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const auto finish=[&] {
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* d:window.findChildren<QDialog*>())if(d->isVisible()&&
                    (dynamic_cast<app::SketchPropertiesDialog*>(d)||dynamic_cast<app::PrimitivePropertiesDialog*>(d))) {
                    QPointer<QDialog> pending=d;
                    d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                    flush();
                    // The reported profile is still being authored. Its
                    // touching closed circles are not yet an extrudable
                    // region; explicitly retain the unresolved definition
                    // through the ordinary second-OK workflow.
                    if(pending&&pending->isVisible()&&dynamic_cast<app::PrimitivePropertiesDialog*>(pending.data()))
                        pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                    flush();
                }
                flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            };finish();capture("T saved");
            auto saved=document::PartDocument::load(path);auto connected=saved.sketches.back();test::verify_circle_contacts(connected);
            const auto snapshot=connected.constraints;
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            check(document::PartDocument::load(path).sketches.back().constraints==snapshot,"GUI T Undo/Redo lost identities");
            open();
            const auto edit_dimension=[&] {
                const auto key="dimension:"+dimension.id;
                for(const double factor:{1.01,.99,1.02}) {
                    const auto current=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});
                    check(current!=view->mesh().dimensions.end(),"Double tangent dimension missing");
                    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key=key;candidate.geometry_index=unsigned(current-view->mesh().dimensions.begin());
                    const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Double tangent dimension label missing");
                    const double before=current->value,target=dimension.value*factor;
                    click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Double tangent dimension editor missing");
                    field->setText(QString::number(target,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                    const auto value=[&]{const auto d=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});check(d!=view->mesh().dimensions.end(),"Double tangent dimension identity lost");return d->value;};
                    check(std::abs(value()-target)<1e-6,"Double tangent GUI dimension edit failed");
                    test::verify_circle_contacts(*window.active_sketch_for_test());
                    window.findChild<QAction*>("undoAction")->trigger();flush();check(std::abs(value()-before)<1e-6,"Double tangent dimension Undo failed");
                    test::verify_circle_contacts(*window.active_sketch_for_test());
                    window.findChild<QAction*>("redoAction")->trigger();flush();check(std::abs(value()-target)<1e-6,"Double tangent dimension Redo failed");
                    test::verify_circle_contacts(*window.active_sketch_for_test());
                }
            };edit_dimension();capture("dimensions edited");
            std::vector<std::string> handles{connected.circles.back().center_point_id};
            for(const auto& c:connected.constraints)if(c.kind==sketcher::ConstraintKind::Tangent)handles.push_back(c.first_point_id);
            const auto drag_handles=[&] {
                for(const auto& id:handles)for(unsigned repeat=0;repeat<3;++repeat) {
                    std::cout<<"Double circular GUI drag "<<id<<" repeat="<<repeat<<std::endl;
                    std::vector<kernel::Vec3> fit;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id)fit.push_back(p.position);
                    view->fit_points(fit);flush();
                    const auto point=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+id;});
                    check(point!=view->mesh().points.end(),"Double tangent handle absent");const auto before=point->position;
                    const auto coordinates=sketch.local_point(before);
                    // Consume RMB cycling with this drag press. A separate
                    // preliminary confirmation would reset the next gesture
                    // to the first candidate and drag the nearby free C point.
                    const auto from=pick("point:"+id,screen(coordinates[0],coordinates[1]),false),to=from+QPointF(12,-8);
                    const auto initial=*window.active_sketch_for_test();
                    mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                    const auto moved=std::ranges::find_if(view->mesh().points,[&](const auto& p){return p.reference.owner_id==sketch.id&&p.reference.semantic_key=="point:"+id;});
                    if(moved==view->mesh().points.end()||std::hypot(moved->position.x-before.x,moved->position.y-before.y,moved->position.z-before.z)<=1e-5) {
                        const auto target=local(to);auto native_check=initial;
                        std::cout<<"Failed GUI target "<<target[0]<<","<<target[1]<<" handle="<<id<<" native accepted="<<native_check.move_point(id,target[0],target[1])<<std::endl;
                        std::ofstream(directory/"failed-drag.json")<<initial.serialized();capture("drag failed");
                        throw std::runtime_error("GUI double tangent drag did not move");
                    }
                    test::verify_circle_contacts(*window.active_sketch_for_test());
                }
            };drag_handles();capture("circles dragged");finish();saved=document::PartDocument::load(path);connected=saved.sketches.back();test::verify_circle_contacts(connected);
            open();
            const auto driven=connected.circles.back();const auto* center=connected.find_point(driven.center_point_id);
            const auto pieces=sketcher::sketch_trim_topology(connected,true);
            const auto parameter=[&](const std::string& id) {const auto* p=connected.find_point(id);double a=std::atan2(p->y-center->y,p->x-center->x)/(2*std::numbers::pi);return a<0?a+1:a;};
            const auto matches=[&](double a,double b){return std::abs(std::remainder(a-b,1.))<1e-7;};
            const auto piece=std::ranges::find_if(pieces,[&](const auto& p){return p.geometry_id==driven.id&&
                ((matches(p.start,parameter(handles[1]))&&matches(p.end,parameter(handles[2])))||
                 (matches(p.start,parameter(handles[2]))&&matches(p.end,parameter(handles[1]))));});
            check(piece!=pieces.end(),"GUI fixture has no interval between tangent contacts");
            const double mid=(piece->start+piece->end)*std::numbers::pi;
            const auto position=screen(center->x+driven.radius*std::cos(mid),center->y+driven.radius*std::sin(mid));
            window.findChild<QAction*>("sketchTrimAction")->trigger();flush();click(position);capture("trim clicked");
            mouse(position,QEvent::MouseButtonPress,Qt::MiddleButton,Qt::MiddleButton);mouse(position,QEvent::MouseButtonRelease,Qt::MiddleButton);flush();finish();
            saved=document::PartDocument::load(path);auto trimmed=saved.sketches.back();test::verify_circle_contacts(trimmed);
            check(trimmed.arcs.size()==1,"GUI trim did not retain one arc");
            const auto trimmed_snapshot=trimmed.constraints;
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            check(document::PartDocument::load(path).sketches.back().constraints==trimmed_snapshot,"Trim Undo/Redo lost T/C references");
            sketch=trimmed;open();edit_dimension();handles={trimmed.arcs.front().center_point_id,trimmed.arcs.front().start_point_id,trimmed.arcs.front().end_point_id};drag_handles();
            view->grabFramebuffer().save(QString::fromStdString((directory/("double-tangent-arc-"+std::to_string(native)+std::to_string(reverse)+".png")).string()));
            finish();test::verify_circle_contacts(document::PartDocument::load(path).sketches.back());
            std::cout<<"Double circular GUI case passed"<<std::endl;
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Double circular GUI: "<<e.what()<<'\n';return 1;}
}
