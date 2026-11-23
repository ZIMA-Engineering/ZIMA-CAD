int verify_curved_face_point_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto rotate=[](kernel::Vec3 p,kernel::Vec3 degrees,bool inverse) {
        const auto axis=[](kernel::Vec3 p,int i,double a) {
            const double c=std::cos(a),s=std::sin(a);
            if(i==0)return kernel::Vec3{p.x,c*p.y-s*p.z,s*p.y+c*p.z};
            if(i==1)return kernel::Vec3{c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
            return kernel::Vec3{c*p.x-s*p.y,s*p.x+c*p.y,p.z};
        };
        const std::array angles{degrees.x,degrees.y,degrees.z};
        for(int j=0;j<3;++j){const int i=inverse?2-j:j;p=axis(p,i,(inverse?-1:1)*angles[i]*std::acos(-1.)/180.);}
        return p;
    };
    const auto minus=[](kernel::Vec3 a,kernel::Vec3 b){return kernel::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto norm=[](kernel::Vec3 p){return std::hypot(p.x,p.y,p.z);};
    for(bool sweep:{false,true})for(bool placed:{false,true}) {
        std::cout<<"Curved-face Point GUI: sweep="<<sweep<<" placed="<<placed<<std::endl;
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_circle(0,0,40));
        auto cylinder=document::PartDocument::create_extrusion_container(sketch.id);
        cylinder.extrusion.length_forward=120;sketch.owner_container_id=cylinder.id;
        doc.sketches={sketch};doc.history={cylinder};document::BodyHistoryGraph graph;
        const auto body_id=graph.create_body("Point support");graph.insert({document::PartHistoryKind::Feature,cylinder.id});
        auto body=*graph.find(body_id);
        const kernel::Vec3 body_origin=placed?kernel::Vec3{20,30,40}:kernel::Vec3{};
        const kernel::Vec3 body_rotation=placed?kernel::Vec3{12,19,27}:kernel::Vec3{};
        body.scope.placement.x=body_origin.x;body.scope.placement.y=body_origin.y;body.scope.placement.z=body_origin.z;
        body.scope.placement.rotation_x=body_rotation.x;body.scope.placement.rotation_y=body_rotation.y;
        body.scope.placement.rotation_z=body_rotation.z;
        body.scope.placement.absolute_rotation_x=body_rotation.x;body.scope.placement.absolute_rotation_y=body_rotation.y;
        body.scope.placement.absolute_rotation_z=body_rotation.z;graph.update_body(body);doc.set_body_history(graph);
        doc.resolve_constructions();
        kernel::OcctKernel engine;
        const auto file=directory/(std::string(sweep?"sweep":"curve")+(placed?"-placed":"-origin")+"-surface-points.prtz");
        doc.save(file,engine.evaluate_history(doc.kernel_operations()));
        app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_HIDDEN_WINDOWS"))window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1600,1050);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        if(!verify(window.open_document_path(QString::fromStdWString(file.wstring())),"Cannot open curved-face Point fixture"))return 1;
        flush();auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
        if(!verify(view!=nullptr,"Curved-face Point fixture has no View"))return 1;
        view->set_standard_view(viewer::StandardView::Isometric);view->fit_all();flush();
        const auto dialog=[&]() -> app::ConstructionPropertiesDialog* {
            for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::ConstructionPropertiesDialog*>(child);d&&d->isVisible())return d;
            return nullptr;
        };
        window.findChild<QAction*>(sweep?"sweep3DAction":"curve3DAction")->trigger();flush();
        auto* parent=dialog();if(!verify(parent!=nullptr,"Cannot create Curve/Sweep"))return 1;
        const kernel::Vec3 parent_origin=placed?kernel::Vec3{12,-8,15}:kernel::Vec3{};
        const kernel::Vec3 parent_rotation=placed?kernel::Vec3{17,25,-12}:kernel::Vec3{};
        for(const auto& [name,value]:std::array<std::pair<const char*,double>,6>{{
                std::pair{"constructionX",parent_origin.x},{"constructionY",parent_origin.y},{"constructionZ",parent_origin.z},
                {"constructionRotationX",parent_rotation.x},{"constructionRotationY",parent_rotation.y},{"constructionRotationZ",parent_rotation.z}}})
            parent->findChild<QDoubleSpinBox*>(name)->setValue(value);
        flush();const auto before=kernel::body_calculation_count.load();
        const auto parent_id=sweep?parent->pending_sweep_value().id:parent->pending_value().id;
        std::vector<kernel::Vec3> positions;
        for(int point_index=0;point_index<3;++point_index) {
            auto* rows=parent->findChild<QTableWidget*>("curve3DPoints");emit rows->cellClicked(rows->rowCount()-1,0);flush();
            auto* point=dialog();if(!verify(point&&point!=parent,"Cannot create an owned Point"))return 1;
            std::optional<QPointF> pixel;kernel::Vec3 expected_body;double best=1e9;
            for(int y=20;y<view->height()-20;y+=12)for(int x=20;x<view->width()-20;x+=12) {
                const QPointF screen(x,y);const auto candidates=view->selection_candidates_at(screen);
                if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Face||candidates.front().owner_id!=cylinder.id)continue;
                const auto ray=view->ray_at(screen);if(!ray)continue;
                const auto origin=rotate(minus(ray->first,body_origin),body_rotation,true);
                const auto direction=rotate(ray->second,body_rotation,true);
                const double a=direction.x*direction.x+direction.y*direction.y;
                const double b=2*(origin.x*direction.x+origin.y*direction.y);
                const double c=origin.x*origin.x+origin.y*origin.y-1600;
                const double discriminant=b*b-4*a*c;if(a<1e-12||discriminant<=0)continue;
                const double t=(-b-std::sqrt(discriminant))/(2*a);
                const kernel::Vec3 hit{origin.x+t*direction.x,origin.y+t*direction.y,origin.z+t*direction.z};
                if(t<=0||hit.z<10||hit.z>110)continue;
                const double score=std::abs(hit.z-(25+35*point_index));
                if(score<best){best=score;pixel=screen;expected_body=hit;}
            }
            if(!verify(pixel.has_value()&&best<4,"Cannot find a visible cylinder point at the requested height"))return 1;
            QMouseEvent move(QEvent::MouseMove,*pixel,QPointF(view->mapToGlobal(pixel->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            QMouseEvent press(QEvent::MouseButtonPress,*pixel,QPointF(view->mapToGlobal(pixel->toPoint())),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(view,&press);flush();
            QMouseEvent release(QEvent::MouseButtonRelease,*pixel,QPointF(view->mapToGlobal(pixel->toPoint())),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&release);flush();
            const auto pending=point->pending_value();
            const auto expected_local=rotate(minus(expected_body,parent_origin),parent_rotation,true);
            const auto actual_rotated=rotate(pending.origin,parent_rotation,false);
            const kernel::Vec3 actual_body{actual_rotated.x+parent_origin.x,actual_rotated.y+parent_origin.y,actual_rotated.z+parent_origin.z};
            if(!verify(!pending.references.empty()&&pending.references.front().owner_id==cylinder.id&&
                    norm(minus(pending.origin,expected_local))<.2&&
                    std::abs(std::hypot(actual_body.x,actual_body.y)-40)<1e-7,
                    "Owned Point did not capture the clicked cylinder position in its local frame"))return 1;
            positions.push_back(pending.origin);
            point->buttons()->button(QDialogButtonBox::Ok)->click();flush();
            if(!verify(dialog()==parent&&parent->pending_value().curve_points.size()==positions.size(),"Point OK did not return to its owning path"))return 1;
        }
        auto* rows=parent->findChild<QTableWidget*>("curve3DPoints");emit rows->cellClicked(rows->rowCount()-1,0);flush();
        auto* canceled=dialog();if(!verify(canceled&&canceled!=parent,"Cannot open cancellable Point"))return 1;
        canceled->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        if(!verify(parent->pending_value().curve_points.size()==3&&kernel::body_calculation_count.load()==before,
                "Point drafts/Cancel inserted data or calculated a Body"))return 1;
        // A planar cap must retain the ordinary projection of the owning
        // Origin, even when the pointer is well away from that projection.
        rows=parent->findChild<QTableWidget*>("curve3DPoints");emit rows->cellClicked(rows->rowCount()-1,0);flush();
        auto* planar=dialog();if(!verify(planar&&planar!=parent,"Cannot create a planar support Point"))return 1;
        std::optional<QPointF> cap_pixel;
        for(int y=20;y<view->height()-20&&!cap_pixel;y+=12)for(int x=20;x<view->width()-20&&!cap_pixel;x+=12) {
            const QPointF pixel(x,y);const auto candidates=view->selection_candidates_at(pixel);
            if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Face||candidates.front().owner_id!=cylinder.id)continue;
            const auto ray=view->ray_at(pixel);if(!ray)continue;
            const auto origin=rotate(minus(ray->first,body_origin),body_rotation,true);
            const auto direction=rotate(ray->second,body_rotation,true);
            if(direction.z>=-1e-6)continue;
            const double t=(120-origin.z)/direction.z;
            const double px=origin.x+t*direction.x,py=origin.y+t*direction.y;
            if(t>0&&std::hypot(px,py)<32&&std::hypot(px-parent_origin.x,py-parent_origin.y)>8)cap_pixel=pixel;
        }
        if(!verify(cap_pixel.has_value(),"Cannot find the planar cylinder cap"))return 1;
        for(const auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            const bool move=type==QEvent::MouseMove,press=type==QEvent::MouseButtonPress;
            QMouseEvent event(type,*cap_pixel,QPointF(view->mapToGlobal(cap_pixel->toPoint())),
                move?Qt::NoButton:Qt::LeftButton,press?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&event);flush();
        }
        const auto planar_point=planar->pending_value();
        const auto expected_plane=rotate({0,0,120-parent_origin.z},parent_rotation,true);
        if(!verify(!planar_point.references.empty()&&norm(minus(planar_point.origin,expected_plane))<1e-6,
                "Planar support captured the mouse position instead of projecting the owning Origin"))return 1;
        planar->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        if(!verify(parent->pending_value().curve_points.size()==3&&kernel::body_calculation_count.load()==before,
                "Canceled planar Point changed the route or calculated a Body"))return 1;
        if(sweep) {
            std::cout<<"Create section"<<std::endl;
            auto* profiles=parent->findChild<QTableWidget*>("sweep3DProfiles");
            auto* first=qobject_cast<QPushButton*>(profiles->cellWidget(0,1));
            if(!verify(first&&first->isEnabled(),"Sweep cannot create its initial section"))return 1;
            first->click();flush();
            std::cout<<"Section opened"<<std::endl;
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            std::cout<<"Section returned"<<std::endl;
            if(!verify(dialog()==parent,"Cannot return from the section Sketch"))return 1;
            const auto* entry=parent->sweep_profile(0);if(!verify(entry!=nullptr,"Sweep has no initial section"))return 1;
            auto section=sketcher::Sketch::from_serialized(entry->sketch_serialized);
            static_cast<void>(section.add_circle(0,0,1));parent->set_sweep_profile_sketch(0,section);flush();
            std::cout<<"Section populated"<<std::endl;
        }
        QPointer<app::ConstructionPropertiesDialog> guard(parent);
        QTimer watchdog;
        bool dialog_error=false;
        QObject::connect(&watchdog,&QTimer::timeout,&window,[&] {
            for(auto* widget:QApplication::topLevelWidgets())if(auto* blocked=dynamic_cast<QMessageBox*>(widget);blocked&&blocked->isVisible()) {
                dialog_error=true;std::cerr<<blocked->text().toStdString()<<std::endl;blocked->reject();
            }
        });
        watchdog.start(100);
        std::cout<<"Confirm path"<<std::endl;
        parent->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        std::cout<<"Path confirmed"<<std::endl;
        if(!verify(!guard||!guard->isVisible(),"Curve/Sweep OK did not commit its surface Points"))return 1;
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        if(!verify(!dialog_error,"Curve/Sweep confirmation or Save displayed an error"))return 1;
        const auto saved=document::PartDocument::load(file);
        const auto check=[&](const auto& loaded) {
            const auto* owner=sweep?loaded.find_container(parent_id):nullptr;
            const auto* curve=sweep?(owner?&owner->sweep3d.path:nullptr):loaded.find_construction(parent_id);
            if(!curve||!curve->reference_valid||curve->curve_points.size()!=positions.size())return false;
            for(std::size_t i=0;i<positions.size();++i)if(!curve->curve_points[i].reference_valid||
                norm(minus(curve->curve_points[i].origin,positions[i]))>1e-7)return false;
            return true;
        };
        if(!verify(check(saved),"Saving lost the captured surface Points"))return 1;
        window.findChild<QAction*>("undoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        const auto undone=document::PartDocument::load(file);
        if(!verify(!undone.find_container(parent_id)&&!undone.find_construction(parent_id),"Undo retained the created path"))return 1;
        window.findChild<QAction*>("redoAction")->trigger();flush();
        window.findChild<QAction*>("saveDocumentAction")->trigger();flush();
        if(!verify(check(document::PartDocument::load(file)),"Redo lost the captured surface Points"))return 1;
        if(!verify(!dialog_error,"Curve/Sweep Undo/Redo displayed an error"))return 1;
    }
    std::cout<<"Curve/Sweep cylinder point clicks, transformed frames, Cancel, native persistence and Undo/Redo passed\n";
    return 0;
}
