static void arc_curve_contracts() {
    using V=kernel::Vec3;
    const auto near=[](V a,V b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-8;};
    for(double radius:{7.,10.,15.})for(double sign:{-1.,1.}) {
        auto c=fixture(0);auto& path=c.sweep3d.path;path.curve_type=document::Curve3DType::Arc;
        path.curve_start_normal={0,0,sign};
        path.curve_points[0].origin={0,0,0};path.curve_points[1].origin={radius,0,sign*radius};
        path.curve_points[2].origin={2*radius,radius,sign*radius};
        auto end=document::create_owned_point(path.id);end.origin={2*radius,2*radius,sign*2*radius};path.curve_points.push_back(end);
        // Inactive authored spline controls must not influence an arc.
        for(auto& point:path.curve_points){point.curve_tangent_enabled=true;point.curve_tangent=document::Curve3DTangentMode::NegativeY;}
        const auto route=document::curve3d_route(path);
        const std::array<V,4> tangents={V{0,0,sign},V{1,0,0},V{0,1,0},V{0,0,sign}};
        const std::array<V,3> centers={V{radius,0,0},V{radius,radius,sign*radius},V{2*radius,radius,sign*2*radius}};
        require(route.segments.size()==3&&route.stations.size()==4,"Arc chain topology is incomplete");
        for(std::size_t i=0;i<4;++i) {
            require(near(route.stations[i].origin,path.curve_points[i].origin)&&near(route.stations[i].tangent,tangents[i]),"Arc chain lost a point or tangent continuity");
            require(route.stations[i].point_id==path.curve_points[i].id,"Arc chain replaced an authored point identity");
        }
        for(std::size_t i=0;i<3;++i) {
            const auto& arc=route.segments[i];require(arc.arc_midpoint.has_value()&&arc.bezier_control_points.empty(),"Arc became a spline");
            for(const auto p:{arc.start,*arc.arc_midpoint,arc.end})
                require(std::abs(std::hypot(p.x-centers[i].x,p.y-centers[i].y,p.z-centers[i].z)-radius)<1e-8,"Arc does not lie on its independent circle");
            const auto& a=arc.start;const auto& b=arc.end;const auto& m=*arc.arc_midpoint;
            require(std::abs(std::hypot(a.x-m.x,a.y-m.y,a.z-m.z)-std::hypot(b.x-m.x,b.y-m.y,b.z-m.z))<1e-8,"Arc midpoint is not halfway along the circle");
        }
        document::PartDocument display;display.constructions.push_back(path);
        const auto wire=display.construction_viewer_mesh(path.id,0,true);
        for(std::size_t i=0;i<3;++i) {
            const auto edge=std::ranges::find_if(wire.edges,[&](const auto& item){return item.reference.semantic_key==route.segments[i].source_id;});
            require(edge!=wire.edges.end()&&edge->points.size()>2&&edge->exact_spline,"Arc preview omitted its circular reference geometry");
            for(const auto p:edge->points)require(std::abs(std::hypot(p.x-centers[i].x,p.y-centers[i].y,p.z-centers[i].z)-radius)<1e-8,"Arc preview does not follow its circle");
        }
        auto doc=document::PartDocument::create_default();doc.history={c};
        kernel::OcctKernel engine;const auto calculated=engine.evaluate_history(doc.kernel_operations());
        require(calculated.back().calculation_errors.empty()&&std::abs(calculated.back().volume-1.5*radius*std::numbers::pi*std::numbers::pi)<.01,"Arc-chain Sweep has incorrect material volume");
        const auto file=std::filesystem::temp_directory_path()/("zima-arc-chain-"+c.id+".prtz");doc.save(file,calculated);
        const auto reopened=document::PartDocument::load(file);std::filesystem::remove(file);
        const auto& loaded=reopened.history.front().sweep3d.path;
        require(loaded.curve_type==document::Curve3DType::Arc&&near(loaded.curve_start_normal,path.curve_start_normal),"Arc direction did not survive native save/reopen");
        const auto restored=document::curve3d_route(loaded);
        for(std::size_t i=0;i<route.segments.size();++i)
            require(restored.segments[i].source_id==route.segments[i].source_id&&near(*restored.segments[i].arc_midpoint,*route.segments[i].arc_midpoint),"Arc identity or exact geometry changed after reload");
        kernel::OcctKernel cold;const auto repeated=cold.evaluate_history(reopened.kernel_operations());
        require(std::abs(repeated.back().volume-calculated.back().volume)<1e-7,"Arc Sweep changed after cold regeneration");
        auto invalid=path;invalid.curve_points[1].origin={0,0,5*sign};bool rejected=false;
        try{static_cast<void>(document::curve3d_route(invalid));}catch(const std::exception&){rejected=true;}
        require(rejected,"Collinear endpoint silently changed an Arc into another curve type");
    }
    for(double angle:{std::numbers::pi/2,std::numbers::pi,3*std::numbers::pi/2}) {
        auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
        curve.curve_type=document::Curve3DType::Arc;
        for(V p:{V{},V{10*(1-std::cos(angle)),0,10*std::sin(angle)}}) {
            auto point=document::create_owned_point(curve.id);point.origin=p;curve.curve_points.push_back(point);
        }
        const auto route=document::curve3d_route(curve);document::PartDocument display;display.constructions.push_back(curve);
        const auto mesh=display.construction_viewer_mesh(curve.id,0,true);
        const auto edge=std::ranges::find_if(mesh.edges,[&](const auto& item){return item.reference.semantic_key==route.segments.front().source_id;});
        require(edge!=mesh.edges.end()&&edge->exact_spline,"Semicircle or major Arc lost its exact reference");
        const auto& spline=*edge->exact_spline;
        for(unsigned i=0;i<=96;++i) {
            const auto p=kernel::bspline_value(spline,i/96.);
            require(std::abs(std::hypot(p.x-10,p.y,p.z)-10)<1e-8,"Arc rational reference changed its circle");
        }
        require(near(kernel::bspline_value(spline,.5),*route.segments.front().arc_midpoint),"Arc preview selected the complementary arc");
    }
    // The first entered plane supplies its oriented normal, independently of
    // manually stored per-point XYZ tangent choices. A missing reference keeps
    // the last solved normal without validating that broken dependency.
    auto doc=document::PartDocument::create_default();auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
    curve.curve_type=document::Curve3DType::Arc;
    for(V position:{V{0,0,0},V{10,10,0}}){auto p=document::create_owned_point(curve.id);p.origin=position;curve.curve_points.push_back(p);}
    document::ConstructionReference ref;ref.owner_id="arc-support";ref.semantic_key="plane";ref.supports_offset=true;
    curve.curve_points.front().references.push_back(ref);doc.constructions.push_back(curve);
    kernel::ViewerReferenceGeometry source;source.vertices={{0,-20,-20},{0,20,-20},{0,0,20}};source.triangles={0,1,2};
    kernel::FaceReference face{"arc-support","plane",{}};kernel::SurfaceGeometry plane;plane.kind=kernel::SurfaceGeometry::Kind::Plane;plane.axis={1,0,0};plane.radial={0,1,0};
    face.surface=std::make_shared<const kernel::SurfaceGeometry>(plane);source.triangle_references.push_back(face);
    doc.resolve_constructions(source);
    require(doc.constructions.front().reference_valid&&near(doc.constructions.front().curve_start_normal,{1,0,0}),"First Arc ignored the first entered plane");
    doc.constructions.front().curve_points.front().references.front().flip=true;doc.resolve_constructions(source);
    require(near(doc.constructions.front().curve_start_normal,{-1,0,0}),"First Arc ignored the plane side");
    doc.resolve_constructions(kernel::ViewerReferenceGeometry{});
    require(!doc.constructions.front().reference_valid&&near(doc.constructions.front().curve_start_normal,{-1,0,0}),"Missing Arc plane lost the last normal or remained valid");
    std::cout<<"Arc chain normal, side, exact circles, spatial tangency, native Sweep and persistence passed\n";
}
