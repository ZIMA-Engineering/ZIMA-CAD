int verify_sketch_arc_axis_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const std::array<unsigned,9> actions{1,4,5,6,7,9,8,0,2};
        const std::array<unsigned,6> current_actions{4,5,9,0,1,2};
        for(unsigned scenario=unsigned(std::max(0,qEnvironmentVariableIntValue("ZIMA_ARC_AXIS_SCENARIO_START")));scenario<48;++scenario) {
            const bool current=scenario>=36;
            const auto action=current?current_actions[(scenario-36)/2]:actions[scenario/4];const bool reverse=scenario%2;const bool tangent=current||(scenario%4)>=2;
            std::cout<<"Arc axis GUI scenario="<<scenario<<" action="<<action<<" reverse="<<reverse<<" T="<<tangent<<std::endl;
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto input=std::filesystem::path(qEnvironmentVariable("ZIMA_NATIVE_CONTACT_SOURCE").toStdString());
            if(current)input=input.parent_path()/"form-koule-axis-ends.json";
            std::ifstream input_file(input);check(bool(input_file),"Arc axis GUI fixture missing");
            auto sketch=sketcher::Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input_file),{}));
            sketch.owner_container_id=feature.id;
            if(tangent&&!current)static_cast<void>(sketch.add_tangent_constraint(sketch.arcs[reverse?1:0].id,sketch.arcs[reverse?0:1].id));
            const auto point=action<6?sketch.arcs[action/3].center_point_id:sketch.arcs[(action-6)/2].start_point_id;
            const auto support=action<6?action%3:(action-6)%2;
            const std::string target=support==2?"external_point:sketch_origin":support?"sketch_axis:y":"sketch_axis:x";
            if(action>=6)std::erase_if(sketch.constraints,[&](const auto& c){return c.kind==sketcher::ConstraintKind::PointOnLine&&c.first_point_id==point;});
            auto tested_dimension=reverse?sketch.create_arc_diameter_dimension(sketch.arcs[1].id):sketch.create_arc_radius_dimension(sketch.arcs[0].id);
            sketch.apply_dimension(tested_dimension);
            const auto original_points=sketch.points.size(),original_constraints=sketch.constraints.size();
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Native tangent contact");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("arc-axis-"+std::to_string(scenario)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Arc axis fixture did not open");flush();
            check(activate_test_body(application,window,body),"Arc axis Body cannot activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto open=[&] {
                QTreeWidgetItem* row{};for(QTreeWidgetItemIterator i(tree);*i;++i)
                    if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"Arc axis Sketch row missing");window.show_tree_item_properties(row);flush();
                auto* button=window.findChild<QPushButton*>("sketchOpenButton");check(button,"Arc axis Sketch open button missing");button->click();flush();
                QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            };open();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Arc axis View missing");
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
                check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key==key,"Arc axis hover/click mismatch");
                mouse(*pos,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*pos,QEvent::MouseButtonRelease,Qt::LeftButton);return *pos;
            };
            window.findChild<QAction*>("sketchCoincidentAction")->trigger();flush();
            pick(reverse?target:"point:"+point);pick(reverse?"point:"+point:target);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            const bool expected=action!=0&&action!=2&&(!current||action!=1);
            if(expected) {
                const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
                const auto created=snapshot();
                window.findChild<QAction*>("undoAction")->trigger();flush();
                check(snapshot().serialized()==sketch.serialized(),"Arc axis constraint Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();
                check(snapshot().serialized()==created.serialized(),"Arc axis constraint Redo failed");
            }
            const auto finish=[&] {
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())p->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            };finish();
            auto saved=document::PartDocument::load(path);auto connected=saved.sketches.front();
            check(connected.constraints.size()==original_constraints+(expected?1:0),"GUI axis/Origin constraint count incorrect");
            check(connected.points.size()==original_points,"GUI axis contact changed native point ownership");
            const auto verify=[&](const sketcher::Sketch& actual) {
                const auto* p=actual.find_point(point);check(p,"GUI axis point identity lost");
                if(expected)check(support==2?std::hypot(p->x,p->y)<1e-6:std::abs(support?p->x:p->y)<1e-6,"GUI axis/Origin contact equation failed");
                for(const auto& a:actual.arcs) {
                    const auto* c=actual.find_point(a.center_point_id);
                    for(const auto& id:{a.start_point_id,a.end_point_id}) {
                        const auto* e=actual.find_point(id);check(std::abs(std::hypot(e->x-c->x,e->y-c->y)-a.radius)<1e-6,"GUI axis contact broke native arc incidence");
                    }
                }
                if(tangent) {
                    const auto* p=actual.find_point(sketch.arcs[0].end_point_id);
                    const auto* a=actual.find_point(actual.arcs[0].center_point_id);const auto* b=actual.find_point(actual.arcs[1].center_point_id);
                    check(std::abs((p->x-a->x)*(p->y-b->y)-(p->y-a->y)*(p->x-b->x))/(actual.arcs[0].radius*actual.arcs[1].radius)<1e-6,"GUI axis contact broke shared T");
                }
            };verify(connected);
            if(!expected)check(connected.serialized()==sketch.serialized(),"Rejected GUI axis/Origin contact changed Sketch");
            if(expected) {
                open();
                for(const double factor:{1.01,.99,1.02}) {
                    const auto key="dimension:"+tested_dimension.id;
                    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key=key;
                    const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});
                    check(dimension!=view->mesh().dimensions.end(),"Arc axis GUI dimension missing");candidate.geometry_index=unsigned(dimension-view->mesh().dimensions.begin());
                    const auto before=dimension->value;const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Arc axis GUI dimension label missing");
                    click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Arc axis dimension editor missing");
                    field->setText(QString::number(tested_dimension.value*factor,'g',15));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                    const auto value=[&]{const auto d=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key==key;});check(d!=view->mesh().dimensions.end(),"Arc axis dimension identity lost");return d->value;};
                    check(std::abs(value()-tested_dimension.value*factor)<1e-6,"Arc axis GUI dimension edit failed");
                    window.findChild<QAction*>("undoAction")->trigger();flush();check(std::abs(value()-before)<1e-6,"Arc axis dimension Undo failed");
                    window.findChild<QAction*>("redoAction")->trigger();flush();check(std::abs(value()-tested_dimension.value*factor)<1e-6,"Arc axis dimension Redo failed");
                }
                finish();connected=document::PartDocument::load(path).sketches.front();verify(connected);open();
                for(unsigned repeat=0;repeat<3;++repeat) {
                    std::vector<kernel::Vec3> fit;for(const auto& p:view->mesh().points)if(p.reference.owner_id==sketch.id)fit.push_back(p.position);
                    view->fit_points(fit);flush();
                    const auto* p=connected.find_point(point);const auto start=screen(p->x,p->y);pick("point:"+point);
                    const auto to=start+QPointF(8,-5);
                    auto prediction=connected;const auto destination=local(to);
                    const bool permitted=prediction.move_point(point,destination[0],destination[1]);
                    mouse(start,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                    finish();const auto edited=document::PartDocument::load(path).sketches.front();verify(edited);
                    if(!permitted)check(edited.serialized()==connected.serialized(),"Rejected GUI arc point drag changed document");
                    else {const auto* moved=edited.find_point(point);check(std::hypot(moved->x-p->x,moved->y-p->y)>1e-6,"Permitted GUI arc point drag did not move");}
                    connected=edited;open();
                }
                finish();verify(document::PartDocument::load(path).sketches.front());
            }
            std::cout<<"Arc axis GUI scenario="<<scenario<<" passed"<<std::endl;
        }
        std::cout<<"Native arc axis/Origin mouse selection, dimension edits, drags, Undo/Redo and persistence passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
