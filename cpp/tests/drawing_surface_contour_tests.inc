// Exact rotational surface contours and radius contacts, independent of OCCT.
void verify_surface_contours(const std::filesystem::path& directory) {
    using K=kernel::SurfaceGeometry::Kind;
    const double pi=std::numbers::pi;
    {
        kernel::ViewerMesh arc;kernel::ViewerEdge edge;edge.reference={"rim","trimmed-circle",{}};
        for(int i=0;i<=5;++i){const double t=-pi/3+2*pi*i/15;edge.points.push_back({2.75*std::cos(t),2.75*std::sin(t),-2});}
        arc.edges={edge};const auto exact=drawing::drawing_profile_mesh(arc,{{0,0,1},{1,0,0},{0,-1,0}});
        double high=0;for(const auto p:exact.edges.front().points)high=std::max(high,p.x);
        require(std::abs(high-2.75)<1e-12,"Trimmed edge-on circle leaves a gap at its extremum");
        require(exact.edges.front().points.front()==edge.points.front()&&exact.edges.front().points.back()==edge.points.back(),"Extremum refinement changed trimmed circle endpoints");
    }
    for(const int count:{25,26,27})for(const double phase:{0.,.17}) {
        kernel::ViewerMesh circle;kernel::ViewerEdge edge;edge.reference={"rim","circle",{}};
        for(int i=0;i<=count;++i){const auto t=phase+2*pi*i/count;edge.points.push_back({2.75*std::cos(t),2.75*std::sin(t),-2});}
        edge.points.back()=edge.points.front();circle.edges.push_back(edge);
        const drawing::ProjectionCamera camera{{0,0,1},{1,0,0},{0,-1,0}};
        const auto exact=drawing::drawing_profile_mesh(circle,camera);
        double low=0,high=0;for(const auto p:exact.edges.front().points){low=std::min(low,p.x);high=std::max(high,p.x);}
        require(std::abs(low+2.75)<1e-12&&std::abs(high-2.75)<1e-12,"Edge-on circle display stops short of its exact extrema");
        require(exact.edges.front().reference==edge.reference&&circle.edges.front().points==edge.points,"Circle display refinement changed native reference or source samples");
    }
    {
        kernel::ViewerMesh cone;
        auto support=std::make_shared<kernel::SurfaceGeometry>();support->kind=K::Cone;
        support->radius=5;support->semi_angle=std::atan(.5);support->axial_max=2.8;
        kernel::FaceReference face{"cone","trimmed",{}};face.surface=support;
        const auto plane=[](std::string name,kernel::Vec3 origin,kernel::Vec3 normal) {
            kernel::FaceReference f{"trim",std::move(name),{}};auto s=std::make_shared<kernel::SurfaceGeometry>();
            s->origin=origin;s->axis=normal;f.surface=s;return f;
        };
        const auto bottom=plane("bottom",{0,0,0},{0,0,1}),top=plane("top",{6,0,0},kernel::dimension_unit({1,0,1}));
        std::array<kernel::ViewerEdge,4> edges;
        for(unsigned i=0;i<4;++i){edges[i].reference={"trim","edge"+std::to_string(i),{}};edges[i].edge_treatment_side_references={face};}
        edges[0].edge_treatment_side_references.push_back(bottom);edges[1].edge_treatment_side_references.push_back(top);
        for(unsigned i=0;i<=24;++i) {
            const double angle=-pi/3+2*pi*i/72,z=(6-5*std::cos(angle))/(1+.5*std::cos(angle));
            for(const double station:{0.,z})cone.vertices.push_back({(5+.5*station)*std::cos(angle),(5+.5*station)*std::sin(angle),station});
            edges[0].points.push_back(cone.vertices[2*i]);edges[1].points.push_back(cone.vertices[2*i+1]);
            if(i)for(const auto index:{2*i-2,2*i,2*i-1,2*i,2*i+1,2*i-1})cone.triangles.push_back(index);
            if(i){cone.triangle_references.push_back(face);cone.triangle_references.push_back(face);}
        }
        for(unsigned i=0;i<2;++i) {
            const unsigned offset=i?48:0;edges[i+2].points={cone.vertices[offset],cone.vertices[offset+1]};
            const auto p=edges[i+2].points.front();
            edges[i+2].edge_treatment_side_references.push_back(plane("side"+std::to_string(i),{},kernel::dimension_unit({-p.y,p.x,0})));
        }
        for(auto& edge:edges)cone.edges.push_back(std::move(edge));
        const auto exact=drawing::drawing_profile_mesh(cone,{{0,0,1},{1,0,0},{0,-1,0}});
        std::vector<kernel::ViewerEdge> contours;
        for(const auto& edge:exact.edges)if(edge.reference.semantic_key.starts_with("drawing-contour:"))contours.push_back(edge);
        require(contours.size()==1,"Convex cone trim did not retain exactly its in-domain generator");
        const auto a=contours.front().points.front(),b=contours.front().points.back();
        require(std::abs(a.x-5)<1e-9&&std::abs(a.z)<1e-9&&std::abs(b.x-16./3)<1e-9&&std::abs(b.z-2./3)<1e-9,"Cone trim used sampled bounds instead of exact plane intersection");
        auto snapshot=drawing::DrawingDocument::create_default();
        auto view=drawing::DrawingDocument::create_view("unavailable","missing.prtz",cone,drawing::ViewOrientation::Front);
        view.camera={{0,0,1},{1,0,0},{0,-1,0}};
        drawing::prepare_interactive_view(view,std::make_shared<const kernel::ViewerMesh>(cone));
        // Reproduce a saved projection lacking its analytic boundary. Opening
        // must use the embedded calculated packet, without loading the Part.
        std::erase_if(view.projected_edges,[](const auto& e){return e.source.semantic_key.starts_with("drawing-contour:");});
        auto stale=*view.measurement_geometry;
        std::erase_if(stale.curves,[](const auto& c){return c.source.semantic_key.starts_with("drawing-contour:");});
        view.measurement_geometry=drawing::share_measurement_geometry(std::move(stale));
        snapshot.sheets.front().views={view};const auto path=directory/"embedded-contour.drwz";snapshot.save(path);
        const auto reopened=drawing::DrawingDocument::load(path);const auto& restored=reopened.sheets.front().views.front();
        drawing::MeasurementPickRequest request;request.lines_only=true;
        const auto picks=drawing::measurement_candidates(restored,{1./3,31./6},.01,request);
        require(std::ranges::any_of(picks,[&](const auto& p){return p.attachment.reference==contours.front().reference;}),"Opening a Drawing did not rebuild analytic contour picking from its embedded source");
        require(restored.source_path==view.source_path&&restored.id==view.id&&restored.scale==view.scale,"Opening contour projection changed authored view properties");
    }
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
