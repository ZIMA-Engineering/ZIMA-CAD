// Exact rotational surface contours and radius contacts, independent of OCCT.
void verify_surface_contours() {
    using K=kernel::SurfaceGeometry::Kind;
    const double pi=std::numbers::pi;
    kernel::ViewerMesh mesh;
    auto surface=std::make_shared<kernel::SurfaceGeometry>();
    surface->kind=K::Torus;surface->radius=5;surface->minor_radius=1;
    surface->u_max=2*pi;surface->v_min=pi;surface->v_max=1.5*pi;surface->complete_profile=true;
    kernel::FaceReference face{"round","fillet-face","assembly/bolt"};face.contour_surface=surface;
    for(int u=0;u<=32;++u)for(int v=0;v<=8;++v) {
        const auto a=2*pi*u/32,b=pi+pi*v/16;
        mesh.vertices.push_back({(5+std::cos(b))*std::cos(a),(5+std::cos(b))*std::sin(a),std::sin(b)});
    }
    for(unsigned u=0;u<32;++u)for(unsigned v=0;v<8;++v) {
        const auto a=u*9+v,b=a+9;
        for(const auto index:{a,b,a+1,b,b+1,a+1})mesh.triangles.push_back(index);
        mesh.triangle_references.push_back(face);mesh.triangle_references.push_back(face);
    }
    mesh.original_references.vertices=mesh.vertices;mesh.original_references.triangles=mesh.triangles;
    mesh.original_references.triangle_references=mesh.triangle_references;
    const auto encoded=document::serialize_surface_geometry(*surface);
    require(document::load_surface_geometry(encoded)==*surface,"Torus support/domain did not roundtrip");
    kernel::BodyResult body;body.mesh=mesh;
    const auto loaded=document::load_body_result(document::serialize_body_result(body));
    require(loaded.mesh.original_references.triangle_references.front().contour_surface&&
        *loaded.mesh.original_references.triangle_references.front().contour_surface==*surface&&
        !loaded.mesh.original_references.triangle_references.front().surface,"Native contour data leaked into placement support or failed to persist");
    unsigned cases=0;
    for(const int orientation:{1,-1}) {
        drawing::DrawingView view;view.id="profile-view";view.scale=5;
        view.camera={{0,0,1},{double(orientation),0,0},{0,double(-orientation),0}};
        drawing::prepare_interactive_view(view,std::make_shared<const kernel::ViewerMesh>(mesh));
        const auto profiles=drawing::projected_measurement_curves(view);
        unsigned contours=0;
        for(const auto& curve:profiles)if(curve.source.semantic_key.starts_with("drawing-contour:")) {
            ++contours;require(curve.circular&&curve.center&&std::abs(std::hypot(curve.cosine_axis.x,curve.cosine_axis.y)-1)<1e-9,"Contour lost exact radius");
            drawing::MeasurementPickRequest request;request.circles_only=true;
            const auto middle=curve.points[curve.points.size()/2];
            require(!drawing::measurement_candidates(view,middle,.01,request).empty(),"Visible meridian cannot be picked for radius");
            auto dimension=drawing::make_drawing_dimension(view.id,drawing::DrawingDimensionKind::Radius);
            dimension.attachments={{drawing::DimensionAttachmentKind::CurvePoint,curve.source,{},.5}};
            for(const bool reversed:{false,true})for(const bool shortened:{false,true})
            for(const double rotation:{0.,45.,90.,135.,180.,225.,270.,315.}) {
                dimension.segments[0].layout.arrows_reversed=reversed;
                dimension.segments[0].layout.radius_center_line_hidden=shortened;
                dimension.segments[0].layout.radius_rotation_degrees=rotation;
                drawing::refresh_drawing_dimension(view,dimension);
                const auto evaluated=drawing::evaluate_drawing_dimension(view,dimension);
                require(evaluated.state==drawing::MeasurementState::Resolved,"Meridian dimension failed");
                const auto& value=evaluated.presentations.front();
                const auto rim=value.witness_second;
                require(std::abs(std::hypot(rim.x-curve.center->x,rim.y-curve.center->y)-1)<1e-9,"Arrow left measured circle");
                // This source meridian occupies z <= 0 and radius <= R.
                require(rim.x<=1e-8&&std::abs(rim.y)<=5+1e-8,"Arrow escaped trimmed fillet arc");
                for(const drawing::Point2 movement:std::array{drawing::Point2{.1,.2},drawing::Point2{-.2,-.1},drawing::Point2{.1,-.1}}) {
                    drawing::drag_drawing_dimension(view,dimension,0,0,movement);
                    const auto moved=drawing::evaluate_drawing_dimension(view,dimension).presentations.front();
                    require(std::abs(moved.value-1)<1e-9&&moved.witness_second.x<=1e-8&&std::abs(moved.witness_second.y)<=5+1e-8,"Repeated drag lost radius/contact");
                }
                require(drawing::deserialize_drawing_dimensions(drawing::serialize_drawing_dimensions({dimension})).front()==dimension,"Contour attachment/layout did not roundtrip");
                ++cases;
            }
        }
        require(contours==2,"Both silhouette meridians must have independent face-based references");
        auto changed=mesh;auto next=std::make_shared<kernel::SurfaceGeometry>(*surface);next->minor_radius=.5;
        for(auto& reference:changed.triangle_references)reference.contour_surface=next;
        for(auto& reference:changed.original_references.triangle_references)reference.contour_surface=next;
        const auto updated=drawing::drawing_profile_mesh(changed,view.camera);
        require(updated.edges.size()==2&&updated.edges[0].reference==drawing::drawing_profile_mesh(mesh,view.camera).edges[0].reference,"Radius change replaced contour identity");
    }
    drawing::ProjectionCamera oblique{{1,0,0},{0,1,0},{0,0,1}};
    require(drawing::drawing_profile_mesh(mesh,oblique).edges.empty(),"Noncircular projection offered a circular meridian");
    auto cut=mesh;auto trimmed=std::make_shared<kernel::SurfaceGeometry>(*surface);trimmed->complete_profile=false;
    for(auto& reference:cut.triangle_references)reference.contour_surface=trimmed;
    require(drawing::drawing_profile_mesh(cut,{{0,0,1},{1,0,0},{0,-1,0}}).edges.empty(),"Nonrectangular trimmed face acquired a false contour");
    std::cout<<"Surface contour radius/drag matrix: "<<cases<<" cases passed\n";
}
