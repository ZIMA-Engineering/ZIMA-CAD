static void verify_mesh_surface_placement(QApplication& application,AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    const auto run=[&](const char* command,commands::Json args=commands::Json::object()) {
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",args}}.dump()));
        if(!result.ok)throw std::runtime_error(result.message);flush();
    };
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;const auto body=graph.create_body("General surface placement");
    auto patch=document::create_boundary_surface();
    const std::array<kernel::Vec3,4> corners{{{0,0,0},{100,0,0},{100,80,20},{0,80,0}}};
    for(int i=0;i<4;++i) {
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        curve.curve_points.clear();curve.curve_type=document::Curve3DType::Polyline;
        for(auto p:{corners[i],corners[(i+1)%4]}) {auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);}
        patch.boundary_surface.boundaries[i]={curve.id,{}};graph.insert({document::PartHistoryKind::Construction,curve.id});part.constructions.push_back(curve);
    }
    graph.insert({document::PartHistoryKind::Feature,patch.id});part.history={patch};part.set_body_history(graph);part.resolve_constructions();
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"General surface fixture failed");
    const auto& geometry=calculated.back().mesh.original_references;
    const auto face=geometry.triangle_references.front();
    const auto path=directory/("mesh-placement-"+patch.id+".prtz");part.save(path,calculated);
    check(window.open_document_path(QString::fromStdString(path.string())),"General surface file cannot open");flush();run("body.activate",{{"body",body}});
    const auto dialog=[&]{return dynamic_cast<PrimitivePropertiesDialog*>(window.findChild<QWidget*>("featurePropertiesDialog"));};
    window.findChild<QAction*>("featureShortcut0Action")->trigger();flush();check(dialog(),"Mesh placement editor missing");
    auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());
    dialog()->findChild<QTableWidget*>("primitiveReferenceTable")->cellClicked(0,1);flush();view->fit_all();flush();
    std::optional<QPointF> location;std::optional<kernel::Vec3> expected;
    for(int y=20;y<view->height()-20&&!location;y+=9)for(int x=20;x<view->width()-20&&!location;x+=9) {
        const QPointF screen(x,y);const auto candidates=view->selection_candidates_at(screen);
        if(candidates.empty()||candidates.front().kind!=viewer::CandidateKind::Face || candidates.front().owner_id!=face.owner_id)continue;
        const auto ray=view->ray_at(screen);if(!ray)continue;
        const auto hit=confirmed_face_hit(view->candidate_face_triangles(candidates.front()),ray->first,ray->second);
        if(hit&&hit->x>20&&hit->x<80&&hit->y>20&&hit->y<60){location=screen;expected=*hit;}
    }
    check(location.has_value(),"General surface not offered by common picker");
    for(auto type:{QEvent::MouseMove,QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
        QMouseEvent event(type,*location,QPointF(view->mapToGlobal(location->toPoint())),
            type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&event);flush();
    }
    auto picked=dialog()->pending_value();
    check(!picked.placement.references.empty(),"General face click was rejected");
    check(document::resolve_placement(picked.placement,geometry),"Clicked mesh point cannot resolve");
    check(std::hypot(picked.placement.x-expected->x,picked.placement.y-expected->y,picked.placement.z-expected->z)<1e-5,"Mesh click moved to a different triangle");
    document::ConstructionReference x{{},patch.container_origin.id,"origin:plane:yz",0,true};x.measured_offset=expected->x;
    document::ConstructionReference y{{},patch.container_origin.id,"origin:plane:xz",0,true};y.measured_offset=expected->y;
    check(dialog()->set_reference(1,x,"X")&&dialog()->set_reference(2,y,"Y"),"Mesh reference capture failed");flush();
    check(dialog()->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Valid mesh placement disabled OK");
    const auto value=dialog()->pending_value();const auto id=value.id;
    check(std::abs(value.placement.references[1].offset-expected->x)<1e-6&&!value.placement.references[1].offset_locked&&
        std::abs(value.placement.references[2].offset-expected->y)<1e-6&&!value.placement.references[2].offset_locked,"Mesh did not capture and release both locks");
    auto bad=x;bad.offset=200;bad.measured_offset.reset();check(dialog()->set_reference(1,bad,"Outside"),"Invalid preview could not be entered");flush();
    check(!dialog()->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Impossible mesh placement allowed OK");
    x.offset=*x.measured_offset;check(dialog()->set_reference(1,x,"X"),"Corrected reference rejected");flush();
    check(dialog()->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Correcting mesh reference did not restore OK");
    window.grab().save(QString::fromStdString((directory/"general-surface-placement.png").string()));
    dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Mesh placement OK failed");run("save");
    auto saved=document::PartDocument::load(path);check(saved.find_container(id),"Mesh placement not saved");
    const auto stored=saved.find_container(id)->placement;
    run("undo");run("save");check(!document::PartDocument::load(path).find_container(id),"Mesh placement Undo failed");
    run("redo");run("regenerate");run("save");check(document::PartDocument::load(path).find_container(id),"Mesh placement Redo failed");
    run("close");check(window.open_document_path(QString::fromStdString(path.string())),"Mesh placement cannot reopen");flush();run("body.activate",{{"body",body}});
    QTreeWidgetItem* row{};
    for(QTreeWidgetItemIterator it(window.findChild<QTreeWidget*>("documentTree"));*it;++it)
        if((*it)->data(0,Qt::UserRole).toString().toStdString()==id){row=*it;break;}
    check(row,"Saved mesh Feature missing from tree");window.show_tree_item_properties(row);flush();check(dialog(),"Mesh properties cannot reopen");
    check(dialog()->buttons()->button(QDialogButtonBox::Ok)->isEnabled(),"Saved mesh placement became invalid");
    check(dialog()->set_reference(1,bad,"Outside"),"Invalid mesh edit failed");flush();dialog()->reject();flush();run("save");
    check(document::PartDocument::load(path).find_container(id)->placement==stored,"Mesh Cancel changed persisted placement");
}
