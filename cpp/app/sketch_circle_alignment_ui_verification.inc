static int verify_sketch_circle_alignment_ui(QApplication& application,
        const std::filesystem::path& directory) {
    using namespace zima;
    using K=sketcher::ConstraintKind;
    using D=sketcher::DimensionKind;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        unsigned cases{},dimension_cases{};
        for(bool rectangle:{false,true})for(bool external:{false,true})
        for(bool vertical:{false,true})for(double side:{1.,-1.}) {
            std::cout<<"Circular alignment GUI case="<<cases<<" rectangle="<<rectangle<<" external="<<external<<" vertical="<<vertical<<" side="<<side<<std::endl;
            const auto xy=[&](double x,double y){return vertical?std::array{y,x}:std::array{x,y};};
            auto part=document::PartDocument::create_default();
            auto feature=document::PartDocument::create_sketch_container();
            auto source_feature=document::PartDocument::create_sketch_container();
            auto target=sketcher::Sketch::create_default(),source=sketcher::Sketch::create_default();
            target.owner_container_id=feature.id;feature.feature.sketch_id=target.id;
            source.owner_container_id=source_feature.id;source_feature.feature.sketch_id=source.id;
            std::array<std::string,2> supports;
            for(std::size_t i=0;i<2;++i) {
                auto& owner=external?source:target;
                const auto center=xy(i?30.:-30.,0.);
                const auto circle=owner.add_circle(center[0],center[1],10.);
                owner.find_point(owner.circles.back().center_point_id)->fixed=true;
                auto radius=owner.create_circle_radius_dimension(circle);radius.locked=true;owner.apply_dimension(radius);
                supports[i]=circle;
                if(external) {
                    auto ref=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
                    ref.source_document_id=part.document_id;ref.source_owner_id=source.id;ref.source_semantic_key="circle:"+circle;
                    for(int k=0;k<=72;++k) {
                        const double angle=k*2.*std::numbers::pi/72.;
                        ref.cached_points.push_back({center[0]+10.*std::cos(angle),center[1]+10.*std::sin(angle)});
                    }
                    supports[i]=ref.id;target.add_external_reference(ref);
                }
            }
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Circular contact inference");
            if(external){part.history.push_back(source_feature);part.sketches.push_back(source);graph.insert({document::PartHistoryKind::Feature,source_feature.id});}
            part.history.push_back(feature);part.sketches.push_back(target);graph.insert({document::PartHistoryKind::Feature,feature.id});
            part.set_body_history(graph);part.resolve_constructions();
            const auto path=directory/("circle-alignment-"+std::to_string(cases++)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(1200,900);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Circle fixture cannot open");flush();
            check(activate_test_body(application,window,body),"Circle fixture Body unavailable");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==target.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Circle fixture Sketch missing");window.show_tree_item_properties(row);flush();
            window.findChild<QPushButton*>("sketchOpenButton")->click();flush();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Circle fixture View missing");
            view->fit_points({target.world_point(-50,-50),target.world_point(50,50)});flush();
            const auto local=[&](QPointF p){const auto ray=view->ray_at(p);check(ray.has_value(),"Circle ray missing");return *target.intersect_ray(ray->first,ray->second);};
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](const std::array<double,2>& p){return QPointF(((p[0]-a[0])*vy-(p[1]-a[1])*vx)/det,(ux*(p[1]-a[1])-uy*(p[0]-a[0]))/det);};
            const auto mouse=[&](QPointF p,QEvent::Type type,Qt::MouseButton button,Qt::MouseButtons held=Qt::NoButton){QMouseEvent event(type,p,QPointF(view->mapToGlobal(p.toPoint())),button,type==QEvent::MouseButtonPress?Qt::MouseButtons(button):held,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();};
            const auto click=[&](std::array<double,2> p){const auto at=screen(p);mouse(at,QEvent::MouseMove,Qt::NoButton);mouse(at,QEvent::MouseButtonPress,Qt::LeftButton);mouse(at,QEvent::MouseButtonRelease,Qt::LeftButton);};
            auto* action=window.findChild<QAction*>(rectangle?"sketchRectangleAction":"sketchSegmentAction");check(action&&action->isEnabled(),"Circle drawing action unavailable");action->trigger();flush();
            click(xy(-30.+8.*side,6.));
            const auto end=xy(30.-8.*side,rectangle?-6.08:6.08);
            mouse(screen(end),QEvent::MouseMove,Qt::NoButton);
            const auto mark=rectangle?"S":vertical?"V":"H";
            check(std::ranges::any_of(view->transient_labels(),[&](const auto& label){return label.second.find(mark)!=std::string::npos;}),"Circular contact did not advertise H/V or symmetry");
            click(end);
            QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&window,&escape);flush();
            const auto* active=window.active_sketch_for_test();check(active&&active->segments.size()==(rectangle?4u:1u),"Circular contact drawing was not committed");
            const auto first_id=active->segments[0].first_point_id;
            const auto second_id=rectangle?active->segments[1].second_point_id:active->segments[0].second_point_id;
            const auto references=active->external_references;
            const auto equations=[&](const sketcher::Sketch& s) {
                check(s.external_references==references,"Circular inference changed read-only references");
                const auto* p=s.find_point(first_id);const auto* q=s.find_point(second_id);check(p&&q,"Circular contact lost point identity");
                for(std::size_t i=0;i<2;++i) {
                    const auto* point=i?q:p;const auto center=xy(i?30.:-30.,0.);
                    check(std::abs(std::hypot(point->x-center[0],point->y-center[1])-*s.circular_radius(supports[i]))<1.e-6,"Circular contact equation failed");
                    check(std::ranges::any_of(s.constraints,[&](const auto& relation){return relation.first_point_id==point->id&&relation.geometry_id==supports[i]&&(relation.kind==K::PointOnCircle||relation.kind==K::PointOnLine);}),"Circular contact C was not persisted");
                }
                if(rectangle) {
                    for(const auto& relation:s.constraints)if(relation.kind==K::Symmetric) {
                        const auto* a=s.find_point(relation.first_point_id);const auto* b=s.find_point(relation.second_point_id);
                        const bool x=relation.geometry_id=="sketch_axis:x";
                        check(std::abs(x?a->x-b->x:a->x+b->x)<1.e-6&&std::abs(x?a->y+b->y:a->y-b->y)<1.e-6,"Rectangle lost circular contact symmetry");
                    }
                    for(const auto& edge:s.segments){const auto* u=s.find_point(edge.first_point_id);const auto* v=s.find_point(edge.second_point_id);check(std::abs(u->x-v->x)<1.e-6||std::abs(u->y-v->y)<1.e-6,"Circular rectangle lost H/V");}
                } else check(std::abs(vertical?p->x-q->x:p->y-q->y)<1.e-6,"Circle-to-circle segment lost H/V");
                for(const auto& d:s.dimensions)if(!d.suppressed) {
                    double measured{};
                    if(d.kind==D::Radius||d.kind==D::Diameter)measured=*s.circular_radius(d.geometry_id)*(d.kind==D::Diameter?2.:1.);
                    else {
                        const auto* a=s.find_point(d.first_point_id);
                        const auto* b=s.find_point(d.second_point_id);
                        check(a,"Circular dimension lost its point");
                        const double dx=b?b->x-a->x:a->x,dy=b?b->y-a->y:a->y;
                        if(d.kind==D::Distance)measured=std::hypot(dx,dy);
                        else if(d.kind==D::DistanceX)measured=dx;
                        else if(d.kind==D::DistanceY)measured=dy;
                        else throw std::runtime_error("Unverified circular dimension kind");
                    }
                    check(std::abs(measured-d.value)<1.e-6,"Circular dimension value disagrees with geometry");
                }
            };
            equations(*active);
            // Real mouse dragging must move the free contact, not just solve
            // the initial equations once. Both supports remain read-only.
            for(double shift:{.5,-.7,.4}) {
                active=window.active_sketch_for_test();const auto* p=active->find_point(first_id);const std::array before{p->x,p->y};
                auto destination=before;destination[vertical?0:1]+=shift;
                mouse(screen(before),QEvent::MouseMove,Qt::NoButton);mouse(screen(before),QEvent::MouseButtonPress,Qt::LeftButton);
                mouse(screen(destination),QEvent::MouseMove,Qt::NoButton,Qt::LeftButton);mouse(screen(destination),QEvent::MouseButtonRelease,Qt::LeftButton);
                active=window.active_sketch_for_test();equations(*active);p=active->find_point(first_id);
                check(std::hypot(p->x-before[0],p->y-before[1])>1.e-5,"Circular contact GUI drag did not move");
            }
            const auto baseline=*window.active_sketch_for_test();
            // Segment distance/projected distance, point-pair distance in both
            // orders, and axis position cover the independent editable DOF.
            for(int kind=0;kind<(rectangle?6:5);++kind)for(bool reverse:{false,true})
            for(bool driving:{false,true})for(bool locked:{false,true}) {
                auto s=baseline;const auto p=reverse?second_id:first_id,q=reverse?first_id:second_id;
                const auto* sa=s.find_point(s.segments[0].first_point_id);
                const auto* sb=s.find_point(s.segments[0].second_point_id);
                const auto segment_axis=std::abs(sa->x-sb->x)>std::abs(sa->y-sb->y)?D::DistanceX:D::DistanceY;
                auto dimension=kind==0?s.create_segment_dimension(s.segments[0].id):
                    kind==1?s.create_segment_dimension(s.segments[0].id,segment_axis):
                    kind==2?s.create_point_dimension(p,q):
                    kind==3?s.create_point_dimension(p,q,vertical?D::DistanceY:D::DistanceX):
                    kind==4?s.create_axis_dimension(p,vertical?"sketch_axis:y":"sketch_axis:x"):
                        s.create_segment_dimension(s.segments[1].id);
                dimension.driving=driving;dimension.locked=locked;
                const auto before=s.serialized();bool accepted=true;
                try{s.apply_dimension(dimension);}catch(const std::exception&){accepted=false;}
                if(!driving&&locked){check(!accepted&&s.serialized()==before,"Locked reference circle dimension was not rejected atomically");continue;}
                check(accepted,"Circle contact dimension was unexpectedly rejected");equations(s);++dimension_cases;
                const auto applied=std::ranges::find_if(s.dimensions,[&](const auto& d){return d.id==dimension.id;});
                const double displayed=sketcher::dimension_display_value(*applied,s.plane);
                for(double factor:{.98,1.02,1.}) {
                    if(driving)check(s.set_dimension_value(dimension.id,displayed*factor),"Circular contact dimension successive edit failed");
                    equations(s);
                }
                if(!driving) {
                    const auto unchanged=s.serialized();
                    static_cast<void>(s.set_dimension_value(dimension.id,displayed*1.2));
                    check(s.serialized()==unchanged,"Reference dimension changed circular geometry");
                } else {
                    const auto unchanged=s.serialized();
                    check(!s.set_dimension_value(dimension.id,10000.)&&s.serialized()==unchanged,"Impossible circular dimension was not rejected atomically");
                }
                const auto* p0=s.find_point(first_id);const std::array prior{p0->x,p0->y};
                const bool moved=s.move_point(first_id,p0->x+(vertical?.25:0.),p0->y+(vertical?0.:.25));
                if(driving&&locked)check(std::hypot(s.find_point(first_id)->x-prior[0],s.find_point(first_id)->y-prior[1])<1.e-7,"Fully constrained circular contact escaped on drag");
                else {const auto* p1=s.find_point(first_id);check(moved&&std::hypot(p1->x-prior[0],p1->y-prior[1])>1.e-5,"Editable circular contact did not move after dimensioning");}
                equations(s);equations(sketcher::Sketch::from_serialized(s.serialized()));
            }
            // H/V already determines the line angle: a driving angle is
            // redundant, while a reference angle remains an inspector.
            for(bool driving:{false,true})for(bool locked:{false,true}) {
                auto s=baseline;auto angle=s.create_segment_dimension(s.segments[0].id,D::Angle);
                angle.driving=driving;angle.locked=locked;const auto before=s.serialized();bool accepted=true;
                try{s.apply_dimension(angle);}catch(const std::exception&){accepted=false;}
                if(driving||locked)check(!accepted&&s.serialized()==before,"Redundant circular line angle changed the Sketch");
                else {
                    check(accepted,"Reference circular line angle was rejected");
                    const auto* a=s.find_point(angle.first_point_id);const auto* b=s.find_point(angle.second_point_id);
                    check(std::abs(std::atan2(b->y-a->y,b->x-a->x)*180./std::numbers::pi-angle.value)<1.e-7,"Circular reference angle is incorrect");
                    const auto unchanged=s.serialized();static_cast<void>(s.set_dimension_value(angle.id,37.));
                    check(s.serialized()==unchanged,"Reference angle drove circular contacts");
                }
            }
            if(!external)for(bool diameter:{false,true})for(bool driving:{false,true})for(bool locked:{false,true}) {
                auto s=baseline;
                std::erase_if(s.dimensions,[&](const auto& d){return d.geometry_id==supports[0];});
                auto radial=diameter?s.create_circle_diameter_dimension(supports[0]):s.create_circle_radius_dimension(supports[0]);
                radial.driving=driving;radial.locked=locked;const auto before=s.serialized();bool accepted=true;
                try{s.apply_dimension(radial);}catch(const std::exception&){accepted=false;}
                if(!driving&&locked){check(!accepted&&s.serialized()==before,"Locked reference radius was not rejected atomically");continue;}
                check(accepted,"Circular support R/D was rejected");equations(s);++dimension_cases;
                if(driving)for(double radius:{10.2,9.8,10.}) {
                    check(s.set_dimension_value(radial.id,radius*(diameter?2.:1.)),"Circular support R/D edit failed");equations(s);
                }
                for(double shift:{.3,-.4,.2}) {
                    const auto* p=s.find_point(first_id);const std::array prior{p->x,p->y};
                    check(s.move_point(first_id,p->x+(vertical?shift:0.),p->y+(vertical?0.:shift)),"R/D circular contact cannot drag");
                    check(std::hypot(s.find_point(first_id)->x-prior[0],s.find_point(first_id)->y-prior[1])>1.e-5,"R/D drag did not move");equations(s);
                }
                equations(sketcher::Sketch::from_serialized(s.serialized()));
            }
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::SketchPropertiesDialog*>(dialog);d&&d->isVisible()){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();break;}
            check(window.execute_console_command("save").ok,"Circular fixture save failed");flush();
            const auto saved=document::PartDocument::load(path);const auto& reopened=saved.sketches.back();equations(reopened);
            if(external)check(saved.sketches.front().serialized()==part.sketches.front().serialized(),"Circular inference modified its source Sketch");
            check(window.execute_console_command("undo").ok&&window.execute_console_command("redo").ok&&window.execute_console_command("save").ok,"Circular GUI Undo/Redo failed");
            check(document::PartDocument::load(path).sketches.back().serialized()==reopened.serialized(),"Circular GUI Undo/Redo changed contacts");
        }
        std::cout<<"Circular C + H/V/S: "<<cases<<" GUI cases, "<<dimension_cases<<" dimension variants, repeated edits/drags and native reopen passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Circular alignment GUI: "<<e.what()<<'\n';return 1;}
}
