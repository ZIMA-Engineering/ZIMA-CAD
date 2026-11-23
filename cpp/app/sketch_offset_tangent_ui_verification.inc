static int verify_sketch_offset_tangent_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using namespace zima::test::mechanism;
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(400,&loop,&QEventLoop::quit);loop.exec();flush();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"offset-tangent-ui-XXXXXX").string()));require(isolated.isValid(),"Cannot isolate offset tangent GUI test");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());unsigned cases{};
        for(bool reverse:{false,true})for(bool existing:{false,true})for(bool arc:{false,true})for(bool external:{false,true}) {
            auto s=Sketch::create_default();
            auto source=arc?s.add_arc(0,0,0,-10,0,10):s.add_circle(0,0,10);
            auto projected_source=s;const auto projected_source_id=source;
            if(external) {
                const auto geometry=sketcher::sketch_curve_geometry(s,source);s=Sketch::create_default();
                auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
                reference.source_document_id="external-fixture";reference.source_owner_id="source";reference.source_semantic_key="circle";reference.exact_spline=geometry;
                for(unsigned i=0;i<=96;++i){const auto p=kernel::bspline_value(geometry,i/96.);reference.cached_points.push_back({p.x,p.y});}
                s.add_external_reference(reference);source=s.add_external_profile_geometry(reference.id);
            }else {
                s.find_point(arc?s.arcs.front().center_point_id:s.circles.front().center_point_id)->fixed=true;
                auto source_size=arc?s.create_arc_radius_dimension(source):s.create_circle_radius_dimension(source);source_size.locked=true;s.apply_dimension(source_size);
            }
            const auto offset=s.add_offset(source,2,false);
            const auto circle=s.add_circle(10.8,std::sqrt(9.-2.8*2.8),3);
            std::string contact;
            if(existing) {
                auto point=Sketch::create_point(8,0);contact=point.id;s.points.push_back(std::move(point));
                static_cast<void>(s.add_point_on_circle_constraint(contact,offset));
                static_cast<void>(s.add_point_on_circle_constraint(contact,circle));
            }
            auto size=reverse?s.create_circle_diameter_dimension(circle):s.create_circle_radius_dimension(circle);size.locked=true;s.apply_dimension(size);
            auto authored=part(s);s=authored.sketches.front();
            if(external) {
                auto& reference=s.external_references.front();reference.source_document_id=authored.document_id;
                reference.source_owner_id=projected_source.id;reference.source_semantic_key=(arc?"arc:":"circle:")+projected_source_id;
                auto source_container=document::PartDocument::create_sketch_container();projected_source.owner_container_id=source_container.id;
                const auto edited=authored.history.front();authored.history={source_container,edited};authored.sketches={projected_source,s};
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Offset contact"));
                graph.insert({document::PartHistoryKind::Feature,source_container.id});graph.insert({document::PartHistoryKind::Feature,edited.id});
                authored.set_body_history(graph);authored.resolve_constructions();s=authored.sketches.back();
            }
            const auto file=directory/("offset-"+std::to_string(cases)+".prtz");authored.save(file);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            require(window.open_document_path(QString::fromStdString(file.string())),"Offset fixture cannot open");flush();
            const auto* body=authored.body_owner_for_object(s.owner_container_id);
            require(body&&activate_test_body(application,window,body->scope.id),"Offset Body cannot activate");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==s.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            require(row,"Offset Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Offset View missing");view->fit_points({s.world_point(-12,-12),s.world_point(18,12)});settle();
            const auto screen=[&](XY p) {
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);require(ray.has_value(),"Offset ray missing");const auto hit=s.intersect_ray(ray->first,ray->second);require(hit.has_value(),"Offset plane missing");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);
            };
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button=Qt::NoButton,Qt::MouseButtons held=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](QPointF p){mouse(p,QEvent::MouseMove);mouse(p,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(p,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto curve_point=[&](const std::string& id,double t){const auto p=kernel::bspline_value(sketcher::sketch_curve_geometry(*window.active_sketch_for_test(),id),t);return screen({p.x,p.y});};
            const auto original=window.active_sketch_for_test()->serialized();
            window.findChild<QAction*>("sketchTangentAction")->trigger();flush();
            click(curve_point(reverse?circle:offset,reverse?.05:.25));click(curve_point(reverse?offset:circle,reverse?.25:.05));
            auto draft=*window.active_sketch_for_test();
            const auto tangent=std::ranges::find_if(draft.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;});
            require(tangent!=draft.constraints.end(),"Mouse selection did not create offset tangency");
            require(!existing||tangent->first_point_id==contact,"Mouse tangency duplicated existing C");
            const auto expected=draft.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==original,"Offset T is not one Undo transaction");
            window.findChild<QAction*>("redoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==expected,"Offset T Redo changed identity");
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            for(double radius:{3.5,2.5,3.}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+size.id,[](const auto& d){return d.reference.semantic_key;});require(shown!=view->mesh().dimensions.end(),"Offset circle dimension missing");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=s.id;candidate.semantic_key="dimension:"+size.id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto label=view->candidate_dimension_label_position(candidate);require(label.has_value(),"Offset dimension label missing");click(*label);mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");require(editor&&editor->isVisible(),"Offset radius editor missing");editor->setText(QString::number(radius*(reverse?2:1)));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                draft=*window.active_sketch_for_test();const auto c=*std::ranges::find(draft.circles,circle,&SketchCircle::id);const auto p=position(draft,c.center_point_id);
                require(std::abs(c.radius-radius)<1e-7&&std::abs(std::hypot(p[0],p[1])-8-radius)<1e-7,"GUI offset size edit broke tangency");
                const double angle=std::atan2(p[1],p[0])+.1;const XY target{(8+radius)*std::cos(angle),(8+radius)*std::sin(angle)};
                mouse(screen(p),QEvent::MouseMove);mouse(screen(p),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(screen(target),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen(target),QEvent::MouseButtonRelease,Qt::LeftButton);
                draft=*window.active_sketch_for_test();const auto after=position(draft,c.center_point_id);
                require(std::hypot(after[0]-p[0],after[1]-p[1])>.1&&std::abs(std::hypot(after[0],after[1])-8-radius)<1e-7,"GUI offset circle drag failed");
                if(external) {
                    require(draft.external_references==s.external_references,"GUI offset edit rewrote external reference");
                    for(const auto& id:s.import_blocks.front().point_ids) {
                        const auto& before=*s.find_point(id);auto after=*draft.find_point(id);
                        require(std::hypot(after.x-before.x,after.y-before.y)<1e-12,"GUI offset edit moved projected source");
                        after.x=before.x;after.y=before.y;require(after==before,"GUI offset edit changed projected point identity or flags");
                    }
                }
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)){buttons->button(QDialogButtonBox::Ok)->click();flush();break;}
            require(window.execute_console_command("save").ok,"Offset save failed");
            const auto reopened=document::PartDocument::load(file).sketches.back().serialized();
            if(reopened!=draft.serialized()) {
                std::ofstream(parent/"offset-before.json")<<draft.serialized();
                std::ofstream(parent/"offset-reopened.json")<<reopened;
                throw std::runtime_error("Offset reopen changed definition in case "+std::to_string(cases));
            }
            ++cases;
        }
        std::cout<<cases<<" offset tangent GUI cases passed: mouse selection, C reuse, R/diameter edits, drags, Undo/Redo and reopen\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Offset tangent GUI: "<<e.what()<<'\n';return 1;}
}
