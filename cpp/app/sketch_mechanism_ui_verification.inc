static int verify_sketch_mechanism_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using namespace zima::test::mechanism;
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto settle=[&]{QEventLoop loop;QTimer::singleShot(950,&loop,&QEventLoop::quit);loop.exec();flush();};
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
        QTemporaryDir isolated(QString::fromStdString((parent/"mechanism-ui-XXXXXX").string()));
        require(isolated.isValid(),"Cannot create isolated mechanism GUI directory");
        const auto directory=std::filesystem::u8path(isolated.path().toStdString());
        QSettings preferences(isolated.filePath("config.ini"),QSettings::IniFormat);preferences.setValue("Application/Language","en");preferences.sync();
        unsigned edits{},drags{};
        for(const auto kind:{Kind::SliderCrank,Kind::FourBar,Kind::MidpointBranch,Kind::Watt,Kind::ScottRussell}) {
            auto f=make(kind,1,0,false,false);
            auto angle=f.sketch.create_segment_dimension(f.crank,sketcher::DimensionKind::Angle);
            angle.locked=false;f.angle=angle.id;f.sketch.apply_dimension(angle);
            require(f.sketch.set_dimension_placement(angle.id,45,-70),"Cannot place GUI input-angle label");
            auto authored=part(f.sketch);f.sketch=authored.sketches.front();
            const auto file=directory/(std::string(name(kind))+".prtz");authored.save(file);
            app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1366,900);window.show();flush();
            require(window.open_document_path(QString::fromStdString(file.string())),"Cannot open native mechanism in GUI");flush();
            const auto* body=authored.body_owner_for_object(f.sketch.owner_container_id);
            require(body&&activate_test_body(application,window,body->scope.id),"Cannot activate mechanism Body");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==f.sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            require(row,"Mechanism has no actual Sketch Tree row");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");require(open&&open->isVisible(),"Mechanism has no Open Sketch action");open->click();settle();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());require(view,"Mechanism View missing");
            window.findChild<QAction*>("viewSelectionAction")->trigger();flush();
            std::vector<kernel::Vec3> bounds;for(const auto& p:f.sketch.points)bounds.push_back(f.sketch.world_point(p.x,p.y));
            bounds.push_back(f.sketch.world_point(-40,-85));bounds.push_back(f.sketch.world_point(180,125));view->fit_points(bounds);settle();
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto screen=[&](XY point) {
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);require(ray.has_value(),"Mechanism cursor ray missing");
                    const auto hit=f.sketch.intersect_ray(ray->first,ray->second);require(hit.has_value(),"Mechanism cursor plane missing");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((point[0]-a[0])*vy-(point[1]-a[1])*vx)/det,(ux*(point[1]-a[1])-uy*(point[0]-a[0]))/det);
            };
            for(const auto& handle:{f.a,f.p.empty()?f.b:f.p})for(double theta:{40.,30.,35.}) {
                f.sketch=*window.active_sketch_for_test();const auto old=position(f.sketch,handle);
                const auto poses=f.pose(theta);const auto goal=std::ranges::find(poses,handle,[](const auto& p){return p.first;})->second;
                const auto from=screen(old),to=screen(goal);
                mouse(from,QEvent::MouseMove,Qt::NoButton);
                const auto pick=view->hovered_candidate();require(pick&&pick->kind==viewer::CandidateKind::SketchPoint&&pick->semantic_key=="point:"+handle,"Mechanism point cannot be selected in the actual View");
                mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                f.sketch=*window.active_sketch_for_test();const auto actual=position(f.sketch,handle);
                require(std::hypot(actual[0]-old[0],actual[1]-old[1])>1e-5,"GUI mechanism drag did not move");check(f,input_angle(f));++drags;
                const auto edited=f.sketch.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
                require(window.active_sketch_for_test()->serialized()==edited,"GUI mechanism drag Undo/Redo changed geometry");
            }
            for(double value:{42.,30.,38.}) {
                const auto shown=std::ranges::find(view->mesh().dimensions,"dimension:"+angle.id,[](const auto& d){return d.reference.semantic_key;});
                require(shown!=view->mesh().dimensions.end(),"Mechanism input angle is absent from View");
                viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=f.sketch.id;
                candidate.semantic_key="dimension:"+angle.id;candidate.geometry_index=std::size_t(shown-view->mesh().dimensions.begin());
                const auto pixel=view->candidate_dimension_label_position(candidate);require(pixel.has_value(),"Mechanism angle label has no View position");
                for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease,QEvent::MouseButtonDblClick,QEvent::MouseButtonRelease}) {
                    const bool down=type==QEvent::MouseButtonPress||type==QEvent::MouseButtonDblClick;
                    mouse(*pixel,type,type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,down?Qt::LeftButton:Qt::NoButton);
                }
                auto* editor=view->findChild<QLineEdit*>("inlineDimensionValueEdit");require(editor&&editor->isVisible(),"Mechanism angle inline editor missing");
                editor->setText(QString::number(value));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(editor,&enter);flush();
                f.sketch=*window.active_sketch_for_test();check(f,value);++edits;
            }
            const auto expected=f.sketch.serialized();window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>();buttons&&buttons->button(QDialogButtonBox::Ok)) {
                require(buttons->button(QDialogButtonBox::Ok)->isEnabled(),"Mechanism Sketch OK disabled");buttons->button(QDialogButtonBox::Ok)->click();flush();break;
            }
            require(trap.error.isEmpty(),"Mechanism GUI showed a modeling error: "+trap.error.toStdString());
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto loaded=document::PartDocument::load(file);require(loaded.sketches.front().serialized()==expected,"Mechanism GUI Save changed the draft geometry");
            app::AssemblyWorkspaceWindow reopened(isolated.path());reopened.setAttribute(Qt::WA_DontShowOnScreen);reopened.resize(1200,900);reopened.show();
            require(reopened.open_document_path(QString::fromStdString(file.string())),"Mechanism cannot reopen in fresh GUI");flush();
            require(!dynamic_cast<viewer::MeshView*>(reopened.findChild<QOpenGLWidget*>())->mesh().edges.empty(),"Mechanism reopened without View geometry");
            std::cout<<"Mechanism GUI "<<name(kind)<<" passed\n";
        }
        std::cout<<"Mechanism GUI: "<<drags<<" actual mouse drags, "<<edits<<" inline edits, Undo/Redo and native Save/fresh reopen passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<"Mechanism GUI: "<<e.what()<<'\n';return 1;}
}
