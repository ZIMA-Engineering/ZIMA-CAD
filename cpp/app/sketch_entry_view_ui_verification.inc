void check_sketch_entry_camera(QApplication& application,zima::viewer::MeshView& view,
    const std::string& owner,bool x_right=true,bool y_up=true) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();application.processEvents();
    const auto& axes=view.mesh().axes;
    const auto x=std::ranges::find_if(axes,[&](const auto& a){return a.reference.owner_id==owner&&a.reference.semantic_key=="sketch_axis:x";});
    const auto y=std::ranges::find_if(axes,[&](const auto& a){return a.reference.owner_id==owner&&a.reference.semantic_key=="sketch_axis:y";});
    check(x!=axes.end()&&y!=axes.end(),"Sketch camera verification needs real displayed axes");
    const auto cross=[](auto a,auto b){return kernel::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto normal=cross(x->direction,y->direction);
    const auto project=[&](QPointF at) {
        const auto ray=view.ray_at(at);check(ray.has_value(),"Sketch camera ray missing");
        const auto delta=kernel::Vec3{x->point.x-ray->first.x,x->point.y-ray->first.y,x->point.z-ray->first.z};
        const double den=dot(normal,ray->second);check(std::abs(den)>1e-8,"Sketch camera is not facing its plane");
        const double t=dot(normal,delta)/den;
        return kernel::Vec3{ray->first.x+t*ray->second.x,ray->first.y+t*ray->second.y,ray->first.z+t*ray->second.z};
    };
    const auto p=project({view.width()*.5,view.height()*.5});
    const auto right=project({view.width()*.5+20,view.height()*.5});
    const auto up=project({view.width()*.5,view.height()*.5-20});
    const kernel::Vec3 dx{right.x-p.x,right.y-p.y,right.z-p.z},dy{up.x-p.x,up.y-p.y,up.z-p.z};
    check((x_right?1.:-1.)*dot(dx,x->direction)>1e-6,"Local X points to the wrong screen side");
    check((y_up?1.:-1.)*dot(dy,y->direction)>1e-6,"Local Y points to the wrong screen side");
    // Ray unprojection verifies actual screen directions above. Check roll
    // directly on unit vectors: subtracting translated float ray endpoints
    // adds cancellation error unrelated to the camera's angular accuracy.
    const auto state=view.camera_state();const QQuaternion rotation(state[0],state[1],state[2],state[3]);
    const auto actual_x=rotation.rotatedVector(QVector3D(x->direction.x,x->direction.y,x->direction.z)).normalized();
    const auto actual_y=rotation.rotatedVector(QVector3D(y->direction.x,y->direction.y,y->direction.z)).normalized();
    if(std::abs(actual_x.y())>=1e-5||std::abs(actual_y.x())>=1e-5) {
        std::cerr<<"Camera unit-axis cross projections "<<actual_x.y()<<','<<actual_y.x()<<std::endl;
        view.grabFramebuffer().save("build/sketch-camera-axis-failure.png");
    }
    check(std::abs(actual_x.y())<1e-5&&std::abs(actual_y.x())<1e-5&&
        std::abs(actual_x.z())<1e-5&&std::abs(actual_y.z())<1e-5,"Sketch camera has unintended in-plane roll");
}

int verify_sketch_entry_view_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        unsigned cases=0;
        for(auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})for(bool placed:{false,true}) {
            auto part=document::PartDocument::create_default();auto feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;sketch.plane=plane;sketch.plane_auto=false;sketch.refresh_default_frame();
            const auto circle=sketch.add_circle(12,9,3);auto radius=sketch.create_circle_radius_dimension(circle);radius.locked=placed;sketch.apply_dimension(radius);
            part.history={feature};part.sketches={sketch};document::BodyHistoryGraph graph;const auto body=graph.create_body("Sketch camera regression");graph.insert({document::PartHistoryKind::Feature,feature.id});
            if(placed){auto b=*graph.find(body);b.scope.placement.x=17;b.scope.placement.y=-23;b.scope.placement.z=5;b.scope.placement.absolute_rotation_x=20;b.scope.placement.absolute_rotation_y=35;b.scope.placement.absolute_rotation_z=-42;graph.update_body(b);}
            part.set_body_history(graph);part.resolve_constructions();
            if(placed)check(std::abs(part.body_history.find(body)->scope.placement.rotation_z+42)<1e-6,"Camera fixture Body lost its authored rotation");
            const auto path=directory/("sketch-entry-view-"+std::to_string(cases++)+".prtz");part.save(path);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,850);window.show();check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open camera fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate camera fixture Body");flush();
            auto* tree=window.findChild<QTreeWidget*>("documentTree");QTreeWidgetItem* row{};
            for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==sketch.id&&(*i)->data(0,Qt::UserRole+3)=="part-sketch"){row=*i;break;}
            check(row,"Camera fixture Sketch missing");window.show_tree_item_properties(row);flush();
            auto* open=window.findChild<QPushButton*>("sketchOpenButton");check(open,"Camera fixture Sketch open button missing");open->click();flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Camera fixture View missing");
            const auto state=window.execute_console_command("documents").data;
            const auto verify=[&](bool right=true,bool up=true){std::cout<<"Check camera plane="<<int(plane)<<" placed="<<placed<<" right="<<right<<" up="<<up<<std::endl;check_sketch_entry_camera(application,*view,sketch.id,right,up);check(window.execute_console_command("documents").data==state,"Camera controls changed document revision");};
            const bool local_y_up=plane!=sketcher::SketchPlane::XZ;
            verify(true,local_y_up);window.findChild<QAction*>("sketchFlipViewAction")->trigger();flush();verify(true,!local_y_up);
            window.findChild<QAction*>("sketchFlipViewAction")->trigger();flush();verify(true,local_y_up);
            for(int i=0;i<4;++i){window.findChild<QAction*>("sketchRotateViewAction")->trigger();flush();if(i==3)verify(true,local_y_up);}
            window.findChild<QAction*>("sketchNormalViewAction")->trigger();flush();verify(true,local_y_up);
            if(plane==sketcher::SketchPlane::XZ&&!placed) {
                const auto camera=view->camera_state();const QQuaternion rotation(camera[0],camera[1],camera[2],camera[3]);
                check(rotation.rotatedVector({1,0,0}).x()>1-1e-6&&rotation.rotatedVector({0,0,1}).y()>1-1e-6,
                    "Explicit XZ does not show world X right and world Z up");
                check(sketch.resolved_normal.y>1-1e-6&&sketch.resolved_y_axis.z<-1+1e-6,
                    "XZ camera changed the persisted modeling basis or extrusion side");
            }
            view->grabFramebuffer().save(QString::fromStdString((directory/("sketch-entry-view-"+std::to_string(cases)+".png")).string()));
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();
            for(auto* dialog:window.findChildren<QDialog*>())if(dialog->isVisible())if(auto* buttons=dialog->findChild<QDialogButtonBox*>())if(auto* cancel=buttons->button(QDialogButtonBox::Cancel)){cancel->click();flush();break;}
            check(window.execute_console_command("save").ok,"Camera fixture Save failed");const auto saved=document::PartDocument::load(path);
            check(saved.sketches.front().serialized()==part.sketches.front().serialized()&&saved.body_history==part.body_history&&saved.history.front().placement==part.history.front().placement,"Sketch camera changed persisted geometry or placement");
            std::cout<<"Sketch camera case "<<cases<<" plane="<<int(plane)<<" placed="<<placed<<" passed\n";
        }
        for(bool thin:{false,true})for(bool end_cap:{false,true}) {
            auto part=document::PartDocument::create_default();auto source=sketcher::Sketch::create_default();
            static_cast<void>(source.add_rectangle(0,0,30,20));
            auto base=document::PartDocument::create_feature_container(source.id);source.owner_container_id=base.id;
            base.feature.type=document::FeatureType::Modeling;base.feature.sides[0].length=10;
            base.feature.result_type=thin?document::ProfileResultType::Thin:document::ProfileResultType::Solid;base.feature.thin_thickness=2;
            part.history={base};part.sketches={source};document::BodyHistoryGraph graph;
            const auto body=graph.create_body("Face-attached camera regression");graph.insert({document::PartHistoryKind::Feature,base.id});
            part.set_body_history(graph);part.resolve_constructions();kernel::OcctKernel kernel;
            auto calculated=kernel.evaluate_history(part.kernel_operations());
            check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Face camera fixture calculation failed");
            const auto& refs=calculated.back().mesh.triangle_references;
            const auto face=std::ranges::find_if(refs,[&](const auto& ref){return ref.owner_id==base.id&&ref.semantic_key.starts_with(end_cap?"end:":"start:");});
            check(face!=refs.end(),"Face camera fixture cap missing");
            const auto path=directory/(std::string("sketch-face-camera-")+(thin?"thin-":"solid-")+(end_cap?"end":"start")+".prtz");
            part.save(path,calculated);
            app::AssemblyWorkspaceWindow window(QString::fromStdString(directory.string()));window.resize(1200,850);window.show();
            check(window.open_document_path(QString::fromStdString(path.string())),"Cannot open face camera fixture");flush();
            check(activate_test_body(application,window,body),"Cannot activate face camera fixture Body");flush();
            auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Face camera View missing");
            const auto state=window.execute_console_command("documents").data;
            window.findChild<QAction*>("extrusionAction")->trigger();flush();
            const auto dialog=[&]()->app::PrimitivePropertiesDialog* {
                for(auto* child:window.findChildren<QDialog*>())if(auto* d=dynamic_cast<app::PrimitivePropertiesDialog*>(child);d&&d->isVisible())return d;
                return nullptr;
            };
            check(dialog(),"Face-attached feature Properties missing");
            // Consume the ordinary shared reference-entry handler with the
            // actual calculated cap identity, including its persisted side.
            const document::ConstructionReference raw_face{{},face->owner_id,face->semantic_key,0,true,"front",true};
            const auto hover_face=dialog()->prepare_reference_candidate(0,raw_face);
            check(hover_face.orientation_role=="top"&&hover_face.orientation_drives_rotation,
                "First face hover validation does not consume the same real local Z role as confirmation");
            check(dialog()->set_reference(0,raw_face,"Calculated cap"),"Cannot attach feature to its real cap");flush();
            dialog()->set_profile_offset_and_forward_length(0,7);flush();
            const auto pending=dialog()->pending_value();
            check(pending.placement.references.front().orientation_role==hover_face.orientation_role,
                "First face hover and confirmation disagree about the local frame");
            check(std::ranges::any_of(pending.placement.references,[](const auto& ref){return ref.orientation_drives_rotation&&ref.orientation_role=="top";}),
                "First face was not stored as the real local Z orientation");
            auto* own_sketch=dialog()->findChild<QPushButton*>("primitiveOwnSketchButton");check(own_sketch,"Face-attached Sketch action missing");own_sketch->click();flush();
            check(!dialog()&&window.findChild<QAction*>("finishSketchAction")->isEnabled(),"Cannot enter face-attached feature Sketch");
            check_sketch_entry_camera(application,*view,pending.extrusion.sketch_id);
            const auto& axes=view->mesh().axes;
            const auto x_axis=std::ranges::find_if(axes,[&](const auto& axis){return axis.reference.owner_id==pending.extrusion.sketch_id&&axis.reference.semantic_key=="sketch_axis:x";});
            const auto y_axis=std::ranges::find_if(axes,[&](const auto& axis){return axis.reference.owner_id==pending.extrusion.sketch_id&&axis.reference.semantic_key=="sketch_axis:y";});
            const kernel::Vec3 rotation{pending.placement.rotation_x,pending.placement.rotation_y,pending.placement.rotation_z};
            const auto dot=[](auto a,auto b){return a.x*b.x+a.y*b.y+a.z*b.z;};
            check(x_axis!=axes.end()&&y_axis!=axes.end()&&
                dot(x_axis->direction,document::construction_direction_from_local_axis("x",rotation))>1-1e-6&&
                dot(y_axis->direction,document::construction_direction_from_local_axis("y",rotation))>1-1e-6,
                "Sketch axes do not match the owning feature's real local XY frame");
            check(window.execute_console_command("documents").data==state,"Entering face Sketch changed the document revision");
            window.findChild<QAction*>("finishSketchAction")->trigger();flush();check(dialog(),"Face Sketch did not return to feature Properties");
            const auto returned=dialog()->pending_value();
            check(returned.placement==pending.placement&&returned.extrusion.direction==pending.extrusion.direction&&
                returned.extrusion.length_forward==pending.extrusion.length_forward&&returned.extrusion.length_reverse==pending.extrusion.length_reverse,
                "Camera entry changed the cap side, placement or extrusion direction");
            dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            check(window.execute_console_command("documents").data==state,"Cancelling face camera check changed document revision");
            check(window.execute_console_command("save").ok,"Face camera fixture Save failed");const auto saved=document::PartDocument::load(path);
            check(saved.history==part.history&&saved.sketches.front().serialized()==source.serialized()&&saved.body_history==part.body_history,
                "Face Sketch camera changed saved source geometry or placement");
            std::cout<<"Face Sketch camera thin="<<thin<<" end="<<end_cap<<" passed\n";
        }
        std::cout<<"Six default/placed Body and four real solid/thin cap Sketch camera entry cases passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
