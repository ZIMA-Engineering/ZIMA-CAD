int verify_sketch_corner_chain_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    if(qEnvironmentVariableIsSet("ZIMA_SKETCH_TRACE"))qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message){std::cerr<<message.toStdString()<<'\n';});
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto source=qEnvironmentVariable("ZIMA_VERIFY_CORNER_CHAIN_FILE");
        auto part=document::PartDocument::create_default();
        if(!source.isEmpty())part=document::PartDocument::load(std::filesystem::path(source.toStdWString()));
        else {
            auto profile=sketcher::Sketch::create_default();
            for(const auto xy:std::vector<std::array<double,2>>{{0,0},{50,0},{0,-50},{5,-50},{5,-5},{50,-5}})
                profile.points.push_back(sketcher::Sketch::create_point(xy[0],xy[1]));
            for(const auto ij:std::vector<std::array<int,2>>{{0,1},{2,0},{3,4},{4,5},{1,5},{3,2}})
                profile.segments.push_back(sketcher::Sketch::create_segment(profile.points[ij[0]].id,profile.points[ij[1]].id));
            for(const auto i:{0,3,5})static_cast<void>(profile.add_segment_constraint(profile.segments[i].id,sketcher::ConstraintKind::Horizontal));
            for(const auto i:{1,2,4})static_cast<void>(profile.add_segment_constraint(profile.segments[i].id,sketcher::ConstraintKind::Vertical));
            static_cast<void>(profile.add_point_reference_constraint(profile.points[0].id,"sketch_origin"));
            static_cast<void>(profile.add_segment_pair_constraint(profile.segments[0].id,profile.segments[1].id,sketcher::ConstraintKind::EqualLength));
            static_cast<void>(profile.add_segment_pair_constraint(profile.segments[5].id,profile.segments[4].id,sketcher::ConstraintKind::EqualLength));
            profile.apply_dimension(profile.create_segment_dimension(profile.segments[0].id));
            profile.apply_dimension(profile.create_segment_dimension(profile.segments[4].id));
            static_cast<void>(profile.add_corner_fillet(profile.segments[2].id,profile.segments[3].id,9.559749055366167));
            auto feature=document::PartDocument::create_extrusion_container(profile.id);
            feature.feature_kind=document::FeatureKind::Feature;feature.feature.sketch_id=profile.id;
            feature.feature.sides[0].operation=document::FeatureSideOperation::Extrusion;feature.feature.sides[0].length=10;
            profile.owner_container_id=feature.id;part.history={feature};part.sketches={profile};
            document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Corner chain"));
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
        }
        const auto sketch=part.sketches.front();
        const auto path=directory/"corner-chain-copy.prtz";
        part.save(path);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,900);window.show();
        check(window.open_document_path(QString::fromStdString(path.string())),"Corner fixture failed to open");flush();
        check(activate_test_body(application,window,part.body_history.bodies().front().scope.id),"Corner Body unavailable");flush();
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
        check(row,"Corner Sketch row missing");window.show_tree_item_properties(row);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");
        if(!open)open=window.findChild<QPushButton*>("featureSketchButton");
        if(open){open->click();flush();}
        check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Sketch editor did not open");
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"View missing");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_CORNER_EQUAL_DIMENSION")) {
            const auto driver=std::ranges::find_if(sketch.corner_radii,[](const auto& r){return r.dimension_visible;});
            check(driver!=sketch.corner_radii.end(),"Driving corner dimension missing");
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
            candidate.owner_id=sketch.id;candidate.semantic_key="corner_dimension:"+driver->id;
            for(std::size_t i=0;i<view->mesh().dimensions.size();++i) {
                const auto& ref=view->mesh().dimensions[i].reference;
                if(ref.owner_id==candidate.owner_id && ref.semantic_key==candidate.semantic_key)
                    candidate.geometry_index=i;
            }
            window.edit_dimension_inline(candidate);flush();
            auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
            check(field,"Corner dimension inline editor missing");
            field->setText("5");QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
            QApplication::sendEvent(field,&enter);flush();
            check(view->candidate_dimension_value(candidate).has_value() &&
                std::abs(*view->candidate_dimension_value(candidate)-5)<1e-7,"Inline corner edit failed");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
            check(window.execute_console_command("save").ok,"Equal corner Save failed");
            const auto after=document::PartDocument::load(path);
            check(after.sketches.front().corner_radii.size()==2 &&
                std::ranges::all_of(after.sketches.front().corner_radii,[](const auto& r){return std::abs(r.radius-5)<1e-7;}),"Saved equal radii differ from edited value");
            check(after.sketches.front().points==sketch.points && after.sketches.front().segments==sketch.segments &&
                after.sketches.front().constraints==sketch.constraints,"Equal radius edit changed source geometry or constraints");
            check(window.execute_console_command("undo").ok && window.execute_console_command("save").ok,"Equal corner Undo failed");
            check(document::PartDocument::load(path).sketches.front().corner_radii==sketch.corner_radii,"Equal corner Undo did not restore both radii");
            check(window.execute_console_command("redo").ok && window.execute_console_command("save").ok,"Equal corner Redo failed");
            check(document::PartDocument::load(path).sketches.front().serialized()==after.sketches.front().serialized(),"Equal corner Redo did not restore edited sketch");
            std::cout<<"Equal corner inline dimension, native save/reopen and Undo/Redo passed\n";return 0;
        }
        const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Camera ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Camera not facing Sketch");return *q;};
        const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
        const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
        const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,held,modifiers);QApplication::sendEvent(view,&event);flush();};
        const auto click=[&](QPointF p,Qt::KeyboardModifiers modifiers){mouse(p,QEvent::MouseMove,Qt::NoButton,Qt::NoButton,modifiers);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton,modifiers);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton,modifiers);};
        const auto select_segment=[&](std::size_t index) {
            const auto& segment=sketch.segments.at(index);
            const auto ends=sketch.visible_segment_endpoints(segment.id);
            check(ends.has_value(),"Segment endpoints unavailable");
            std::optional<QPointF> hit;std::size_t cycle_count=1000;
            for(int i=10;i<90;++i) {
                const double t=i/100.;const auto p=screen(
                    ends->first[0]*(1-t)+ends->second[0]*t,
                    ends->first[1]*(1-t)+ends->second[1]*t);
                const auto candidates=view->selection_candidates_at(p);
                for(std::size_t j=0;j<candidates.size();++j)
                    if(candidates[j].kind==viewer::CandidateKind::SketchSegment &&
                        candidates[j].semantic_key=="segment:"+segment.id && j<cycle_count){hit=p;cycle_count=j;}
                if(cycle_count==0)break;
            }
            check(hit.has_value(),"Profile leg not offered by common picker");
            mouse(*hit,QEvent::MouseMove,Qt::NoButton,Qt::NoButton,Qt::ControlModifier);
            for(std::size_t i=0;i<cycle_count;++i) {
                mouse(*hit,QEvent::MouseButtonPress,Qt::RightButton,Qt::RightButton,Qt::ControlModifier);
                mouse(*hit,QEvent::MouseButtonRelease,Qt::RightButton,Qt::NoButton,Qt::ControlModifier);
            }
            click(*hit,Qt::ControlModifier);
        };
        const bool rectangle_fixture=sketch.segments.size()==4;
        select_segment(rectangle_fixture?0:2);select_segment(rectangle_fixture?3:5);
        check(view->sketch_selection().size()==2,"Ctrl clicks did not select two native legs");
        const auto* corner=rectangle_fixture?sketch.find_point(sketch.segments[0].first_point_id):nullptr;
        const double corner_x=corner?corner->x:5,corner_y=corner?corner->y:-50;
        const double drag_x=corner_x+(rectangle_fixture?2:-2),drag_y=corner_y+2;
        mouse(screen(corner_x,corner_y),QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
        mouse(screen(corner_x,corner_y),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
        mouse(screen(drag_x,drag_y),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
        mouse(screen(drag_x,drag_y),QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
        view->grabFramebuffer().save(QString::fromStdString((directory/"corner-chain.png").string()));
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();flush();break;}
        const auto saved=window.execute_console_command("save");check(saved.ok,"Save failed");
        const auto after=document::PartDocument::load(path);
        check(after.sketches.front().corner_radii.size()==sketch.corner_radii.size()+1,"Second corner drag did not add a radius");
        check(after.sketches.front().segments==sketch.segments,"Corner drag changed source segments");
        check(after.sketches.front().constraints==sketch.constraints,"Corner drag changed source constraints");
        if(!sketch.corner_radii.empty())check(after.sketches.front().corner_radii.front()==sketch.corner_radii.front(),"Corner drag changed the first radius");
        check(after.sketches.front().evaluated_profile_sketch().arcs.size()==sketch.corner_radii.size()+1,"Reopened profile lost a fillet");
        check(window.execute_console_command("undo").ok,"Corner Undo failed");
        check(window.execute_console_command("save").ok,"Save after Undo failed");
        check(document::PartDocument::load(path).sketches.front().corner_radii.size()==sketch.corner_radii.size(),"One Undo did not restore the first radius only");
        check(window.execute_console_command("redo").ok,"Corner Redo failed");
        check(window.execute_console_command("save").ok,"Save after Redo failed");
        check(document::PartDocument::load(path).sketches.front().serialized()==after.sketches.front().serialized(),"Redo did not restore both radii exactly");
        std::cout<<"Sequential L corner drag, source constraints, Undo/Redo and native reopening passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
