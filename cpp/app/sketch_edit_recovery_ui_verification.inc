static int verify_sketch_edit_recovery_ui(QApplication& application,
        const std::filesystem::path& parent_directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](app::AssemblyWorkspaceWindow& window,const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",std::move(args)}}.dump()));
        if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);flush();return result.data;
    };
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
        QTemporaryDir isolated(QString::fromStdString((parent_directory/"sketch-recovery-XXXXXX").string()));
        check(isolated.isValid(),"Cannot create isolated recovery workspace");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_SKETCH_EDIT_SOURCE").toStdString());
        std::vector<kernel::BodyResult> cached;
        const auto original=document::PartDocument::load(source,&cached);
        const auto feature=std::ranges::find(original.history,std::string("Tažení 03"),&document::HistoryContainer::name);
        check(feature!=original.history.end(),"Recovery fixture has no Tažení 03");
        const auto found=std::ranges::find(original.sketches,feature->id,&sketcher::Sketch::owner_container_id);
        check(found!=original.sketches.end(),"Recovery fixture has no symmetric circle pair");
        const auto sketch=*found;
        std::vector<sketcher::SketchCircle> holes;
        for(const auto& c:sketch.constraints)if(c.kind==sketcher::ConstraintKind::Symmetric) {
            const auto a=std::ranges::find(sketch.circles,c.first_point_id,&sketcher::SketchCircle::center_point_id);
            const auto b=std::ranges::find(sketch.circles,c.second_point_id,&sketcher::SketchCircle::center_point_id);
            if(a!=sketch.circles.end()&&b!=sketch.circles.end()){holes={*a,*b};break;}
        }
        check(holes.size()==2,"Recovery fixture has no persisted symmetric hole centers");
        const auto hole_dimension=[&](const auto& d){return (d.kind==sketcher::DimensionKind::Radius||d.kind==sketcher::DimensionKind::Diameter)&&
            (d.geometry_id==holes[0].id||d.geometry_id==holes[1].id);};
        const auto file=directory/"02-sketch-edit-recovery.prtz";
        check(std::filesystem::absolute(file).lexically_normal()!=std::filesystem::absolute(source).lexically_normal(),"Recovery must operate on a copy");
        original.save(file,cached);
        app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1366,900);window.show();
        check(window.open_document_path(QString::fromStdString(file.string())),"Recovery copy cannot open");flush();run(window,"regenerate");
        const auto* body=original.body_owner_for_object(feature->id);check(body&&activate_test_body(application,window,body->scope.id),"Recovery Body cannot activate");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view&&!view->mesh().triangles.empty(),"Regeneration did not restore actual model display");
        auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
        check(row,"Recovery profile has no Sketch Tree row");window.show_tree_item_properties(row);flush();
        auto* open=window.findChild<QPushButton*>("sketchOpenButton");
        if(!open||!open->isVisible())open=window.findChild<QPushButton*>("featureSketchButton");
        if(open&&open->isVisible()){open->click();flush();}
        else check(window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Recovery profile cannot enter Sketcher");
        auto* selection=window.findChild<QAction*>("viewSelectionAction");
        check(selection&&selection->isEnabled(),"Recovery profile has no ordinary selection command");selection->trigger();flush();
        QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
        view->fit_points({sketch.world_point(-180,-310),sketch.world_point(180,310)});flush();
        QEventLoop fitted;QTimer::singleShot(950,&fitted,&QEventLoop::quit);fitted.exec();
        const auto point=[&](const std::string& id) {
            const auto* draft=window.active_sketch_for_test();check(draft&&draft->id==sketch.id,"Recovery active Sketch draft missing");
            const auto* p=draft->find_point(id);check(p,"Recovery active point missing");return std::array{p->x,p->y};
        };
        const auto local=[&](QPointF pixel) {
            const auto ray=view->ray_at(pixel);check(ray.has_value(),"Recovery cursor ray missing");
            const auto p=sketch.intersect_ray(ray->first,ray->second);check(p.has_value(),"Recovery Sketch plane missing");return *p;
        };
        const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
        const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
        const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
        const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
            QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
        };
        const double initial_distance=std::abs(sketch.find_point(holes.front().center_point_id)->y);
        for(const auto& circle:holes)for(double distance:{initial_distance+10,initial_distance-15,initial_distance+5}) {
            const auto before=point(circle.center_point_id);const double target=std::copysign(distance,before[1]);
            view->clear_selection();tree->clearSelection();flush();
            const auto from=screen(before[0],before[1]),to=screen(.4,target);
            const auto cursor=local(to);
            mouse(from,QEvent::MouseMove,Qt::NoButton);
            const auto pick=view->hovered_candidate();
            check(pick&&pick->kind==viewer::CandidateKind::SketchPoint&&pick->semantic_key=="point:"+circle.center_point_id,"Actual circle center loses mouse selection");
            mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
            mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
            const auto p=point(circle.center_point_id),other=point(holes.front().center_point_id==circle.center_point_id?holes.back().center_point_id:holes.front().center_point_id);
            if(!(std::abs(p[0])<1e-6&&std::abs(other[0])<1e-6&&std::abs(p[1]+other[1])<1e-6&&std::abs(p[1]-cursor[1])<1e-5)) {
                std::cerr<<"Recovery drag center="<<circle.center_point_id<<" before="<<before[0]<<","<<before[1]
                    <<" target="<<target<<" pixel-ray="<<cursor[0]<<","<<cursor[1]<<" result="<<p[0]<<","<<p[1]
                    <<" other="<<other[0]<<","<<other[1]<<'\n';
            }
            check(std::abs(p[0])<1e-6&&std::abs(other[0])<1e-6&&std::abs(p[1]+other[1])<1e-6&&std::abs(p[1]-cursor[1])<1e-5&&std::abs(p[1]-before[1])>1,
                "Real off-axis dragging failed or violated mirror/axis equations");
        }
        const auto moved=point(holes.back().center_point_id);
        window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
        check(point(holes.back().center_point_id)==moved,"Mirror center GUI Undo/Redo lost the drag");
        if(std::ranges::none_of(window.active_sketch_for_test()->dimensions,hole_dimension)) {
            view->clear_selection();tree->clearSelection();flush();
            window.findChild<QAction*>("sketchUniversalDimensionAction")->trigger();flush();
            const auto center=point(holes.front().center_point_id);
            const auto at=screen(center[0]+holes.front().radius,center[1]);
            mouse(at,QEvent::MouseMove,Qt::NoButton);
            check(view->hovered_candidate()&&view->hovered_candidate()->semantic_key=="circle:"+holes.front().id,"Actual hole cannot be selected for a diameter dimension");
            mouse(at,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);
            const auto label=screen(center[0]+50,center[1]+45);
            mouse(label,QEvent::MouseMove,Qt::NoButton);mouse(label,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(label,QEvent::MouseButtonRelease,Qt::LeftButton);
            QEventLoop editor_delay;QTimer::singleShot(30,&editor_delay,&QEventLoop::quit);editor_delay.exec();flush();
            if(auto* automatic=view->findChild<QLineEdit*>("inlineDimensionValueEdit");automatic&&automatic->isVisible()) {
                QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(automatic,&enter);flush();
            }
            selection->trigger();flush();
        }
        const auto* active=window.active_sketch_for_test();
        const auto dimension=std::ranges::find_if(active->dimensions,hole_dimension);
        check(dimension!=active->dimensions.end(),"Actual hole GUI dimension creation failed");
        const bool diameter=dimension->kind==sketcher::DimensionKind::Diameter;
        const std::string dimension_id=dimension->id;
        for(double radius:{27.,30.,28.8744929710136}) {
            viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key="dimension:"+dimension_id;
            const auto shown=std::ranges::find(view->mesh().dimensions,candidate.semantic_key,[](const auto& d){return d.reference.semantic_key;});
            check(shown!=view->mesh().dimensions.end(),"Actual radial dimension is missing from View");candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
            window.edit_dimension_inline(candidate);flush();
            auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(editor&&editor->isVisible(),"Actual radial dimension has no inline editor");
            editor->setText(QString::number(diameter?2*radius:radius,'g',17));
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
            for(const auto& circle:window.active_sketch_for_test()->circles)if(circle.id==holes[0].id||circle.id==holes[1].id)check(std::abs(circle.radius-radius)<1e-7,
                "Actual radius edit did not propagate to both holes");
            zima::test::check_coupled_equations(*window.active_sketch_for_test());
        }
        if(!trap.error.isEmpty())throw std::runtime_error(trap.error.toStdString());
        window.findChild<QAction*>("finishSketchAction")->trigger();flush();
        for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* box=dialog->findChild<QDialogButtonBox*>();box&&box->button(QDialogButtonBox::Ok)) {
            check(box->button(QDialogButtonBox::Ok)->isEnabled(),"Edited profile cannot confirm OK");box->button(QDialogButtonBox::Ok)->click();flush();break;
        }
        check(!view->mesh().triangles.empty(),"Finishing an actual radius edit lost the model");
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        std::vector<kernel::BodyResult> restored;const auto saved=document::PartDocument::load(file,&restored);
        check(saved.history.size()==original.history.size()&&!restored.empty()&&!restored.back().mesh.triangles.empty()&&restored.back().calculation_errors.empty(),
            "Actual edited history failed native save/reopen or lost final display");
        const auto persisted=std::ranges::find(saved.sketches,sketch.id,&sketcher::Sketch::id);
        check(persisted!=saved.sketches.end(),"Recovery saved Sketch missing");
        for(const auto& circle:persisted->circles)if(circle.id==holes[0].id||circle.id==holes[1].id)check(std::abs(circle.radius-28.8744929710136)<1e-7&&std::abs(std::abs(persisted->find_point(circle.center_point_id)->y)-std::abs(moved[1]))<1e-5,
            "Recovery native save did not commit the edited profile");
        app::AssemblyWorkspaceWindow reopened(QString::fromStdString(directory.string()));reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.resize(1200,900);reopened.show();
        check(reopened.open_document_path(QString::fromStdString(file.string())),"Actual edited model cannot reopen");flush();
        auto* reopened_view=dynamic_cast<viewer::MeshView*>(reopened.findChild<QOpenGLWidget*>());
        check(!reopened_view->mesh().triangles.empty(),"Actual edited model reopened invisible");
        check(activate_test_body(application,reopened,body->scope.id),"Reopened recovery Body cannot activate");flush();
        reopened_view->fit_all();QEventLoop fit_model;QTimer::singleShot(950,&fit_model,&QEventLoop::quit);fit_model.exec();
        const auto revision=run(reopened,"documents").front().at("revision");
        reopened.findChild<QAction*>("featurePrototypeAction")->trigger();flush();
        auto* placement=dynamic_cast<app::PrimitivePropertiesDialog*>(reopened.findChild<QWidget*>("featurePropertiesDialog"));
        check(placement,"Actual model cannot start Feature placement");
        std::optional<QPointF> face_pixel;std::optional<viewer::ViewerCandidate> offered;
        for(int y=12;y<reopened_view->height()-12&&!face_pixel;y+=8)for(int x=12;x<reopened_view->width()-12;x+=8) {
            const QPointF pixel(x,y);
            if(placement->geometry().contains(reopened_view->mapTo(&reopened,pixel.toPoint())))continue;
            const auto choices=reopened_view->selection_candidates_at(pixel);
            if(!choices.empty()&&choices.front().kind==viewer::CandidateKind::Face){face_pixel=pixel;offered=choices.front();break;}
        }
        check(face_pixel.has_value(),"Actual model has no offered placement face in View");
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,*face_pixel,QPointF(reopened_view->mapToGlobal(face_pixel->toPoint())),
                type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(reopened_view,&event);flush();
        }
        check(!placement->pending_value().placement.references.empty()&&placement->pending_value().placement.references.front().owner_id==offered->owner_id&&
            placement->pending_value().placement.references.front().semantic_key==offered->semantic_key,"Actual View face confirmation changed offered reference identity");
        const auto pending=placement->pending_value();const std::set<std::string> owners{pending.id,pending.feature_id,pending.container_origin.id};
        const auto single_origin=[&] {
            std::vector<kernel::ViewerAxis> axes;std::size_t planes{},points{};
            for(const auto* mesh:{&reopened_view->mesh(),&reopened_view->transient_context_mesh()}) {
                for(const auto& axis:mesh->axes)if(owners.contains(axis.reference.owner_id)&&axis.reference.semantic_key.starts_with("origin:axis:"))axes.push_back(axis);
                for(const auto& edge:mesh->edges)if(owners.contains(edge.reference.owner_id)&&edge.reference.semantic_key.starts_with("origin:plane:"))++planes;
                for(const auto& point:mesh->points)if(owners.contains(point.reference.owner_id)&&point.reference.semantic_key=="point"&&point.always_visible)++points;
            }
            check(axes.size()==3&&planes==3&&points==1,"Actual model placement retains a ghost or incomplete Origin");
            for(const auto& axis:axes)check(std::hypot(axis.point.x-axes.front().point.x,axis.point.y-axes.front().point.y,axis.point.z-axes.front().point.z)<1e-7,"Actual model Origin axes have different frames");
            check(std::ranges::none_of(reopened_view->mesh().axes,[&](const auto& axis){return owners.contains(axis.reference.owner_id);}),"Actual model pending Origin leaked into base scene");
            return axes.front().point;
        };
        const auto initial_origin=single_origin();bool moved_origin=false;unsigned numeric_edits{};
        for(const auto* coordinate:{"placement:x","placement:y","placement:z"}) {
            // A face constrains one coordinate. Exercise only the numeric
            // correction fields the shared placement contract actually offers.
            if(!placement->set_inline_parameter_value(coordinate,240.))continue;
            flush();single_origin();++numeric_edits;
            for(double value:{-150.,80.}) {
                check(placement->set_inline_parameter_value(coordinate,value),"Actual model editable numeric correction became unavailable");flush();
                const auto current=single_origin();++numeric_edits;
                moved_origin|=std::hypot(current.x-initial_origin.x,current.y-initial_origin.y,current.z-initial_origin.z)>1.;
            }
        }
        check(numeric_edits>=3&&moved_origin,"Actual model numeric correction never moved the live Origin");
        placement->reject();flush();
        check(run(reopened,"documents").front().at("revision")==revision&&!reopened_view->mesh().triangles.empty(),"Cancelling actual model placement changed history or lost display");
        for(const auto* mesh:{&reopened_view->mesh(),&reopened_view->transient_context_mesh()})
            check(std::ranges::none_of(mesh->axes,[&](const auto& axis){return owners.contains(axis.reference.owner_id);}),"Cancelled actual placement left its Origin behind");
        std::cout<<"02.prtz GUI copy: both circle centers dragged three times with off-axis cursor, independent symmetry equations, Undo/Redo, three radius edits, OK, visible model and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Sketch edit recovery: "<<e.what()<<'\n';return 1;}
}
