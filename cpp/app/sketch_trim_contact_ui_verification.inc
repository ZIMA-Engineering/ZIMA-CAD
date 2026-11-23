int verify_sketch_trim_contact_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        std::filesystem::create_directories(directory);
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        QString modal_error;
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();if(!modal_error.isEmpty())throw std::runtime_error(modal_error.toStdString());};
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();std::string body;
        const auto input=qEnvironmentVariable("ZIMA_TRIM_CONTACT_PART");
        if(!input.isEmpty()) {
            part=document::PartDocument::load(std::filesystem::path(input.toStdWString()));sketch=part.sketches.back();
            const auto* owner=part.body_history.owner(sketch.owner_container_id);check(owner,"Trim contact Body missing");body=owner->scope.id;
        } else {
            sketch=test::make_copied_circle_contact_fixture(1.,.61);
            static_cast<void>(sketch.add_tangent_constraint(sketch.external_references[1].id,sketch.arcs.front().id));
            auto source=sketcher::Sketch::create_default();auto base=document::PartDocument::create_sketch_container();source.owner_container_id=base.id;
            for(unsigned i=0;i<sketch.external_references.size();++i) {
                const auto& native=sketch.circles[i];const auto* center=sketch.find_point(native.center_point_id);
                const auto circle=source.add_circle(center->x,center->y,native.radius);
                auto& ref=sketch.external_references[i];ref.source_document_id=part.document_id;ref.source_owner_id=source.id;ref.source_semantic_key="circle:"+circle;
                ref.exact_spline=sketcher::sketch_curve_geometry(source,circle);
                const auto p=*center;
                static_cast<void>(sketch.add_segment(p.x-20,p.y,p.x+20,p.y,1e-7,true));
            }
            auto feature=document::PartDocument::create_sketch_container();sketch.owner_container_id=feature.id;
            part.history={base,feature};part.sketches={source,sketch};document::BodyHistoryGraph graph;body=graph.create_body("Trim contacts");
            graph.insert({document::PartHistoryKind::Feature,base.id});graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        }
        // Exercise each editable coincident support on its own fresh copy.
        std::vector<std::string> targets;
        for(const auto& c:sketch.circles)targets.push_back(c.id);
        for(const auto& a:sketch.arcs)if(std::ranges::any_of(sketch.external_references,[&](const auto& r){return sketch.coincident_circular_support(a.id,r.id);}))targets.push_back(a.id);
        unsigned cases{};
        for(const auto& target:targets) {
            std::cout<<"Trim GUI case "<<cases<<" topology"<<std::endl;
            std::optional<sketcher::SketchDimension> dimension;
            if(input.isEmpty()) {
                sketch.dimensions.clear();dimension=cases%2?sketch.create_arc_diameter_dimension(sketch.arcs.front().id):sketch.create_arc_radius_dimension(sketch.arcs.front().id);
                sketch.apply_dimension(*dimension);part.sketches.back()=sketch;
            }
            const auto topology=sketcher::sketch_trim_topology(sketch,true);
            auto piece=std::ranges::find_if(topology,[&](const auto& p){return p.geometry_id==target&&p.end-p.start>1e-5;});
            for(const auto& c:sketch.constraints)if(!c.suppressed&&c.kind==sketcher::ConstraintKind::PointOnCircle&&sketch.coincident_circular_support(c.geometry_id,target)) {
                const auto* point=sketch.find_point(c.first_point_id);std::string center;double start=0,sweep=2*std::numbers::pi;
                for(const auto& circle:sketch.circles)if(circle.id==target)center=circle.center_point_id;
                for(const auto& arc:sketch.arcs)if(arc.id==target){center=arc.center_point_id;start=arc.start_angle;sweep=arc.end_angle-start;}
                const auto* origin=sketch.find_point(center);check(point&&origin,"Trim contact identity missing");
                double angle=std::atan2(point->y-origin->y,point->x-origin->x);while(angle<start)angle+=2*std::numbers::pi;
                while(angle>start+sweep&&angle-2*std::numbers::pi>=start)angle-=2*std::numbers::pi;
                const double parameter=(angle-start)/sweep;if(parameter<=1e-7||parameter>=1-1e-7)continue;
                piece=std::ranges::find_if(topology,[&](const auto& p){return p.geometry_id==target&&std::abs(p.start-parameter)<1e-8&&p.end-p.start>1e-5;});
                check(piece!=topology.end(),"GUI topology missed the persisted contact on the native copy");break;
            }
            check(piece!=topology.end(),"Trim contact piece missing");
            auto expected=sketch;static_cast<void>(sketcher::apply_sketch_trim(expected,{*piece}));
            const auto path=directory/("trim-contact-"+std::to_string(cases)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1200,900);window.show();
            QTimer guard;guard.setInterval(200);QObject::connect(&guard,&QTimer::timeout,&window,[&] {
                for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget);box&&box->isVisible()){modal_error=box->text();box->reject();}
            });guard.start();
            check(window.open_document_path(QString::fromStdWString(path.wstring())),"Trim fixture did not open");flush();
            check(activate_test_body(application,window,body),"Trim Body did not activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Trim Sketch row missing");window.show_tree_item_properties(row);flush();
            if(auto* button=window.findChild<QPushButton*>("sketchOpenButton")){button->click();flush();}
            check(window.active_sketch_for_test(),"Trim Sketch did not enter edit mode");
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Trim View missing");
            std::vector<kernel::Vec3> bounds;for(const auto& p:sketch.points)bounds.push_back(sketch.world_point(p.x,p.y));view->fit_points(bounds);flush();
            const auto local=[&](QPointF at){const auto ray=view->ray_at(at);check(ray.has_value(),"Trim camera ray missing");const auto p=sketch.intersect_ray(ray->first,ray->second);check(p.has_value(),"Trim camera not facing Sketch");return *p;};
            const auto screen=[&](double x,double y) {const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,d=ux*vy-uy*vx;return QPointF(((x-a[0])*vy-(y-a[1])*vx)/d,(ux*(y-a[1])-uy*(x-a[0]))/d);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton){QMouseEvent e(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&e);flush();};
            const auto pick=[&](QPointF at,const std::string& key) {
                view->clear_selection();
                mouse(at,QEvent::MouseMove,Qt::NoButton);const auto offered=view->selection_candidates_at(at);
                check(std::ranges::any_of(offered,[&](const auto& c){return c.semantic_key==key;}),"Common picker missed the contact-bounded Trim piece");
                for(unsigned n=0;n<offered.size()&&(!view->hovered_candidate()||view->hovered_candidate()->semantic_key!=key);++n){mouse(at,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton);mouse(at,QEvent::MouseButtonRelease,Qt::RightButton);}
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Trim hover/click candidate differs");
            };
            const auto at=piece->points[piece->points.size()/2];const auto position=screen(at[0],at[1]);const auto key="trim_piece:"+std::to_string(piece-topology.begin());
            const auto original=window.active_sketch_for_test()->serialized();
            for(bool cancel:{true,false}) {
                std::cout<<"Trim GUI preview cancel="<<cancel<<std::endl;
                window.findChild<QAction*>("sketchTrimAction")->trigger();flush();pick(position,key);
                window.grab().save(QString::fromStdWString((directory/("trim-hover-"+std::to_string(cases)+".png")).wstring()));
                mouse(position,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(position,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto* preview=window.sketch_trim_preview_for_test();check(preview&&preview->circles.size()==expected.circles.size()&&preview->arcs.size()==expected.arcs.size(),"GUI trim removed a different interval");
                if(cancel) {QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();check(window.active_sketch_for_test()->serialized()==original,"Trim Cancel changed the Sketch");}
                else {
                    mouse(position,QEvent::MouseButtonPress,Qt::MiddleButton);mouse(position,QEvent::MouseButtonRelease,Qt::MiddleButton);
                    check(window.sketch_trim_preview_for_test()&&window.active_sketch_for_test()->serialized()==original,"Short middle click committed Trim");
                    mouse(position,QEvent::MouseButtonDblClick,Qt::MiddleButton,Qt::MiddleButton);mouse(position,QEvent::MouseButtonRelease,Qt::MiddleButton);
                }
            }
            auto trimmed=*window.active_sketch_for_test();const auto snapshot=trimmed.serialized();
            std::cout<<"Trim GUI confirmed"<<std::endl;
            check(!window.sketch_trim_preview_for_test()&&snapshot!=original&&trimmed.circles.size()==expected.circles.size()&&trimmed.arcs.size()==expected.arcs.size(),"Middle double-click did not commit Trim");
            check(trimmed.external_references==sketch.external_references,"GUI trim changed the reference source");
            window.findChild<QAction*>("undoAction")->trigger();flush();check(window.active_sketch_for_test()->serialized()==original,"GUI Trim Undo lost the original contacts");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(window.active_sketch_for_test()->serialized()==snapshot,"GUI Trim Redo changed contacts");
            auto* changed=window.active_sketch_for_test();check(changed,"Trim edit session lost");
            auto check_solve=*changed;const auto solved=check_solve.solve();check(solved.status!=sketcher::SolveStatus::Invalid&&solved.status!=sketcher::SolveStatus::Conflicting,"GUI trimmed Sketch cannot solve");
            if(dimension) {
                std::cout<<"Trim GUI numeric edits"<<std::endl;
                const auto verify=[&] {
                    const auto& actual=*window.active_sketch_for_test();test::verify_circle_contacts(actual);
                    check(actual.external_references==sketch.external_references,"Post-trim GUI edits changed external geometry");
                    for(const auto& a:actual.arcs)for(const auto& id:{a.start_point_id,a.end_point_id}) {
                        const auto* p=actual.find_point(id);const auto* center=actual.find_point(a.center_point_id);
                        check(p&&center&&std::abs(std::hypot(p->x-center->x,p->y-center->y)-a.radius)<1e-7,"Post-trim GUI endpoint left its circle");
                    }
                };
                for(double factor:{1.01,.99,1.02}) {
                    std::cout<<"Trim GUI factor="<<factor<<std::endl;
                    const auto key="dimension:"+dimension->id;
                    const auto d=std::ranges::find_if(view->mesh().dimensions,[&](const auto& value){return value.reference.semantic_key==key;});
                    check(d!=view->mesh().dimensions.end(),"Post-trim R/D dimension missing");
                    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key=key;candidate.geometry_index=unsigned(d-view->mesh().dimensions.begin());
                    const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Post-trim dimension label missing");
                    mouse(*label,QEvent::MouseMove,Qt::NoButton);mouse(*label,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                    mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Post-trim R/D editor missing");
                    field->setText(QString::number(dimension->value*factor,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                    const auto radius=*window.active_sketch_for_test()->circular_radius(sketch.arcs.front().id);
                    check(std::abs(radius-dimension->value*factor/(cases%2?2:1))<1e-7,"Post-trim GUI dimension change rejected");verify();
                }
                const auto& arc=sketch.arcs.front();
                for(const auto& id:{arc.center_point_id,arc.start_point_id,arc.end_point_id})for(unsigned repeat=0;repeat<3;++repeat) {
                    std::cout<<"Trim GUI drag "<<id<<" repeat="<<repeat<<std::endl;
                    const auto before=*window.active_sketch_for_test()->find_point(id);const auto from=screen(before.x,before.y),to=from+QPointF(8,-5);
                    std::ofstream(directory/"before-drag.json")<<window.active_sketch_for_test()->serialized();
                    const auto target_point=local(to);std::cout<<"Trim GUI target "<<target_point[0]<<","<<target_point[1]<<" pick"<<std::endl;
                    pick(from,"point:"+id);std::cout<<"Trim GUI press"<<std::endl;mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                    std::cout<<"Trim GUI move"<<std::endl;mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
                    std::cout<<"Trim GUI release"<<std::endl;mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                    const auto* after=window.active_sketch_for_test()->find_point(id);check(after&&std::hypot(after->x-before.x,after->y-before.y)>1e-6,"Post-trim GUI point did not move");verify();
                }
                trimmed=*window.active_sketch_for_test();
            }
            window.grab().save(QString::fromStdWString((directory/("trim-result-"+std::to_string(cases)+".png")).wstring()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&(dynamic_cast<app::SketchPropertiesDialog*>(dialog)||dynamic_cast<app::PrimitivePropertiesDialog*>(dialog))) {
                QPointer<QDialog> pending=dialog;dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                if(pending&&pending->isVisible()){pending->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();}
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(path);
            check(reopened.sketches.back().constraints==trimmed.constraints,"GUI trim native save/reopen changed contact constraints");
            ++cases;std::cout<<"Trim contact GUI case "<<cases<<" passed\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Trim contact GUI: "<<e.what()<<'\n';return 1;}
}
