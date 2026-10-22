int verify_sketch_slot_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto original_palette=application.palette();
    try {
        for(bool dark:{false,true})for(int orientation=0;orientation<17;++orientation) {
            auto palette=original_palette;
            if(dark){palette.setColor(QPalette::Window,QColor("#262b30"));palette.setColor(QPalette::WindowText,Qt::white);palette.setColor(QPalette::Text,Qt::white);palette.setColor(QPalette::Base,QColor("#171b21"));}
            application.setPalette(palette);
            std::cout<<"Slot GUI case "<<orientation<<" dark="<<dark<<std::endl;
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
            std::array<double,2> first{10.,-15.},second{10.,15.};
            std::string symmetry="sketch_axis:x";
            if(orientation==1){first={-15.,10.};second={15.,10.};symmetry="sketch_axis:y";}
            if(orientation==2) {
                first={25.,10.};second={10.,25.};
                symmetry=sketch.add_segment(-20,-20,30,30);sketch.set_geometry_construction(symmetry,true);
            }
            const bool priority=orientation>=14;
            const bool directional=orientation>=3&&!priority, manual=orientation>=7&&orientation<11;
            const bool vertical=directional&&(orientation%2==0);
            const bool disabled=orientation>=12&&orientation<14;
            if(directional) {
                first={10.,20.};
                const double sign=orientation==5||orientation==6?-1.:1.;
                second=manual?std::array{42.,44.}:vertical?std::array{10.12,20.+sign*26.}:std::array{10.+sign*32.,20.12};
            }
            if(orientation==11) {
                const auto line=sketch.add_segment(6,0,46,40);sketch.set_geometry_construction(line,true);
            }
            std::string priority_point;
            if(orientation==14) {priority_point=sketch.add_point(second[0],second[1]);sketch.find_point(priority_point)->fixed=true;}
            if(orientation==15) {const auto support=sketch.add_segment(10,-40,10,40);sketch.set_geometry_construction(support,true);}
            if(orientation==16) {priority_point=sketch.add_point(22,second[1]);sketch.find_point(priority_point)->fixed=true;}
            part.history={feature};part.sketches={sketch};
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Slot test");
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
            const auto path=directory/("slot-"+std::to_string(orientation)+(dark?"-dark":"-light")+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Cannot open slot fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate slot Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Cannot locate slot Sketch");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Slot View missing");
            auto camera=view->camera_state();camera[4]=60;camera[5]=camera[6]=0;camera[7]=60;view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Slot ray missing");const auto q=sketch.intersect_ray(ray->first,ray->second);check(q.has_value(),"Slot camera not facing Sketch");return *q;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](std::array<double,2> p){return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](std::array<double,2> p){const auto at=screen(p);mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            if(disabled||orientation==7||orientation==8) {
                window.findChild<QAction*>("sketchConstraintsAction")->trigger();flush();
                auto* dialog=window.findChild<QDialog*>("sketchConstraintsDialog");check(dialog,"Slot constraints settings missing");
                for(const auto* name:{"sketchHorizontalActionAutomatic","sketchVerticalActionAutomatic"}) {
                    auto* toggle=dialog->findChild<QPushButton*>(name);check(toggle,"Slot H/V automatic toggle missing");toggle->setChecked(false);
                }
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            }
            auto* slot=window.findChild<QAction*>("sketchSlotAction");check(slot&&slot->isEnabled(),"Slot unavailable");slot->trigger();flush();
            click(first);
            if(orientation==3&&dark) {
                const auto original=app::ApplicationSettings::load(QString::fromStdString(document::path_to_utf8(directory)));
                QTemporaryDir languages;check(languages.isValid(),"Slot language configuration missing");
                for(const auto& language:{"cs","en","de","fr","ru"}) {
                    QSettings config(languages.filePath("config.ini"),QSettings::IniFormat);
                    config.setValue("Application/Language",language);config.setValue("Paths/Localization",original.resolved_paths.value("Localization"));config.sync();
                    const auto settings=app::ApplicationSettings::load(languages.path());app::apply_application_translations(application,settings);
                    slot->trigger();flush();click(first);
                    const auto* status=window.findChild<QLabel*>("workspaceState");check(status,"Slot status label missing");
                    const auto source=QString::fromUtf8("Drážka skici: určete střed druhého oblouku. Přichycení nabízí H/V mezi středy nebo symetrii vůči ose či konstrukční čáře.");
                    check(!settings.qt_translations.value(source).isEmpty()&&status->text()==settings.qt_translations.value(source),"Slot prompt did not follow the selected language");
                    window.grab().save(QString::fromStdString(document::path_to_utf8(directory/(std::string("slot-prompt-")+language+".png"))));
                }
                app::apply_application_translations(application,original);slot->trigger();flush();click(first);
            }
            check(view->transient_points().size()==1,"Slot first-centre confirmation supplied no marker");
            const auto first_marker=view->transient_points().front();
            const auto base_revision=view->base_mesh_revision();QElapsedTimer preview_time;preview_time.start();
            for(int move=0;move<8;++move)mouse(screen({second[0]+.01*(move%2),second[1]+.01*(move%3)}),QEvent::MouseMove,Qt::NoButton);
            mouse(screen(second),QEvent::MouseMove,Qt::NoButton);
            check(view->transient_points().size()==1,"Slot second-centre entry lost its first-centre marker");
            check(std::hypot(view->transient_points().front().x-first_marker.x,
                view->transient_points().front().y-first_marker.y,view->transient_points().front().z-first_marker.z)<1e-7,
                "Slot first-centre marker moved with the pointer");
            if(priority)check(std::ranges::any_of(view->transient_labels(),[](const auto& value){return value.second=="S";}),
                "Slot symmetry did not take priority over an existing point/curve/HV snap");
            check(view->base_mesh_revision()==base_revision,"Slot preview republished the base scene");
            std::cout<<"Slot pointer preview: publications=0 moves=9 ms="<<preview_time.nsecsElapsed()/1e6<<std::endl;
            if(directional&&!manual) {
                const auto label=vertical?"V":"H";
                check(std::ranges::any_of(view->transient_labels(),[&](const auto& item){return item.second==label;})!=disabled,"Slot H/V preview marker disagrees with automatic settings");
                view->grab().save(QString::fromStdString(document::path_to_utf8(directory/("slot-direction-"+std::to_string(orientation)+(dark?"-dark":"-light")+".png"))));
            }
            click(second);
            if(directional&&!manual&&!disabled) second[vertical?0:1]=first[vertical?0:1];
            const double dx=second[0]-first[0],dy=second[1]-first[1],length=std::hypot(dx,dy);
            const std::array radius_point{second[0]-dy/length*5,second[1]+dx/length*5};
            mouse(screen(radius_point),QEvent::MouseMove,Qt::NoButton);
            view->grab().save(QString::fromStdString(document::path_to_utf8(directory/("slot-preview-"+std::to_string(orientation)+".png"))));
            click(radius_point);
            // Escape discards the next unfinished slot, without deleting the
            // already committed four-curve profile or creating a transaction.
            click({-22.,-17.});
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);flush();
            check(view->transient_points().empty()&&view->transient_labels().empty(),"Slot Escape retained unfinished centre markers");
            const auto stored=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(path).sketches.front();};
            const auto created_slot=stored();
            const auto* first_center=created_slot.find_point(created_slot.arcs.at(0).center_point_id);
            check(first_center,"Slot first centre is missing");
            const auto committed_marker=created_slot.world_point(first_center->x,first_center->y);
            check(std::hypot(first_marker.x-committed_marker.x,first_marker.y-committed_marker.y,
                first_marker.z-committed_marker.z)<1e-7,"Slot marker did not represent its accepted first centre");
            if(manual) {
                auto* action=window.findChild<QAction*>(vertical?"sketchVerticalAction":"sketchHorizontalAction");
                check(action&&action->isEnabled(),"Manual slot H/V unavailable");action->trigger();flush();
                click(orientation>=9?second:first);click(orientation>=9?first:second);
                QKeyEvent done(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&done);flush();
            }
            const auto committed=stored();
            check(std::abs(committed.arcs[0].radius-5)<1e-5&&std::abs(committed.arcs[1].radius-5)<1e-5,"Slot cursor did not define its perpendicular radius");
            window.findChild<QAction*>("undoAction")->trigger();flush();
            const auto undone=stored();
            check(manual?undone.constraints.size()+1==committed.constraints.size():undone.arcs.empty(),"Slot Undo did not restore the preceding edit");
            window.findChild<QAction*>("redoAction")->trigger();flush();
            const auto redone=stored();check(redone.points==committed.points&&redone.constraints==committed.constraints,"Slot Redo lost geometry or constraints");
            if(directional&&!disabled) {
                for(int step=0;step<3;++step) {
                    const auto before=stored();const auto id=before.arcs[step%2].center_point_id;const auto* p=before.find_point(id);
                    const auto from=screen({p->x,p->y}),to=screen({p->x+3,p->y+3});
                    view->clear_selection();const auto hits=view->selection_candidates_at(from);
                    check(!hits.empty()&&hits.front().semantic_key=="point:"+id,"Slot center is not first in mouse picker");
                    mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton);
                    QMouseEvent move(QEvent::MouseMove,to,QPointF(view->mapToGlobal(to.toPoint())),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto dragged=stored();const auto* q=dragged.find_point(id);
                    check(q&&std::hypot(q->x-p->x,q->y-p->y)>1e-4,"Slot mouse center drag did not move");
                    const auto* a=dragged.find_point(dragged.arcs[0].center_point_id);const auto* b=dragged.find_point(dragged.arcs[1].center_point_id);
                    auto solved=dragged;check(std::abs(vertical?a->x-b->x:a->y-b->y)<1e-7&&solved.solve().maximum_residual<1e-8,"Slot center drag escaped H/V");
                    window.findChild<QAction*>("undoAction")->trigger();flush();check(stored().points==before.points,"Slot drag Undo failed");
                    window.findChild<QAction*>("redoAction")->trigger();flush();check(stored().points==dragged.points,"Slot drag Redo failed");
                }
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            app::SketchPropertiesDialog* properties{};
            for(auto* d:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::SketchPropertiesDialog*>(d);candidate&&candidate->isVisible())properties=candidate;
            check(properties,"Slot did not return to Sketch Properties");properties->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            auto saved=document::PartDocument::load(path);auto& result=saved.sketches.front();
            check(result.arcs.size()==2&&result.segments.size()==(orientation==2||orientation==11||orientation==15?3:2),"Slot did not commit exactly four profile curves");
            if(priority&&!priority_point.empty()) {
                check(result.arcs[1].center_point_id!=priority_point,"Slot symmetry also merged the second centre with a competing snap point");
                check(result.find_point(priority_point)&&result.find_point(priority_point)->fixed,"Slot priority modified its fixed source point");
            }
            if(!directional) check(std::ranges::any_of(result.constraints,[&](const auto& value){return value.kind==sketcher::ConstraintKind::Symmetric&&value.geometry_id==symmetry;}),"Slot lost offered centre symmetry");
            else if(!disabled) {
                const auto kind=vertical?sketcher::ConstraintKind::Vertical:sketcher::ConstraintKind::Horizontal;
                const auto& a=result.arcs[0].center_point_id;const auto& b=result.arcs[1].center_point_id;
                check(std::ranges::any_of(result.constraints,[&](const auto& c){return c.kind==kind&&((c.first_point_id==a&&c.second_point_id==b)||(c.first_point_id==b&&c.second_point_id==a));}),"Slot did not persist H/V between its centers");
                const auto* p=result.find_point(a);const auto* q=result.find_point(b);
                check(std::abs(vertical?p->x-q->x:p->y-q->y)<1e-7,"Slot H/V center equation failed");
            }
            else check(std::ranges::none_of(result.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Horizontal||c.kind==sketcher::ConstraintKind::Vertical;}),"Disabled slot H/V created a constraint");
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Slot final saved document did not reopen");flush();
            check(result.solve().maximum_residual<1e-8,"Slot does not solve after save/reopen");
            if(orientation==3||orientation==4) {
                const auto command=[&](const commands::Json& value) {
                    auto response=window.execute_console_command(QString::fromStdString(value.dump()));
                    if(!response.ok)throw std::runtime_error("Slot dimension setup: "+response.code+": "+response.message);
                    flush();return response.data;
                };
                const bool diameter=orientation==4;
                const auto radial=command({{"command","sketch.dimension.create"},{"arguments",{{"sketch",sketch.id},{"kind",diameter?"diameter":"radius"},{"geometry",{result.arcs[0].id}},{"position",{50.,10.}},{"locked",true}}}});
                const auto length=command({{"command","sketch.dimension.create"},{"arguments",{{"sketch",sketch.id},{"kind",vertical?"distance_y":"distance_x"},{"points",{result.arcs[0].center_point_id,result.arcs[1].center_point_id}},{"position",{15.,-5.}},{"locked",true}}}});
                row=nullptr;for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
                check(row,"Slot dimension Sketch row missing");window.show_tree_item_properties(row);flush();window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
                QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();view->set_camera_state(camera);flush();
                for(const auto& definition:{radial,length})for(double factor:{1.1,.9,1.}) {
                    const auto id=definition["dimension"].get<std::string>();const double target=definition["value"].get<double>()*factor;
                    const auto before=stored();viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;candidate.owner_id=sketch.id;candidate.semantic_key="dimension:"+id;
                    const auto dimension=std::ranges::find_if(view->mesh().dimensions,[&](const auto& d){return d.reference.owner_id==candidate.owner_id&&d.reference.semantic_key==candidate.semantic_key;});
                    check(dimension!=view->mesh().dimensions.end(),"Slot numeric dimension disappeared");candidate.geometry_index=unsigned(dimension-view->mesh().dimensions.begin());
                    const auto at=view->candidate_dimension_label_position(candidate);check(at.has_value(),"Slot numeric label missing");
                    mouse(*at,QEvent::MouseMove,Qt::NoButton);mouse(*at,QEvent::MouseButtonDblClick,Qt::LeftButton);mouse(*at,QEvent::MouseButtonRelease,Qt::LeftButton);
                    auto* field=view->findChild<QLineEdit*>("inlineDimensionValueEdit");check(field&&field->isVisible(),"Slot dimension editor did not open by double click");field->setText(QString::number(target,'g',15));
                    QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field,&enter);flush();
                    auto changed=stored();const auto d=std::ranges::find(changed.dimensions,id,&sketcher::SketchDimension::id);
                    check(d!=changed.dimensions.end()&&std::abs(d->value-target)<1e-7,"Slot mouse numeric edit was rejected");
                    const auto* a=changed.find_point(changed.arcs[0].center_point_id);const auto* b=changed.find_point(changed.arcs[1].center_point_id);
                    check(std::abs(vertical?a->x-b->x:a->y-b->y)<1e-7,"Numeric slot edit lost H/V");
                    check(std::abs(changed.arcs[0].radius-changed.arcs[1].radius)<1e-7,"Numeric slot edit lost equal radii");
                    const auto driver=std::ranges::find(changed.dimensions,radial["dimension"].get<std::string>(),&sketcher::SketchDimension::id);
                    check(std::abs(changed.arcs[0].radius-driver->value/(diameter?2:1))<1e-7,"Numeric slot R/D equation failed");
                    window.findChild<QAction*>("undoAction")->trigger();flush();check(stored().points==before.points,"Slot numeric Undo failed");window.findChild<QAction*>("redoAction")->trigger();flush();check(stored().points==changed.points,"Slot numeric Redo failed");
                }
                window.findChild<QAction*>("finishSketchAction")->trigger();flush();
                for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible()&&dynamic_cast<app::SketchPropertiesDialog*>(dialog))dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
                static_cast<void>(stored());check(window.open_document_path(QString::fromStdString(document::path_to_utf8(path))),"Dimensioned slot did not reopen");flush();
            }
        }
        application.setPalette(original_palette);
        std::cout<<"Slot GUI: 34 Light/Dark cases; first-centre markers, symmetry priority over point/curve/HV snaps, manual H/V in both orders, automatic H/V both directions, construction-line interference, disabled inference, repeated center drags, Undo/Redo, symmetry, repeated R/D and center-distance inline edits, Escape and native save/reopen passed\n";return 0;
    }catch(const std::exception& error){application.setPalette(original_palette);std::cerr<<error.what()<<'\n';return 1;}
}
