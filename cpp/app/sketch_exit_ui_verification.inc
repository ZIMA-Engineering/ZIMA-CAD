static int verify_sketch_exit_ui(QApplication& application,AssemblyWorkspaceWindow& window,const std::filesystem::path& directory) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        window.show();window.resize(qRound(1920/window.devicePixelRatioF()),qRound(1020/window.devicePixelRatioF()));
        unsigned cases{};
        for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
        for(bool diameter:{false,true})for(bool fixed:{false,true}) {
            auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
            sketch.plane=plane;sketch.plane_auto=false;
            const auto circle=sketch.add_circle(12,10,8);const auto center=sketch.circles.front().center_point_id;
            auto dimension=diameter?sketch.create_circle_diameter_dimension(circle):sketch.create_circle_radius_dimension(circle);
            sketch.apply_dimension(dimension);sketch.set_point_fixed(center,fixed);
            auto container=document::PartDocument::create_sketch_container();sketch.owner_container_id=container.id;
            workspace::insert_new_sketch(part,sketch,container);
            const auto path=directory/("sketch-exit-"+std::to_string(cases)+".prtz");part.save(path);
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Sketch exit fixture did not open");flush();
            check(window.execute_console_command(QString::fromStdString(commands::Json{{"command","body.activate"},{"arguments",{{"body",part.body_history.active_body_id()}}}}.dump())).ok,"Sketch Body did not activate");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");
            const auto enter=[&]() {
                QTreeWidgetItem* row{};for(QTreeWidgetItemIterator it(tree);*it;++it)
                    if((*it)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*it)->data(0,Qt::UserRole+3)=="part-sketch"){row=*it;break;}
                check(row,"Sketch row is missing");window.show_tree_item_properties(row);flush();
                auto* button=window.findChild<QPushButton*>("sketchOpenButton");check(button,"Sketch editor button is missing");button->click();flush();
                auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
                for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                view->fit_all();flush();check(window.active_sketch_for_test(),"Sketch editor did not open");
            };
            const auto properties=[&]() -> SketchPropertiesDialog* {
                for(auto* d:window.findChildren<QDialog*>())if(d->isVisible())if(auto* s=dynamic_cast<SketchPropertiesDialog*>(d))return s;return nullptr;
            };
            enter();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
            auto* ok=window.findChild<QAction*>("finishSketchAction");auto* cancel=window.findChild<QAction*>("cancelSketchAction");
            check(ok&&cancel&&ok->isEnabled()&&cancel->isEnabled(),"Sketch OK/Cancel are disabled");
            for(auto* action:{ok,cancel}) {
                QToolButton* button{};for(auto* object:action->associatedObjects())if(auto* b=qobject_cast<QToolButton*>(object);b&&b->isVisible())button=b;
                check(button&&button->height()>=40&&!button->icon().isNull(),"Sketch exit button is hidden or lacks its large icon");
                const auto point=button->mapTo(&window,QPoint{});
                check(point.y()<260&&point.x()>window.width()/2,"Sketch exit button is not at the top right on 1080p");
            }
            const auto entry=window.active_sketch_for_test()->serialized();
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto edit=[&](double radius) {
                const auto& dimensions=view->mesh().dimensions;
                const auto d=std::ranges::find_if(dimensions,[&](const auto& d){return d.reference.owner_id==sketch.id&&d.reference.semantic_key=="dimension:"+dimension.id;});
                check(d!=dimensions.end(),"Draft dimension missing");viewer::ViewerCandidate hit;hit.kind=viewer::CandidateKind::Dimension;
                hit.owner_id=sketch.id;hit.semantic_key=d->reference.semantic_key;hit.geometry_index=d-dimensions.begin();
                const auto label=view->candidate_dimension_label_position(hit);check(label.has_value(),"Draft dimension has no label");
                mouse(*label,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);
                mouse(*label,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                mouse(*label,QEvent::MouseButtonDblClick,Qt::LeftButton,Qt::LeftButton);
                mouse(*label,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");
                check(field,"Draft dimension did not open its value editor");
                field->setText(QString::number(radius*(diameter?2:1)));QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                check(std::abs(window.active_sketch_for_test()->circles.front().radius-radius)<1e-7,"Draft radius equation failed");
            };
            for(double radius:{10.,6.,9.}) {
                edit(radius);const auto before=*window.active_sketch_for_test()->find_point(center);
                const auto local=[&](QPointF pixel){const auto ray=view->ray_at(pixel);check(ray.has_value(),"Sketch cursor ray missing");return *window.active_sketch_for_test()->intersect_ray(ray->first,ray->second);};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
                const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
                const auto from=screen(before.x,before.y),to=screen(before.x+1.5,before.y+.8);view->clear_selection();flush();
                mouse(from,QEvent::MouseMove,Qt::NoButton,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton,Qt::LeftButton);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton,Qt::NoButton);
                const auto after=*window.active_sketch_for_test()->find_point(center);const auto movement=std::hypot(after.x-before.x,after.y-before.y);
                check(fixed?movement<1e-7:movement>1e-5,"Sketch drag escaped its fix or failed to move");
                check(std::abs(window.active_sketch_for_test()->circles.front().radius-radius)<1e-7,"Sketch drag changed its locked radius");
            }
            const auto changed=window.active_sketch_for_test()->serialized();
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(window.active_sketch_for_test()->serialized()==changed,"Draft Undo/Redo lost an edit");
            if(cases==0)window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"sketch-ok-cancel-1080p.png")));
            cancel->trigger();flush();check(!window.active_sketch_for_test()&&properties(),"Sketch Cancel did not return to Properties");
            properties()->findChild<QPushButton*>("sketchOpenButton")->click();flush();
            check(window.active_sketch_for_test()->serialized()==entry,"Sketch Cancel retained draft edits");
            edit(11);ok->trigger();flush();check(properties(),"Sketch OK did not return to Properties");
            properties()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("save").ok,"Cannot save cancelled Sketch fixture");
            check(document::PartDocument::load(path).sketches.front().circles.front().radius==8,"Properties Cancel committed a Sketch edit");
            enter();edit(12);ok->trigger();flush();properties()->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            check(window.execute_console_command("save").ok,"Cannot save confirmed Sketch fixture");
            check(document::PartDocument::load(path).sketches.front().circles.front().radius==12,"Confirmed Sketch did not reopen with its edited radius");
            window.findChild<QAction*>("undoAction")->trigger();flush();window.findChild<QAction*>("redoAction")->trigger();flush();
            check(window.execute_console_command("save").ok&&document::PartDocument::load(path).sketches.front().circles.front().radius==12,"Document Undo/Redo lost the confirmed Sketch");
            ++cases;
        }
        std::cout<<"Sketch OK/Cancel GUI: "<<cases<<" plane/radius-diameter/fixed-free cases, repeated edits/drags, draft and document Undo/Redo, Cancel and native reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
