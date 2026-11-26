static int verify_sketch_contour_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;
    unsigned checks{},drags{};
    const auto check=[&](bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    struct ModalTrap:QObject {
        QString error;
        bool eventFilter(QObject* object,QEvent* event) override {
            if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object)) {
                box->setAttribute(Qt::WA_DontShowOnScreen);error=box->text();QTimer::singleShot(0,box,[box]{box->accept();});
            }
            return false;
        }
    } trap;
    application.installEventFilter(&trap);
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"contour-ui-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create contour GUI directory");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        QSettings settings(isolated.filePath("config.ini"),QSettings::IniFormat);settings.setValue("Application/Language","en");settings.sync();
        for(int context:{0,1,2})
        for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
        for(int kind:{0,1,2,3}) {
            const bool assembly_mode=context==1,profile_mode=context==2;
            if(profile_mode&&(plane!=sketcher::SketchPlane::XY||(kind!=0&&kind!=2)))continue;
            std::cout<<"Contour GUI case context="<<context<<" plane="<<int(plane)<<" kind="<<kind<<std::endl;
            auto s=sketcher::Sketch::create_default();s.plane=plane;s.plane_auto=false;s.refresh_default_frame();
            const bool circle=kind>=2;
            const auto id=circle?s.add_circle(30,25,12):s.add_segment(10,15,65,25);
            const auto center=circle?s.circles.front().center_point_id:std::string{};
            const auto a=circle?std::string{}:s.segments.front().first_point_id,b=circle?std::string{}:s.segments.front().second_point_id;
            if(kind==1)s.find_point(a)->fixed=true;
            auto dimension=circle?s.create_circle_diameter_dimension(id):s.create_segment_dimension(id);
            dimension.locked=kind==3;s.apply_dimension(dimension);
            check(s.set_dimension_placement(dimension.id,25,-10),"Cannot place contour dimension");
            auto part=test::mechanism::part(s);s=part.sketches.front();
            if(profile_mode) {
                auto feature=document::PartDocument::create_sweep3d_container();s.owner_container_id=feature.id;
                if(!circle){s.segments.front().construction=true;static_cast<void>(s.add_circle(-12,15,5));}
                auto first=document::PartDocument::create_construction(document::ConstructionKind::Point),second=first;
                first.parent_construction_id=feature.sweep3d.path.id;second=document::PartDocument::create_construction(document::ConstructionKind::Point);
                second.parent_construction_id=feature.sweep3d.path.id;second.origin={0,0,40};feature.sweep3d.path.curve_points={first,second};
                feature.sweep3d.profiles.push_back({"contour-profile",first.id,s.id,s.serialized()});
                check(document::PartDocument::reframe_sweep3d_profile(feature,0),"Cannot frame contour section");
                s=sketcher::Sketch::from_serialized(feature.sweep3d.profiles.front().sketch_serialized);
                part.history={feature};part.sketches.clear();document::BodyHistoryGraph bodies;static_cast<void>(bodies.create_body("Contour section"));
                bodies.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(bodies);
            }
            const auto file=directory/("contour-"+std::to_string(context)+"-"+std::to_string(int(plane))+"-"+std::to_string(kind)+(assembly_mode?".asmz":".prtz"));
            if(assembly_mode){auto group=assembly::AssemblyDocument::create_default();auto owner=part.history.front();owner.feature.sketch_id=s.id;group.insert_sketch(s,owner);group.resolve_constructions();group.save(file);}
            else part.save(file);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1400,950);window.show();flush();
            check(window.open_document_path(QString::fromStdString(file.string())),"Cannot open contour fixture");flush();
            if(!assembly_mode)check(activate_test_body(application,window,part.body_owner_for_object(s.owner_container_id)->scope.id),"Cannot activate contour Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==(assembly_mode||profile_mode?s.owner_container_id:s.id)&&(*it)->data(0,Qt::UserRole+3)==(assembly_mode?"assembly-sketch-container":profile_mode?"part-container":"part-sketch")){row=*it;break;}
            check(row,"No contour Sketch row");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");
            if(profile_mode){auto* profiles=window.findChild<QTableWidget*>("sweep3DProfiles");check(profiles,"No section table");open=qobject_cast<QPushButton*>(profiles->cellWidget(0,1));}
            check(open,"No Open Sketch button");open->click();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));check(view,"No contour View");
            QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();flush();
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            view->fit_points({s.world_point(-20,-25),s.world_point(100,85)});
            QEventLoop fitted;QTimer::singleShot(1000,&fitted,&QEventLoop::quit);fitted.exec();flush();
            const auto live=[&]()->const sketcher::Sketch&{const auto* current=window.active_sketch_for_test();check(current,"Sketch editor closed");return *current;};
            const auto xy=[&](const std::string& p){const auto* value=live().find_point(p);return std::array{value->x,value->y};};
            const auto handle=[&]{if(circle){const auto c=xy(center);return std::array{c[0]+live().circles.front().radius,c[1]};}const auto p=xy(a),q=xy(b);return std::array{.3*p[0]+.7*q[0],.3*p[1]+.7*q[1]};};
            const auto screen=[&](std::array<double,2> p){
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Contour cursor ray missing");
                    const auto hit=live().intersect_ray(ray->first,ray->second);check(hit.has_value(),"Contour cursor plane missing");return *hit;};
                const auto a0=local({0,0}),b0=local({100,0}),c0=local({0,100});
                const double ux=(b0[0]-a0[0])/100,uy=(b0[1]-a0[1])/100,vx=(c0[0]-a0[0])/100,vy=(c0[1]-a0[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a0[0])*vy-(p[1]-a0[1])*vx)/det,(ux*(p[1]-a0[1])-uy*(p[0]-a0[0]))/det);
            };
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton,Qt::KeyboardModifiers modifiers=Qt::NoModifier){
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,modifiers);QApplication::sendEvent(view,&event);flush();
            };
            unsigned presses{};
            const auto press=[&](QPointF at){++presses;mouse(at,QEvent::MouseMove,Qt::NoButton);
                // Confirmed Sketch selection intentionally suppresses hover.
                // The next press still uses the View's common candidate picker.
                mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);const auto selected=view->confirmed_candidate();
                check(selected&&selected->semantic_key==(circle?"circle:":"segment:")+id,"Actual contour picking failed press="+std::to_string(presses)+" picked="+(selected?selected->semantic_key:"none")+" x="+std::to_string(at.x())+" y="+std::to_string(at.y()));
            };
            auto from=screen(handle());auto before=live().serialized();press(from);
            const auto calculations=kernel::body_calculation_count.load();
            mouse(from+QPointF(1,1),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(from,QEvent::MouseButtonRelease,Qt::LeftButton);
            check(live().serialized()==before,"Click jitter changed geometry");
            // Ctrl selection must never arm an outline gesture.
            mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton,Qt::ControlModifier);
            mouse(from+QPointF(40,20),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton,Qt::ControlModifier);
            mouse(from+QPointF(40,20),QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton,Qt::ControlModifier);
            check(live().serialized()==before,"Ctrl selection dragged geometry");
            QKeyEvent clear(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&clear);flush();
            for(const auto delta:{std::array{3.,4.},std::array{-2.,-3.},std::array{4.,2.}}) {
                before=live().serialized();const auto source=live();const auto p=handle();from=screen(p);const auto to=screen({p[0]+delta[0],p[1]+delta[1]});
                const auto from_ray=view->ray_at(from),to_ray=view->ray_at(to);check(from_ray&&to_ray,"Contour test ray missing");
                const auto actual_from=source.intersect_ray(from_ray->first,from_ray->second),actual_to=source.intersect_ray(to_ray->first,to_ray->second);
                check(actual_from&&actual_to,"Contour test ray missed Sketch");press(from);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=live().serialized();++drags;
                if(kind==3){check(after==before,"Locked circle changed in GUI");continue;}
                check(after!=before,"Actual outline drag did not move");
                if(circle){const auto c=source.find_point(center);const double expected=source.circles.front().radius+std::hypot((*actual_to)[0]-c->x,(*actual_to)[1]-c->y)-std::hypot((*actual_from)[0]-c->x,(*actual_from)[1]-c->y);
                    check(std::abs(live().circles.front().radius-expected)<1e-6,"GUI circle radius equation");}
                else {const auto pa=xy(a),pb=xy(b);check(std::abs(std::hypot(pb[0]-pa[0],pb[1]-pa[1])-std::hypot(55.,10.))<1e-6,"GUI line length equation");if(kind==1)check(std::hypot(pa[0]-10,pa[1]-15)<1e-6,"GUI pivot moved");}
                window.findChild<QAction*>("undoAction")->trigger();flush();check(live().serialized()==before,"One contour Undo did not restore original");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(live().serialized()==after,"Contour Redo changed result");
                before=after;const auto cancel_from=screen(handle());press(cancel_from);mouse(cancel_from+QPointF(30,-25),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();mouse(cancel_from,QEvent::MouseButtonRelease,Qt::LeftButton);
                check(live().serialized()==before,"Escape committed contour draft");
            }
            before=live().serialized();from=screen(handle());press(from);
            const auto back=circle?screen({xy(center)[0],xy(center)[1]+live().circles.front().radius}):from;
            mouse(from+QPointF(40,-30),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);
            mouse(back,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(back,QEvent::MouseButtonRelease,Qt::LeftButton);
            check(live().serialized()==before,"Returning to original contour size/position committed a change");
            for(double value:{circle?30.:60.,circle?22.:50.,circle?26.:55.}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+dimension.id,[](const auto& d){return d.reference.semantic_key;});
                check(shown!=view->mesh().dimensions.end(),"Contour dimension missing after dragging");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=s.id;candidate.semantic_key="dimension:"+dimension.id;
                candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto at=view->candidate_dimension_label_position(candidate);check(at.has_value(),"Contour dimension label missing");
                for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    const bool down=type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick;
                    mouse(*at,type,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,down?Qt::LeftButton:Qt::NoButton);
                }
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Contour dimension value editor missing");
                editor->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                if(circle)check(std::abs(live().circles.front().radius-value/2)<1e-6,"GUI diameter edit failed after contour dragging");
                else {const auto p=xy(a),q=xy(b);check(std::abs(std::hypot(q[0]-p[0],q[1]-p[1])-value)<1e-6,"GUI length edit failed after contour dragging");}
                const auto p=xy(circle?center:b);
                const auto from_point=screen(p),to_point=screen({p[0]+2,p[1]+1});const auto original=live().serialized();
                mouse(from_point,QEvent::MouseMove,Qt::NoButton);mouse(from_point,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);const auto pick=view->confirmed_candidate();
                check(pick&&pick->semantic_key=="point:"+(circle?center:b),"Native point picking regressed");
                mouse(to_point,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to_point,QEvent::MouseButtonRelease,Qt::LeftButton);
                check(live().serialized()!=original,"Point no longer moves after outline/dimension edits");
            }
            check(kernel::body_calculation_count.load()==calculations,"Sketch interaction calculated a solid");
            const auto expected=live().serialized();window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)){buttons->button(QDialogButtonBox::Ok)->click();flush();break;}
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto loaded=assembly_mode?assembly::AssemblyDocument::load(file).sketches.front():profile_mode?
                sketcher::Sketch::from_serialized(document::PartDocument::load(file).history.front().sweep3d.profiles.front().sketch_serialized):document::PartDocument::load(file).sketches.front();
            check(loaded.serialized()==expected,"GUI contour native save/reopen");
            check(trap.error.isEmpty(),"Unexpected contour GUI message: "+trap.error.toStdString());
            view->grabFramebuffer().save(QString::fromStdString((parent/("contour-"+std::to_string(int(plane))+"-"+std::to_string(kind)+".png")).string()));
        }
        std::cout<<"Contour GUI: "<<drags<<" actual drags, "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Contour GUI: "<<e.what()<<'\n';return 1;}
}
