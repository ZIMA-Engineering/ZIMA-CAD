static int verify_sketch_offset_tangent_ui(QApplication& application,const std::filesystem::path& parent,bool noncircular=false) {
    using namespace zima;using namespace zima::test::mechanism;
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(400,&loop,&QEventLoop::quit);loop.exec();flush();};
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"offset-tangent-ui-XXXXXX").string()));require(isolated.isValid(),"Cannot isolate offset tangent GUI test");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());unsigned cases{},verified{};
        for(bool target_arc:noncircular?std::vector{false,true}:std::vector{false})
        for(bool reverse:{false,true})for(bool existing:{false,true})for(bool arc:{false,true})for(bool external:{false,true}) {
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_OFFSET_CASE")&&cases!=qEnvironmentVariableIntValue("ZIMA_VERIFY_OFFSET_CASE")){++cases;continue;}
            std::cout<<"Offset GUI case "<<cases<<": target_arc="<<target_arc<<", reverse="<<reverse<<", existing="<<existing<<", spline/arc="<<arc<<", external="<<external<<std::endl;
            auto s=Sketch::create_default();
            auto source=arc?s.add_arc(0,0,0,-10,0,10):s.add_circle(0,0,10);
            if(noncircular) {
                s=Sketch::create_default();source=arc?s.add_bspline({{-20,0},{-8,8},{7,4},{20,0}}):s.add_ellipse(0,0,15,0,0,10);
            }
            auto projected_source=s;const auto projected_source_id=source;
            if(external) {
                const auto geometry=sketcher::sketch_curve_geometry(s,source);s=Sketch::create_default();
                auto reference=Sketch::create_external_reference(ExternalReferenceKind::Edge);
                reference.source_document_id="external-fixture";reference.source_owner_id="source";reference.source_semantic_key="circle";reference.exact_spline=geometry;
                for(unsigned i=0;i<=96;++i){const auto p=kernel::bspline_value(geometry,i/96.);reference.cached_points.push_back({p.x,p.y});}
                s.add_external_reference(reference);source=s.add_external_profile_geometry(reference.id);
            }else if(noncircular) {
                for(auto& point:s.points)point.fixed=true;
            }else {
                s.find_point(arc?s.arcs.front().center_point_id:s.circles.front().center_point_id)->fixed=true;
                auto source_size=arc?s.create_arc_radius_dimension(source):s.create_circle_radius_dimension(source);source_size.locked=true;s.apply_dimension(source_size);
            }
            const auto offset=s.add_offset(source,2,false);
            const auto support=sketcher::sketch_curve_geometry(s,offset);
            const auto cp=kernel::bspline_value(support,.4),ct=kernel::bspline_derivative(support,.4);
            const double length=std::hypot(ct.x,ct.y);
            const double cx=noncircular?cp.x-2.8*ct.y/length+std::sqrt(9.-2.8*2.8)*ct.x/length:10.8;
            const double cy=noncircular?cp.y+2.8*ct.x/length+std::sqrt(9.-2.8*2.8)*ct.y/length:std::sqrt(9.-2.8*2.8);
            const double angle=std::atan2(cp.y-cy,cp.x-cx),start=angle-(existing?0:.5);
            // Keep a visibly separate arc interior for both selection orders;
            // a short near-tangent arc overlaps the support's pick tolerance.
            const auto circle=target_arc?s.add_arc(cx,cy,cx+3*std::cos(start),cy+3*std::sin(start),cx+3*std::cos(angle+2),cy+3*std::sin(angle+2)):s.add_circle(cx,cy,3);
            struct Radial { std::string center_point_id;double radius; };
            const auto radial=[&](const Sketch& value) -> Radial {
                if(target_arc) {const auto& c=*std::ranges::find(value.arcs,circle,&SketchArc::id);return {c.center_point_id,c.radius};}
                const auto& c=*std::ranges::find(value.circles,circle,&SketchCircle::id);return {c.center_point_id,c.radius};
            };
            std::string contact;
            if(existing) {
                if(target_arc)contact=s.arcs.back().start_point_id;
                else {auto point=Sketch::create_point(noncircular?cp.x:8,noncircular?cp.y:0);contact=point.id;s.points.push_back(std::move(point));}
                static_cast<void>(s.add_point_on_circle_constraint(contact,offset));
                if(!target_arc)static_cast<void>(s.add_point_on_circle_constraint(contact,circle));
            }
            auto size=target_arc?(reverse?s.create_arc_diameter_dimension(circle):s.create_arc_radius_dimension(circle)):
                (reverse?s.create_circle_diameter_dimension(circle):s.create_circle_radius_dimension(circle));
            size.locked=true;s.apply_dimension(size);
            auto authored=part(s);s=authored.sketches.front();
            if(external) {
                auto& reference=s.external_references.front();reference.source_document_id=authored.document_id;
                reference.source_owner_id=projected_source.id;reference.source_semantic_key=(noncircular?(arc?"bspline:":"ellipse:"):(arc?"arc:":"circle:"))+projected_source_id;
                auto source_container=document::PartDocument::create_sketch_container();projected_source.owner_container_id=source_container.id;
                const auto edited=authored.history.front();authored.history={source_container,edited};authored.sketches={projected_source,s};
                document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Offset contact"));
                graph.insert({document::PartHistoryKind::Feature,source_container.id});graph.insert({document::PartHistoryKind::Feature,edited.id});
                authored.set_body_history(graph);authored.resolve_constructions();s=authored.sketches.back();
            }
            const auto file=directory/("offset-"+std::to_string(cases)+".prtz");authored.save(file);
            const std::array<const char*,5> languages{"cs","en","de","fr","ru"};
            QSettings config(isolated.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",languages[cases%languages.size()]);
            config.setValue("Paths/Localization",QString::fromStdString(std::filesystem::absolute("config/localization").string()));config.sync();
            const auto settings=app::ApplicationSettings::load(isolated.path());
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,900);window.show();flush();
            require(window.open_document_path(QString::fromStdString(file.string())),"Offset fixture cannot open");flush();
            const auto* body=authored.body_owner_for_object(s.owner_container_id);
            require(body&&activate_test_body(application,window,body->scope.id),"Offset Body cannot activate");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==s.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            require(row,"Offset Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Offset View missing");
            view->fit_points(noncircular?std::vector<kernel::Vec3>{s.world_point(-22,-20),s.world_point(22,20)}:
                std::vector<kernel::Vec3>{s.world_point(-12,-12),s.world_point(18,12)});settle();
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
            const auto click_curve=[&](const std::string& id,double preferred) {
                // Radius/diameter witnesses and point grips have legitimate
                // priority at crossings. Select a clear curve interior using
                // the same ordered picker consumed by the actual mouse event.
                std::vector<double> parameters{preferred};
                for(int i=1;i<20;++i)parameters.push_back(i/20.);
                for(double parameter:parameters) {
                    const auto pixel=curve_point(id,parameter);
                    const auto hits=view->selection_candidates_at(pixel);
                    if(!hits.empty()&&hits.front().kind==viewer::CandidateKind::SketchCurve&&
                       hits.front().owner_id==s.id&&hits.front().semantic_key.ends_with(":"+id)) {
                        click(pixel);return;
                    }
                }
                throw std::runtime_error("Offset tangent curve has no clear mouse target");
            };
            const auto original=window.active_sketch_for_test()->serialized();
            require(window.findChild<QAction*>("sketchTangentAction")->isEnabled(),"Tangent action is disabled");
            window.findChild<QAction*>("sketchTangentAction")->trigger();flush();
            // The diameter's witness line crosses the arc midpoint.
            const double radial_pick=target_arc?.72:.05;
            click_curve(reverse?circle:offset,reverse?radial_pick:.25);
            if(noncircular&&!reverse) {
                const auto expected=settings.qt_translations.value("Tečná vazba: vyberte řízenou úsečku, kružnici nebo oblouk.");
                const auto labels=window.findChildren<QLabel*>();require(std::ranges::any_of(labels,[&](auto* label){return label->text()==expected;}),"Offset tangent prompt is untranslated");
            }
            click_curve(reverse?offset:circle,reverse?.25:radial_pick);
            auto draft=*window.active_sketch_for_test();
            if(std::ranges::none_of(draft.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;})) {
                std::ofstream(parent/"offset-selection-failed.json")<<draft.serialized();
                window.grab().save(QString::fromStdString((parent/"offset-selection-failed.png").string()));
                for(auto* label:window.findChildren<QLabel*>())if(label->isVisible())std::cerr<<label->text().toStdString()<<'\n';
                for(const auto& candidate:view->selection_candidates_at(curve_point(reverse?offset:circle,reverse?.25:radial_pick)))
                    std::cerr<<"Candidate "<<int(candidate.kind)<<' '<<candidate.semantic_key<<'\n';
            }
            const auto equations=[&](const Sketch& value) {
                const auto c=radial(value);const auto center=position(value,c.center_point_id);
                const auto tangent=std::ranges::find_if(value.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;});
                require(tangent!=value.constraints.end(),"Offset tangency missing");const auto contact=position(value,tangent->first_point_id);
                const auto projection=kernel::project_bspline(sketcher::sketch_curve_geometry(value,offset),{contact[0],contact[1],0});
                const double tx=projection.tangent.x,ty=projection.tangent.y,len=std::hypot(tx,ty),x=center[0]-contact[0],y=center[1]-contact[1];
                require(projection.squared_distance<1e-14&&std::abs(std::hypot(x,y)-c.radius)<1e-7&&std::abs(x*tx+y*ty)/len<1e-7,"GUI offset contact equations failed");
                const auto on_target=value.project_point_to_curve(circle,contact[0],contact[1]);
                require(on_target&&std::hypot((*on_target)[0]-contact[0],(*on_target)[1]-contact[1])<1e-7,"GUI contact left finite arc domain");
            };
            equations(draft);
            const auto tangent=std::ranges::find_if(draft.constraints,[](const auto& c){return c.kind==ConstraintKind::Tangent;});
            require(tangent!=draft.constraints.end(),"Mouse selection did not create offset tangency");
            require(!existing||tangent->first_point_id==contact,"Mouse tangency duplicated existing C");
            const auto expected=draft.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==original,"Offset T is not one Undo transaction");
            window.findChild<QAction*>("redoAction")->trigger();flush();require(window.active_sketch_for_test()->serialized()==expected,"Offset T Redo changed identity");
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            for(double radius:{3.5,2.5,3.}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+size.id,[](const auto& d){return d.reference.semantic_key;});require(shown!=view->mesh().dimensions.end(),"Offset circle dimension missing");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=s.id;candidate.semantic_key="dimension:"+size.id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto label=view->candidate_dimension_label_position(candidate);require(label.has_value(),"Offset dimension label missing");
                // A contact/endpoint can overlap the label center and has
                // ordinary point-picking priority. Click a clear part of the
                // same label/witness, as a user would, through the real picker.
                std::optional<QPointF> label_hit;
                for(int radius=0;radius<=12&&!label_hit;radius+=2)
                    for(int x=-radius;x<=radius&&!label_hit;x+=2)
                        for(int y=-radius;y<=radius&&!label_hit;y+=2) {
                            const auto pixel=QPointF(*label)+QPointF(x,y);
                            const auto hits=view->selection_candidates_at(pixel);
                            if(!hits.empty()&&hits.front().kind==viewer::CandidateKind::Dimension&&hits.front().semantic_key==candidate.semantic_key)
                                label_hit=pixel;
                        }
                require(label_hit.has_value(),"Offset dimension has no clear mouse target");
                click(*label_hit);mouse(*label_hit,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);mouse(*label_hit,QEvent::MouseButtonRelease,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                if(!editor||!editor->isVisible()) {
                    window.grab().save(QString::fromStdString((parent/"offset-dimension-failed.png").string()));
                    std::cerr<<"Dimension click case="<<cases<<" radius="<<radius<<" pixel="<<label->x()<<','<<label->y()<<'\n';
                    for(const auto& hit:view->selection_candidates_at(*label))std::cerr<<"Candidate "<<int(hit.kind)<<' '<<hit.semantic_key<<'\n';
                }
                require(editor&&editor->isVisible(),"Offset radius editor missing");editor->setText(QString::number(radius*(reverse?2:1)));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                draft=*window.active_sketch_for_test();const auto c=radial(draft);const auto p=position(draft,c.center_point_id);
                require(std::abs(c.radius-radius)<1e-7,"GUI offset size edit failed");equations(draft);
                const double angle=std::atan2(p[1],p[0])+.1;XY target{(8+radius)*std::cos(angle),(8+radius)*std::sin(angle)};
                if(noncircular) {
                    const auto live=sketcher::sketch_curve_geometry(draft,offset);
                    const auto& relation=*std::ranges::find_if(draft.constraints,[](const auto& item){return item.kind==ConstraintKind::Tangent;});
                    const auto contact=position(draft,relation.first_point_id);
                    const auto projection=kernel::project_bspline(live,{contact[0],contact[1],0});
                    const auto parameter=projection.parameter+(projection.parameter>.9?-.025:.025);
                    const auto q=kernel::bspline_value(live,parameter),t=kernel::bspline_derivative(live,parameter);
                    const double len=std::hypot(t.x,t.y),r=relation.tangent_internal?-radius:radius;
                    target={q.x-r*t.y/len,q.y+r*t.x/len};
                }
                mouse(screen(p),QEvent::MouseMove);mouse(screen(p),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(screen(target),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen(target),QEvent::MouseButtonRelease,Qt::LeftButton);
                draft=*window.active_sketch_for_test();const auto after=position(draft,c.center_point_id);
                if(std::hypot(after[0]-p[0],after[1]-p[1])<=.1) {
                    std::ofstream(parent/"offset-drag-failed.json")<<draft.serialized();
                    std::cerr<<"Drag center="<<c.center_point_id<<" from="<<p[0]<<','<<p[1]<<" target="<<target[0]<<','<<target[1]
                        <<" screen="<<screen(p).x()<<','<<screen(p).y()<<'\n';
                    for(const auto& candidate:view->selection_candidates_at(screen(p)))std::cerr<<"Candidate "<<int(candidate.kind)<<' '<<candidate.semantic_key<<'\n';
                    throw std::runtime_error("GUI offset circle drag failed");
                }
                equations(draft);
                if(target_arc) {
                    const auto& curve=*std::ranges::find(draft.arcs,circle,&SketchArc::id);
                    const auto endpoint=curve.end_point_id;const auto a=position(draft,endpoint),o=position(draft,curve.center_point_id);
                    const double theta=std::atan2(a[1]-o[1],a[0]-o[0])+.08;
                    const XY next{o[0]+radius*std::cos(theta),o[1]+radius*std::sin(theta)};
                    mouse(screen(a),QEvent::MouseMove);mouse(screen(a),QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                    mouse(screen(next),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen(next),QEvent::MouseButtonRelease,Qt::LeftButton);
                    draft=*window.active_sketch_for_test();const auto moved=position(draft,endpoint);
                    require(std::hypot(moved[0]-a[0],moved[1]-a[1])>.1,"GUI tangent arc end did not move");equations(draft);
                }
                if(external) {
                    require(draft.external_references==s.external_references,"GUI offset edit rewrote external reference");
                    for(const auto& id:s.import_blocks.front().point_ids) {
                        const auto& before=*s.find_point(id);auto after=*draft.find_point(id);
                        require(std::hypot(after.x-before.x,after.y-before.y)<1e-12,"GUI offset edit moved projected source");
                        after.x=before.x;after.y=before.y;require(after==before,"GUI offset edit changed projected point identity or flags");
                    }
                }
            }
            if(noncircular&&cases==0)require(window.grab().save(QString::fromStdString((parent/"noncircular-offset-tangent.png").string())),"Cannot retain offset tangent GUI evidence");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)){buttons->button(QDialogButtonBox::Ok)->click();flush();break;}
            require(window.execute_console_command("save").ok,"Offset save failed");
            const auto reopened=document::PartDocument::load(file).sketches.back().serialized();
            if(reopened!=draft.serialized()) {
                std::ofstream(parent/"offset-before.json")<<draft.serialized();
                std::ofstream(parent/"offset-reopened.json")<<reopened;
                throw std::runtime_error("Offset reopen changed definition in case "+std::to_string(cases));
            }
            ++cases;++verified;
        }
        std::cout<<verified<<(noncircular?" noncircular":" circular")<<" offset tangent GUI cases passed: mouse selection, C reuse, R/diameter edits, drags, Undo/Redo and reopen\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Offset tangent GUI: "<<e.what()<<'\n';return 1;}
}
