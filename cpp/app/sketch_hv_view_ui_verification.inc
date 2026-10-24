static int verify_sketch_hv_view_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;using K=sketcher::ConstraintKind;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_SKETCH_COORDINATE_SOURCE");!source.isEmpty()) {
            const auto model=document::PartDocument::load(std::filesystem::u8path(source.toStdString()));
            const auto sketch=std::ranges::find_if(model.sketches,[](const auto& s){return std::ranges::any_of(s.dimensions,[&](const auto& d){return sketcher::is_coordinate_dimension(d)&&std::abs(std::abs(sketcher::dimension_display_value(d,s.plane))-150)<1e-7;});});
            check(sketch!=model.sketches.end(),"Coordinate review Sketch missing");
            const auto* owner=model.find_container(sketch->owner_container_id);check(owner,"Coordinate review Feature missing");const auto& feature=*owner;
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1280,900);window.show();
            check(window.open_document_path(source),"Coordinate review copy cannot open");flush();
            const auto* body=model.body_owner_for_object(feature.id);check(body&&activate_test_body(application,window,body->scope.id),"Coordinate review Body missing");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch->id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Coordinate review Sketch tree row missing");window.show_tree_item_properties(row);flush();
            if(auto* button=window.findChild<QPushButton*>("sketchOpenButton"))button->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Coordinate review View missing");view->fit_all();flush();
            bool found=false;
            for(const auto& dimension:view->mesh().dimensions)if(dimension.reference.owner_id==sketch->id&&std::abs(std::abs(dimension.value)-150)<1e-7) {
                std::cout<<"Coordinate View dimension owner="<<dimension.reference.owner_id<<" key="<<dimension.reference.semantic_key<<" value="<<dimension.value
                    <<" anchors="<<dimension.witness_first.x<<","<<dimension.witness_first.y<<","<<dimension.witness_first.z<<" -> "<<dimension.witness_second.x<<","<<dimension.witness_second.y<<","<<dimension.witness_second.z<<std::endl;found=true;
                const auto axis=std::ranges::find_if(view->mesh().axes,[&](const auto& a){return a.reference.owner_id==feature.container_origin.id&&a.reference.semantic_key=="origin:axis:z";});
                check(axis!=view->mesh().axes.end(),"Coordinate review Feature Origin Z is missing");
                const auto span=kernel::dimension_sub(dimension.witness_second,dimension.witness_first);
                const double projected=kernel::dimension_dot(span,axis->direction);
                check(dimension.value==150&&std::abs(projected-150)<1e-7,"Coordinate View disagrees with the actual positive Feature Origin Z");
            }
            check(found,"Coordinate review displayed 150 mm dimension is missing");
            for(const auto& axis:view->mesh().axes)if(axis.reference.owner_id==sketch->id||axis.reference.semantic_key=="origin:axis:z")
                std::cout<<"Coordinate View axis owner="<<axis.reference.owner_id<<" key="<<axis.reference.semantic_key<<" label="<<axis.label
                    <<" direction="<<axis.direction.x<<","<<axis.direction.y<<","<<axis.direction.z<<std::endl;
            window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"user-coordinate-review.png")));
            std::cout<<"Coordinate review opened only the supplied copy; no save or geometry calculation requested"<<std::endl;return 0;
        }
        unsigned cases{};
        for(bool back:{false,true})for(int turn=0;turn<4;++turn)
        for(bool vertical:{false,true})for(bool point_pair:{false,true}) {
            std::cout<<"H/V GUI case="<<cases<<" back="<<back<<" turn="<<turn<<" vertical="<<vertical<<" point_pair="<<point_pair<<std::endl;
            auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
            auto feature=document::PartDocument::create_sketch_container();sketch.owner_container_id=feature.id;feature.feature.sketch_id=sketch.id;
            const auto segment=sketch.add_segment(12,13,32,23);sketch.set_point_fixed(sketch.segments.front().first_point_id,true);
            if(cases==1)sketch.set_geometry_construction(segment,true);
            const auto line=sketch.segments.front();
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("View-relative H/V");graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);part.resolve_constructions();
            const auto file=directory/("hv-view-"+std::to_string(cases)+".prtz");part.save(file);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(document::path_to_utf8(directory)));window.resize(1200,900);window.show();
            check(window.open_document_path(QString::fromStdString(document::path_to_utf8(file))),"H/V fixture cannot open");flush();
            check(activate_test_body(application,window,body),"H/V fixture Body unavailable");
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"H/V Sketch tree row missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"H/V View missing");
            if(cases==1)view->set_reference_visibility(viewer::ReferenceVisibility::AxisPoints,false);
            const auto q=QQuaternion::fromAxisAndAngle(0,0,1,float(turn*90))*(back?QQuaternion::fromAxisAndAngle(1,0,0,180):QQuaternion{});
            auto camera=view->camera_state();camera[0]=q.scalar();camera[1]=q.x();camera[2]=q.y();camera[3]=q.z();
            view->set_camera_state(camera);flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"H/V camera ray missing");const auto value=sketch.intersect_ray(ray->first,ray->second);check(value.has_value(),"H/V camera is not facing the Sketch");return *value;};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons==Qt::NoButton&&type==QEvent::MouseButtonPress?Qt::MouseButtons(button):buttons,Qt::NoModifier);
                QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](QPointF at){mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* command=window.findChild<QAction*>(vertical?"sketchVerticalAction":"sketchHorizontalAction");check(command&&command->isEnabled(),"H/V action unavailable");
            if(!point_pair)click(screen(22,18));
            command->trigger();flush();
            if(point_pair){click(screen(12,13));click(screen(32,23));}
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            const auto snapshot=[&]{window.findChild<QAction*>("saveDocumentAction")->trigger();flush();return document::PartDocument::load(file);};
            auto model=snapshot();auto edited=*std::ranges::find(model.sketches,sketch.id,&sketcher::Sketch::id);
            const auto expected=(vertical!=(turn%2!=0))?K::Vertical:K::Horizontal;
            check(std::ranges::any_of(edited.constraints,[&](const auto& relation){return relation.kind==expected;}),"Screen H/V action stored the wrong native relation");
            const auto equation=[&](const auto& value) {
                const auto *first=value.find_point(line.first_point_id),*last=value.find_point(line.second_point_id);
                check(std::abs(expected==K::Vertical?last->x-first->x:last->y-first->y)<1e-7,"H/V GUI edit escaped its native equation");
            };equation(edited);
            const auto marker=std::ranges::find_if(view->mesh().constraint_markers,[](const auto& m){return m.label=="H"||m.label=="V";});
            check(marker!=view->mesh().constraint_markers.end()&&view->sketch_hv_label(marker->label,sketch.id)==(vertical?"V":"H"),"H/V marker does not follow the perpendicular view");
            const auto marker_count=view->mesh().constraint_markers.size();const auto revision=view->base_mesh_revision();
            const auto oblique=QQuaternion::fromAxisAndAngle(0,1,0,37)*q;
            auto tilted=camera;tilted[0]=oblique.scalar();tilted[1]=oblique.x();tilted[2]=oblique.y();tilted[3]=oblique.z();view->set_camera_state(tilted);flush();
            check(view->mesh().constraint_markers.size()==marker_count&&view->sketch_hv_label(marker->label,sketch.id)==(vertical?"V":"H")&&view->base_mesh_revision()==revision,
                "Oblique view hid/rewrote H/V markers or republished the model");
            const auto untouched=snapshot();
            check(untouched.history==model.history&&untouched.sketches.front().serialized()==model.sketches.front().serialized(),"Camera-only H/V mapping changed the native document");
            view->set_camera_state(camera);flush();
            for(double increment:{3.,7.,11.}) {
                const auto* end=edited.find_point(line.second_point_id);const double x=end->x+(expected==K::Horizontal?increment:0),y=end->y+(expected==K::Vertical?increment:0);
                view->clear_selection();tree->clearSelection();flush();
                mouse(screen(end->x,end->y),QEvent::MouseMove,Qt::NoButton);
                if(!view->hovered_candidate()||view->hovered_candidate()->kind!=viewer::CandidateKind::SketchPoint) {
                    for(const auto& candidate:view->selection_candidates_at(screen(end->x,end->y)))std::cerr<<"H/V endpoint candidate kind="<<int(candidate.kind)<<" key="<<candidate.semantic_key<<" owner="<<candidate.owner_id<<std::endl;
                    view->grabFramebuffer().save(QString::fromStdString(document::path_to_utf8(directory/"hv-endpoint-hover.png")));
                }
                check(view->hovered_candidate()&&view->hovered_candidate()->kind==viewer::CandidateKind::SketchPoint,"H/V endpoint loses hover to helper geometry");
                mouse(screen(end->x,end->y),QEvent::MouseButtonPress,Qt::LeftButton);
                mouse(screen(x,y),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen(x,y),QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=snapshot();const auto next=*std::ranges::find(after.sketches,sketch.id,&sketcher::Sketch::id);equation(next);
                check(std::hypot(next.find_point(line.second_point_id)->x-end->x,next.find_point(line.second_point_id)->y-end->y)>1e-6,"Free H/V endpoint did not move through real dragging");edited=next;
            }
            const auto committed=edited.serialized();window.findChild<QAction*>("undoAction")->trigger();flush();
            window.findChild<QAction*>("redoAction")->trigger();flush();const auto redone=snapshot();
            check(std::ranges::find(redone.sketches,sketch.id,&sketcher::Sketch::id)->serialized()==committed,"H/V Undo/Redo/reopen changed its identity");
            if(turn==1&&!back&&!vertical&&!point_pair)window.grab().save(QString::fromStdString(document::path_to_utf8(directory/"hv-quarter-turn.png")));
            ++cases;
        }
        std::cout<<cases<<" H/V real GUI matrices: quarter turns/front-back, segment/point entry, oblique persistence, repeated endpoint drags and Undo/Redo/reopen passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
