static void verify_face_wire(QApplication& application,QWidget& parent) {
    using namespace zima;
    viewer::MeshView view(&parent);view.setGeometry(0,0,500,360);
    kernel::ViewerMesh mesh;
    mesh.vertices={{-2,-1,0},{0,-1,0},{0,1,0},{-2,1,0},{2,-1,0},{2,1,0}};
    mesh.triangles={0,1,2,0,2,3,1,4,5,1,5,2};
    mesh.triangle_references={{"solid","left",{}},{"solid","left",{}},{"solid","right",{}},{"solid","right",{}}};
    mesh.original_references.vertices=mesh.vertices;mesh.original_references.triangles=mesh.triangles;
    mesh.original_references.triangle_references=mesh.triangle_references;
    view.set_mesh(mesh);view.set_display_mode(viewer::DisplayMode::Shaded);
    auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;view.set_camera_state(camera);
    view.set_selection_contract({viewer::CandidateKind::Face});view.show();view.raise();application.processEvents();
    const double offset=1/view.world_tolerance_for_pixels(1);
    const QPointF left(250-offset,180),right(250+offset,180);
    const auto move=[&](QPointF p) {
        QMouseEvent event(QEvent::MouseMove,p,p,view.mapToGlobal(p.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&event);application.processEvents();
    };
    const auto pixel=[&](const QImage& frame,QPointF p) {
        return frame.pixelColor(qRound(p.x()*frame.width()/view.width()),qRound(p.y()*frame.height()/view.height()));
    };
    const auto boundary_visible=[&](const QImage& frame) {
        const QPointF at=left-QPointF(offset,0);
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
            const auto c=pixel(frame,at+QPointF(x,y));
            if(c.red()<90&&c.green()>150&&c.blue()>205)return true;
        }
        return false;
    };
    move({2,2});view.clear_selection();const auto idle=view.grabFramebuffer();
    move(left);require(view.hovered_candidate()&&view.hovered_candidate()->semantic_key=="left","Face wire fixture selected the wrong face");
    const auto hover=view.grabFramebuffer();const auto h=pixel(hover,left),base=pixel(idle,left);
    require(h==base&&boundary_visible(hover),"Face hover filled its interior or omitted its boundary");
    require(pixel(hover,right)==pixel(idle,right),"Face hover tinted an adjacent face of the same solid");
    view.confirm_reference("solid","left",{},viewer::CandidateKind::Face);application.processEvents();
    const auto selected=view.grabFramebuffer();const auto s=pixel(selected,left);
    selected.save("interaction-face-selected.png");hover.save("interaction-face-hover.png");
    require(s==base&&boundary_visible(selected),"Confirmed face filled its interior or omitted its boundary");
    move({2,2});view.clear_selection();view.set_inspected_faces({{viewer::CandidateKind::Face,0,0,"solid","left",{},viewer::CandidateGeometry::OriginalReference}});
    application.processEvents();const auto inspected=view.grabFramebuffer();
    require(pixel(inspected,left)==base&&boundary_visible(inspected),"Face inspection did not use only its exact boundary");
    view.set_inspected_faces({});view.clear_selection();
    view.set_constraint_reference_highlights({},{{"solid","left",{}}});application.processEvents();
    const auto reference=view.grabFramebuffer();
    require(pixel(reference,left)==base&&boundary_visible(reference),"Placement reference eye filled the face or omitted its exact boundary");
    require(pixel(reference,right)==pixel(idle,right),"Placement reference eye tinted a sibling face");
    view.set_constraint_reference_highlights({},{});application.processEvents();
    const auto cleared=view.grabFramebuffer();
    require(pixel(cleared,left)==pixel(idle,left)&&!boundary_visible(cleared),"Ending placement inspection retained the face highlight");
    for(auto& reference:mesh.triangle_references)reference.semantic_key="plane";
    mesh.original_references.triangle_references=mesh.triangle_references;view.set_mesh(mesh,false);
    const auto plane_idle=view.grabFramebuffer();
    view.confirm_reference("solid","plane",{},viewer::CandidateKind::Face);application.processEvents();
    require(pixel(view.grabFramebuffer(),left)==pixel(plane_idle,left),"Datum plane received a solid-face fill");
}
