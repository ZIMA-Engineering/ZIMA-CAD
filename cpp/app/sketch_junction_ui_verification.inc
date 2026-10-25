static int verify_sketch_junction_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;using K=sketcher::ConstraintKind;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const auto supplied=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_SKETCH_JUNCTION_SOURCE").toStdString());
        const auto owner=qEnvironmentVariable("ZIMA_VERIFY_SKETCH_JUNCTION_OWNER").toStdString();
        const auto original=document::PartDocument::load(supplied);
        const auto found=std::ranges::find(original.sketches,owner,&sketcher::Sketch::owner_container_id);
        check(found!=original.sketches.end()&&found->segments.size()>=3,"Junction GUI source Sketch missing");
        const auto source=*found;
        for(bool reverse:{false,true})for(bool vertical_first:{false,true}) {
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SKETCH_JUNCTION_CASE")&&qEnvironmentVariableIntValue("ZIMA_VERIFY_SKETCH_JUNCTION_CASE")!=2*int(reverse)+int(vertical_first))continue;
            QElapsedTimer timer;timer.start();qint64 previous=0;
            const auto timing=[&](const char* phase){const auto now=timer.elapsed();std::cout<<"Junction timing "<<phase<<" ms="<<now-previous<<std::endl;previous=now;};
            const auto file=directory/("junction-"+std::to_string(reverse)+std::to_string(vertical_first)+".prtz");
            std::filesystem::copy_file(supplied,file,std::filesystem::copy_options::overwrite_existing);
            app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1280,900);window.show();
            check(window.open_document_path(QString::fromStdWString(file.wstring())),"Junction GUI copy cannot open");flush();
            timing("cached-open");
            const auto* body=original.body_owner_for_object(owner);check(body,"Junction Body missing");
            check(activate_test_body(application,window,body->scope.id),"Junction Body cannot activate");
            timing("activate-body");
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_SKETCH_JUNCTION_PROFILE_PLACEMENT")) {
                const auto before=window.execute_console_command("documents").data;
                auto* action=window.findChild<QAction*>("featurePrototypeAction");check(action&&action->isEnabled(),"Placement profile creation action missing");action->trigger();flush();timing("open-new-feature");
                auto* dialog=dynamic_cast<app::PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));
                auto* button=window.findChild<QPushButton*>("containerDefaultOriginButton");check(dialog&&button,"Placement profile Default Origin missing");
                auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Placement profile View missing");
                const auto initial=dialog->pending_value();const auto revision=view->base_mesh_revision();
                std::cout<<"Default-origin BEGIN"<<std::endl;button->click();flush();timing("default-origin");std::cout<<"Default-origin END scene_publications="<<view->base_mesh_revision()-revision<<std::endl;
                const auto pending=dialog->pending_value();check(pending.placement.references.size()>=3,"Default Origin did not fill its plane references");
                check(std::abs(initial.placement.x-pending.placement.x)<1e-9&&std::abs(initial.placement.y-pending.placement.y)<1e-9&&std::abs(initial.placement.z-pending.placement.z)<1e-9,"Default Origin changed the pending coordinate frame");
                dialog->reject();flush();check(window.execute_console_command("documents").data==before,"Placement profile Cancel changed the document");
                std::cout<<"Placement profile: native reference rows, identical position and unchanged document after Cancel passed"<<std::endl;return 0;
            }
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==source.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Junction Sketch row missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");
            if(!open)open=window.findChild<QPushButton*>("primitiveOwnSketchButton");
            if(open){open->click();flush();}
            auto* finish=window.findChild<QAction*>("finishSketchAction");
            check(finish&&finish->isEnabled(),"Junction native Sketch editing did not start");
            timing("enter-owned-sketch");
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Junction View missing");
            const auto vector=[](kernel::Vec3 p){return QVector3D(float(p.x),float(p.y),float(p.z));};
            const auto q=QQuaternion::fromAxes(vector(source.x_axis()),vector(source.y_axis()),vector(source.normal())).conjugated();
            auto camera=view->camera_state();camera[0]=q.scalar();camera[1]=q.x();camera[2]=q.y();camera[3]=q.z();view->set_camera_state(camera);
            std::vector<kernel::Vec3> points;for(const auto& p:source.points)points.push_back(source.world_point(p.x,p.y));view->fit_points(points);flush();
            const auto read=[&] {
                auto sketch=source;const auto origin=source.world_point(0,0),x=source.x_axis(),y=source.y_axis();
                for(auto& p:sketch.points) {
                    const auto it=std::ranges::find_if(view->mesh().points,[&](const auto& v){return v.reference.owner_id==source.id&&v.reference.semantic_key=="point:"+p.id;});
                    check(it!=view->mesh().points.end(),"Junction View lost a real native point");const auto d=kernel::dimension_sub(it->position,origin);
                    p.x=kernel::dimension_dot(d,x);p.y=kernel::dimension_dot(d,y);
                }return sketch;
            };
            const auto screen=[&](double x,double y) {
                const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Junction camera ray missing");const auto hit=source.intersect_ray(ray->first,ray->second);check(hit.has_value(),"Junction camera misses Sketch");return *hit;};
                const auto a=local({0,0}),b=local({100,0}),c=local({0,100});const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
                return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);
            };
            const auto mouse=[&](QPointF at,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons buttons=Qt::NoButton) {
                QMouseEvent event(type,at,QPointF(view->mapToGlobal(at.toPoint())),button,buttons==Qt::NoButton&&type==QEvent::MouseButtonPress?Qt::MouseButtons(button):buttons,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            };
            const auto click=[&](const auto& p){const auto at=screen(p.x,p.y);mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            const auto escape=[&]{QKeyEvent event(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            bool perpendicular=false,vertical=false;
            const auto equations=[&](const auto& s) {
                const auto *a=s.find_point(s.segments[0].first_point_id),*j=s.find_point(s.segments[0].second_point_id),*b=s.find_point(s.segments[1].second_point_id),*e=s.find_point(s.segments[2].second_point_id);
                const double ax=a->x-j->x,ay=a->y-j->y,bx=b->x-j->x,by=b->y-j->y,ex=e->x-j->x,ey=e->y-j->y,t=(ax*ex+ay*ey)/(ex*ex+ey*ey);
                check(std::abs(std::hypot(ax,ay)-std::hypot(bx,by))<1e-7&&std::hypot(2*t*ex-ax-bx,2*t*ey-ay-by)<1e-7&&std::abs(b->y-e->y)<1e-7,"Junction GUI broke existing symmetry/equality/alignment");
                if(perpendicular)check(std::abs((ax*bx+ay*by)/(std::hypot(ax,ay)*std::hypot(bx,by)))<1e-8,"Junction GUI perpendicular equation failed");
                if(vertical)check(std::abs(ex)<1e-7,"Junction GUI vertical equation failed");
            };
            for(bool make_vertical:{vertical_first,!vertical_first}) {
                view->clear_selection();tree->clearSelection();flush();const auto s=read();
                auto* action=window.findChild<QAction*>(make_vertical?"sketchVerticalAction":"sketchPerpendicularAction");check(action&&action->isEnabled(),"Junction relation action unavailable");action->trigger();flush();
                if(make_vertical) {
                    const auto& axis=s.segments[2];click(*s.find_point(reverse?axis.second_point_id:axis.first_point_id));click(*s.find_point(reverse?axis.first_point_id:axis.second_point_id));vertical=true;
                } else {
                    for(int index:{reverse?1:0,reverse?0:1}) {
                        const auto& line=s.segments[index];const auto *a=s.find_point(line.first_point_id),*b=s.find_point(line.second_point_id);
                        sketcher::SketchPoint p;p.x=(a->x+b->x)*.5;p.y=(a->y+b->y)*.5;click(p);
                    }perpendicular=true;
                }
                escape();equations(read());
                timing(make_vertical?"vertical-points":"perpendicular-lines");
            }
            for(double displacement:{2.,-3.,4.}) {
                const auto before=read();const auto* p=before.find_point(source.segments[1].second_point_id);const auto* j=before.find_point(source.segments[0].second_point_id);
                const auto from=screen(p->x,p->y),to=screen(p->x+displacement,p->y+std::copysign(std::abs(displacement),p->y-j->y));
                view->clear_selection();tree->clearSelection();mouse(from,QEvent::MouseMove,Qt::NoButton);mouse(from,QEvent::MouseButtonPress,Qt::LeftButton);
                mouse(to,QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(to,QEvent::MouseButtonRelease,Qt::LeftButton);
                const auto after=read();equations(after);check(std::hypot(after.find_point(p->id)->x-p->x,after.find_point(p->id)->y-p->y)>1e-6,"Junction free point did not move through GUI dragging");
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            timing("finish-sketch");
            auto* properties=dynamic_cast<ui::PropertiesSubWindow*>(window.findChild<QWidget*>("featurePropertiesDialog"));check(properties,"Junction feature Properties missing after Sketch finish");
            properties->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            timing("feature-ok");
            check(window.execute_console_command("undo").ok,"Junction committed edit cannot Undo");flush();
            check(window.execute_console_command("redo").ok,"Junction committed edit cannot Redo");flush();
            timing("undo-redo");
            window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
            const auto reopened=document::PartDocument::load(file);const auto saved=std::ranges::find(reopened.sketches,source.id,&sketcher::Sketch::id);check(saved!=reopened.sketches.end(),"Junction save lost Sketch");equations(*saved);
            timing("save-reopen");
            check(saved->constraints.size()==source.constraints.size()+2&&saved->external_references==source.external_references,"Junction save changed sources or omitted constraints");
            std::cout<<"Junction GUI reverse="<<reverse<<" vertical_first="<<vertical_first<<" mouse constraints/drags/commit/reopen passed"<<std::endl;
        }return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
