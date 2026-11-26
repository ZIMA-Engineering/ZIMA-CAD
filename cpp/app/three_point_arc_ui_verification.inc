static int verify_three_point_arc_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        for(int side:{-1,1}) {
            auto sketch=sketcher::Sketch::create_default();
            static_cast<void>(sketch.add_point(-30,-30));static_cast<void>(sketch.add_point(90,90));
            auto part=test::mechanism::part(sketch);sketch=part.sketches.front();
            const auto path=directory/("three-point-arc-"+std::to_string(side)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));
            window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1400,950);window.show();flush();
            check(window.open_document_path(QString::fromStdString(path.string())),"Three-point fixture open failed");flush();
            check(activate_test_body(application,window,part.body_owner_for_object(sketch.owner_container_id)->scope.id),"Arc Body activation failed");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch")row=*i;
            check(row,"Arc Sketch row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Arc View missing");
            const auto live=[&]() -> const sketcher::Sketch& {return *window.active_sketch_for_test();};
            const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Arc cursor ray unavailable");return *live().intersect_ray(ray->first,ray->second);};
            const auto screen=[&](std::array<double,2> p) {
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);
            };
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](std::array<double,2> p) {const auto at=screen(p);mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* command=window.findChild<QAction*>("sketchThreePointArcAction");check(command&&command->isEnabled(),"Three-point action unavailable");
            command->trigger();flush();const auto before=live().serialized();const auto calculations=kernel::body_calculation_count.load();
            click({10,12});click({50,12});click({30,12});
            check(live().serialized()==before,"Collinear third click committed a document change");
            mouse(screen({30,12+side*28.}),QEvent::MouseMove,Qt::NoButton);
            check(!view->transient_edges().empty()&&view->transient_edges().front().points.size()>20,"Three-point arc has no curved live preview");
            click({30,12+side*28.});check(live().arcs.size()==1,"Three clicks did not create an ordinary arc");
            const auto contact=std::ranges::find(live().constraints,sketcher::ConstraintKind::PointOnCircle,&sketcher::SketchConstraint::kind);
            check(contact!=live().constraints.end(),"Three-point arc lost its editable through point");const auto through=contact->first_point_id;
            const auto accepted=live().serialized();window.findChild<QAction*>("undoAction")->trigger();flush();check(live().serialized()==before,"Arc creation Undo failed");
            window.findChild<QAction*>("redoAction")->trigger();flush();check(live().serialized()==accepted,"Arc creation Redo failed");
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            for(double delta:{2.,-3.,1.}) {
                const auto old=*live().find_point(through);const auto a=screen({old.x,old.y}),b=screen({old.x+delta,old.y+delta});const auto original=live().serialized();
                mouse(a,QEvent::MouseMove,Qt::NoButton);mouse(a,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(b,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(b,QEvent::MouseButtonRelease,Qt::LeftButton);
                check(live().serialized()!=original,"Mouse drag did not move the three-point arc contact");
                const auto arc=live().arcs.front();const auto* c=live().find_point(arc.center_point_id);const auto* p=live().find_point(through);
                check(std::abs(std::hypot(p->x-c->x,p->y-c->y)-arc.radius)<1e-6,"GUI drag broke circle incidence");
                const auto moved=live().serialized();window.findChild<QAction*>("undoAction")->trigger();flush();check(live().serialized()==original,"Arc drag Undo failed");
                window.findChild<QAction*>("redoAction")->trigger();flush();check(live().serialized()==moved,"Arc drag Redo failed");
            }
            auto* dimension_action=window.findChild<QAction*>("sketchUniversalDimensionAction");
            check(dimension_action&&dimension_action->isEnabled(),"Arc dimension command unavailable");
            const auto arc=live().arcs.front();const auto* center=live().find_point(arc.center_point_id);
            const double pick_angle=arc.start_angle+.28*(arc.end_angle-arc.start_angle);
            dimension_action->trigger();flush();
            click({center->x+arc.radius*std::cos(pick_angle),center->y+arc.radius*std::sin(pick_angle)});
            click({80,70});
            if(auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");editor&&editor->isVisible()) {
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            }
            check(live().dimensions.size()==1&&live().dimensions.front().kind==sketcher::DimensionKind::Radius,
                "Three-point arc cannot be dimensioned through ordinary mouse entry");
            const auto size_id=live().dimensions.front().id;
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            for(double factor:{1.08,.94,1.03}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+size_id,
                    [](const auto& value){return value.reference.semantic_key;});
                check(shown!=view->mesh().dimensions.end(),"Arc dimension presentation missing");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
                candidate.owner_id=sketch.id;candidate.semantic_key="dimension:"+size_id;
                candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto label=view->candidate_dimension_label_position(candidate);check(label.has_value(),"Arc dimension label missing");
                mouse(*label,QEvent::MouseMove,Qt::NoButton);
                mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
                mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton);
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                check(editor&&editor->isVisible(),"Three-point arc dimension value editor missing");
                editor->setText(QString::number(arc.radius*factor,'g',15));
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                const auto current=live().arcs.front();const auto* c=live().find_point(current.center_point_id);
                check(std::abs(current.radius-arc.radius*factor)<1e-6,"GUI dimension edit lost requested arc radius");
                for(const auto& id:{current.start_point_id,current.end_point_id,through}) {
                    const auto* point=live().find_point(id);
                    check(std::abs(std::hypot(point->x-c->x,point->y-c->y)-current.radius)<1e-6,
                        "GUI dimension edit broke arc incidence");
                }
                const auto edited=live().serialized();window.findChild<QAction*>("undoAction")->trigger();flush();
                window.findChild<QAction*>("redoAction")->trigger();flush();check(live().serialized()==edited,"Arc dimension edit Undo/Redo failed");
            }
            command->trigger();flush();const auto unchanged=live().serialized();click({-10,15});
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(live().serialized()==unchanged,"Escape committed an unfinished three-point arc");
            check(kernel::body_calculation_count.load()==calculations,"Sketch arc interaction invoked OCCT");
            window.grab().save(QString::fromStdString((directory/("three-point-arc-"+std::to_string(side)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};for(auto* d:window.findChildren<QDialog*>())if(auto* p=dynamic_cast<app::SketchPropertiesDialog*>(d);p&&p->isVisible())properties=p;
            check(properties,"Arc did not return to Sketch Properties");const auto result=properties->pending_value().first;
            properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto saved=document::PartDocument::load(path);check(saved.sketches.front().arcs==result.arcs&&saved.sketches.front().constraints==result.constraints,"Arc save/reopen lost native contacts");
        }
        std::cout<<"Three-point arc GUI: both sides, actual mouse creation/preview, collinear rejection, point dragging, radius entry and repeated value edits, Escape, Undo/Redo, native reopen and zero OCCT passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
