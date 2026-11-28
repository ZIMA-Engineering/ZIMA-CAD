// Native authored-boundary equivalence, persistence and edit regression matrix.
workspace::SheetBodyPreparation diagnostic_preparation() {
    return std::getenv("ZIMA_EXPERIMENT_SHEET_SKELETON_FIRST")?
        workspace::SheetBodyPreparation::BoundarySkeleton:workspace::SheetBodyPreparation::Materialized;
}
void bend_allowance_equation_probe() {
    for(double k:{.318309886184,.5}) {
        kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();
        document::set_sheet_metal_defaults(part,{2.,k});
        const auto body=part.body_history.create_body("Allowance equation");
        auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
        auto profile=sketcher::Sketch::create_default();profile.owner_container_id=flat.id;flat.flat.sketch_id=profile.id;
        flat.flat.direction=document::ExtrusionDirection::Reverse;
        static_cast<void>(profile.add_rectangle(0,0,60,40));
        part.insert_history_entry(document::PartHistoryKind::Feature,flat.id);part.history.push_back(flat);part.sketches.push_back(profile);
        auto cache=workspace::calculate_part_with_resolved_references(engine,part);
        const auto& edges=cache.back().mesh.original_references.edges;
        const auto edge=std::ranges::find_if(edges,[](const auto& e){return kernel::sheet_edge_role(e)==kernel::SheetEdgeRole::Boundary&&
            std::ranges::all_of(e.points,[](auto p){return std::abs(p.y-40)<1e-6&&std::abs(p.z)<1e-6;});});
        check(edge!=edges.end(),"Allowance fixture has no base edge");
        auto bend=document::PartDocument::create_sketch_container();bend.feature_kind=document::FeatureKind::Bend;
        auto start=sketcher::Sketch::create_default();start.owner_container_id=bend.id;document::initialize_bend_start_profile(start,60);
        bend.bend.sketch_id=start.id;bend.bend.sheet_attachment=true;bend.bend.radius=4;bend.bend.angle_degrees=90;
        bend.placement.references=document::bend_sheet_references(*edge);
        part.insert_history_entry(document::PartHistoryKind::Feature,bend.id);part.history.push_back(bend);part.sketches.push_back(start);
        cache=workspace::calculate_part_with_resolved_references(engine,part,&cache);
        for(const auto correction:{std::array{0.,0.},std::array{.2,-.1}}) {
            auto* current=part.find_container(bend.id);
            document::set_bend_profile_extensions(*current,correction[0],correction[1]);
            auto& sketch=*std::ranges::find(part.sketches,current->bend.sketch_id,&sketcher::Sketch::id);
            document::prepare_bend_sketches(*current,sketch,document::sheet_metal_defaults(part));
            cache=workspace::calculate_part_with_resolved_references(engine,part,&cache);
            check(cache.back().calculation_errors.empty(),"Allowance calculation failed");
            const double width=60+(correction[0]+correction[1])*.5;
            const double angle=std::numbers::pi/2;
            const double expected_folded=60*40*2+width*angle*5*2;
            const double expected_developed=60*40*2+width*angle*(4+2*k)*2;
            near(measured_volume(engine,cache.back().body_outputs.at(body)),expected_folded,1e-5);
            const auto dxf=workspace::prepare_sheet_dxf(part,cache,engine);
            near(dxf.volume,expected_developed,1e-5);
            std::cout<<"allowance_k="<<k<<" end_corrections="<<correction[0]<<','<<correction[1]
                <<" expected_developed="<<expected_developed<<" actual_developed="<<dxf.volume<<std::endl;
        }
    }
}

void compare_independent_branch_solids(const TopoDS_Shape& first,const TopoDS_Shape& second) {
    constexpr double coordinate_tolerance=1e-7;
    const auto boundary_distance=[](const TopoDS_Shape& from,const TopoDS_Shape& to) {
        double worst{};
        int kind=-1,index=0;gp_Pnt worst_point;int worst_kind=-1,worst_index=0;
        const auto probe=[&](const gp_Pnt& point) {
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),to);
            check(distance.IsDone(),"Native geometry distance failed");
            double separation=distance.Value();
            if(separation>1e-7)for(TopExp_Explorer e(to,TopAbs_FACE);e.More();e.Next()) {
                // Broad-phase BRep distance can miss an interior point even on
                // the same tolerance-bounded deformed face. Independently solve
                // the exact surface projection and its actual trimmed UV domain.
                const auto face=TopoDS::Face(e.Current());TopLoc_Location location;
                const auto surface=BRep_Tool::Surface(face,location);
                double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
                GeomAPI_ProjectPointOnSurf projection(point.Transformed(location.Transformation().Inverted()),surface,u0,u1,v0,v1,1e-10);
                for(int n=1;n<=projection.NbPoints();++n) {
                    double u,v;projection.Parameters(n,u,v);
                    BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),1e-9);
                    if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)
                        separation=std::min(separation,point.Distance(projection.Point(n).Transformed(location.Transformation())));
                }
            }
            if(separation>worst){worst=separation;worst_point=point;worst_kind=kind;worst_index=index;}
        };
        for(TopExp_Explorer e(from,TopAbs_EDGE);e.More();e.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));
            for(int station=0;station<=16;++station)
                probe(curve.Value(std::lerp(curve.FirstParameter(),curve.LastParameter(),station/16.)));
        }
        for(TopExp_Explorer e(from,TopAbs_FACE);e.More();e.Next()) {
            const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface surface(face);
            kind=static_cast<int>(surface.GetType());++index;
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            for(int i=1;i<=3;++i)for(int j=1;j<=3;++j) {
                const double u=std::lerp(u0,u1,i/4.),v=std::lerp(v0,v1,j/4.);
                BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),1e-9);
                if(classifier.State()==TopAbs_IN)probe(surface.Value(u,v));
            }
        }
        if(worst>1e-7) {
            BRepExtrema_DistShapeShape self(BRepBuilderAPI_MakeVertex(worst_point).Vertex(),from);
            std::cout<<"native_distance_probe="<<worst<<" self_distance="<<self.Value()<<" surface_type="<<worst_kind<<" face="<<worst_index
                <<" point="<<worst_point.X()<<','<<worst_point.Y()<<','<<worst_point.Z()<<std::endl;
        }
        return worst;
    };
    const auto solids=[](const TopoDS_Shape& shape) {
        std::vector<TopoDS_Shape> result;
        for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())result.push_back(e.Current());
        return result;
    };
    check(BRepCheck_Analyzer(first).IsValid()&&BRepCheck_Analyzer(second).IsValid(),"Comparison found an invalid BRep");
    const auto a=solids(first),b=solids(second);
    check(!a.empty()&&a.size()==b.size(),"Independent branch solid count changed");
    std::set<std::size_t> used;
    for(const auto& source:a) {
        GProp_GProps properties,area;BRepGProp::VolumeProperties(source,properties,1e-12);
        BRepGProp::SurfaceProperties(source,area,1e-10);
        bool matched{};
        for(std::size_t j=0;j<b.size()&&!matched;++j)if(!used.contains(j)) {
            GProp_GProps candidate;BRepGProp::VolumeProperties(b[j],candidate,1e-12);
            // A scalar volume has cubic units: use the first-order boundary
            // displacement envelope, not an unrelated fixed linear tolerance.
            // Exact two-way differences and native boundary probes below are
            // still independently required at their original tolerances.
            const double volume_tolerance=1e-6+std::abs(area.Mass())*coordinate_tolerance;
            if(std::abs(properties.Mass()-candidate.Mass())>volume_tolerance||properties.CentreOfMass().Distance(candidate.CentreOfMass())>coordinate_tolerance)continue;
            bool equal=true;
            for(bool reverse:{false,true}) {
                BRepAlgoAPI_Cut difference(reverse?b[j]:source,reverse?source:b[j]);
                check(difference.IsDone(),"Branch difference calculation failed");
                GProp_GProps delta;BRepGProp::VolumeProperties(difference.Shape(),delta);
                equal=equal&&std::abs(delta.Mass())<=1e-6;
            }
            if(equal) {
                const double distance=std::max(boundary_distance(source,b[j]),boundary_distance(b[j],source));
                std::cout<<"branch_native_distance="<<distance<<" integral_delta="<<candidate.Mass()-properties.Mass()<<std::endl;
                check(distance<=coordinate_tolerance,"Independent native boundaries changed beyond tolerance");
                used.insert(j);matched=true;
            }
        }
        check(matched,"An independent branch changed material or position");
    }
    std::cout<<"equivalent_native_branches="<<used.size()<<std::endl;
}

void prepare_box_fixtures(const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    struct Case {const char* name;std::vector<std::array<double,2>> points;double draft;};
    const std::vector<Case> cases{
        {"rectangular",{{0,0},{100,0},{100,80},{0,80}},0},
        {"trapezoidal",{{0,0},{100,0},{80,80},{10,80}},0},
        {"chamfered-corner",{{0,0},{85,0},{100,15},{100,80},{0,80}},0},
        {"drafted",{{0,0},{100,0},{100,80},{0,80}},15},
        {"drafted-chamfer",{{0,0},{85,0},{100,15},{100,80},{0,80}},15},
        {"drafted-asymmetric",{{0,0},{85,0},{105,25},{75,80},{10,80}},20}
    };
    for(const auto& specimen:cases) {
        kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();
        // Separate allowance equations cover non-midplane neutral axes.
        document::set_sheet_metal_defaults(part,{2.,.5});
        const auto body=part.body_history.create_body(specimen.name);
        auto sketch=sketcher::Sketch::create_default();
        for(std::size_t i=0;i<specimen.points.size();++i) {
            const auto a=specimen.points[i],b=specimen.points[(i+1)%specimen.points.size()];
            static_cast<void>(sketch.add_segment(a[0],a[1],b[0],b[1]));
        }
        auto source=document::PartDocument::create_feature_container(sketch.id);
        sketch.owner_container_id=source.id;
        source.feature.sides[0].length=40;source.feature.sides[0].draft_angle_degrees=specimen.draft;
        part.insert_history_entry(document::PartHistoryKind::Feature,source.id);
        part.history.push_back(source);part.sketches.push_back(sketch);
        workspace::PartCalculationPolicy policy;policy.reject_errors=true;
        auto cache=workspace::calculate_part_with_resolved_references(engine,part,nullptr,policy);
        double bottom=INFINITY;
        for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
            for(const auto p:edge.points)bottom=std::min(bottom,p.z);
        std::vector<kernel::EdgeReference> edges;
        for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
            if(edge.points.size()>=2&&std::ranges::all_of(edge.points,[&](auto p){return std::abs(p.z-bottom)<1e-6;}))edges.push_back(edge.reference);
        check(edges.size()==specimen.points.size(),"Box fixture lost an authored base edge");
        auto fillet=document::PartDocument::create_fillet_container(edges);
        fillet.edge_treatment.routes.clear();fillet.edge_treatment.route_end_faces.clear();
        for(const auto& edge:edges){fillet.edge_treatment.routes.push_back({edge});fillet.edge_treatment.route_end_faces.emplace_back();}
        fillet.edge_treatment.primary_size=5;
        part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);part.history.push_back(fillet);
        cache=workspace::calculate_part_with_resolved_references(engine,part,&cache,policy);
        if(std::getenv("ZIMA_EXPERIMENT_CROSS_BODY_FIXTURE"))
            static_cast<void>(part.body_history.create_body("Converted sheet"));
        part.save(output/(std::string(specimen.name)+".prtz"),cache);
        std::cout<<"box_fixture="<<specimen.name<<" draft="<<specimen.draft<<std::endl;
    }
}

TopoDS_Shape converted_body_shape(const document::PartDocument& part,
        const std::vector<kernel::BodyResult>& cache) {
    const auto conversion=std::ranges::find_if(part.history,[](const auto& feature) {
        return feature.feature_kind==document::FeatureKind::SheetFromBody;
    });
    check(conversion!=part.history.end()&&!cache.empty(),"Native box comparison has no conversion");
    return kernel::read_calculated_body_shape(cache.back().body_outputs.at(
        part.body_owner_for_object(conversion->id)->scope.id));
}

void check_independent_overlap(const TopoDS_Shape& shape) {
    check(!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid(),"Overlap audit requires valid native material");
    std::vector<TopoDS_Shape> solids;
    for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())solids.push_back(e.Current());
    check(!solids.empty(),"Overlap audit has no native solid branches");
    double overlap{};
    for(std::size_t i=0;i<solids.size();++i)for(std::size_t j=i+1;j<solids.size();++j) {
        BRepAlgoAPI_Common common(solids[i],solids[j]);
        check(common.IsDone(),"Developed branch overlap calculation failed");
        GProp_GProps volume;BRepGProp::VolumeProperties(common.Shape(),volume);
        const auto pair_overlap=std::abs(volume.Mass());
        if(pair_overlap>1e-6)std::cout<<"overlap_pair="<<i<<','<<j<<" volume="<<pair_overlap<<std::endl;
        overlap+=pair_overlap;
    }
    std::cout<<"independent_overlap_volume="<<overlap<<std::endl;
    near(overlap,0,1e-6);
}

void boundary_skeleton_box_contract() {
    QTemporaryDir temporary("build/sheet-skeleton-matrix-XXXXXX");
    check(temporary.isValid(),"Box matrix temporary directory is unavailable");
    const std::filesystem::path root(temporary.path().toStdString());
    prepare_box_fixtures(root/"sources");
    for(const auto* name:{"rectangular","trapezoidal","chamfered-corner",
            "drafted","drafted-chamfer","drafted-asymmetric"}) {
        std::vector<kernel::BodyResult> cache;
        auto part=document::PartDocument::load(root/"sources"/(std::string(name)+".prtz"),&cache);
        const auto authored=part.serialized();
        double bottom=INFINITY;
        for(const auto point:cache.back().mesh.vertices)bottom=std::min(bottom,point.z);
        const auto& faces=cache.back().mesh.original_references.triangle_references;
        const auto seed=std::ranges::find_if(faces,[&](const auto& face) {
            return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                std::abs(face.surface->axis.z)>.999999&&std::abs(face.surface->origin.z-bottom)<1e-6;
        });
        check(seed!=faces.end(),"Box matrix has no authored base face");
        std::array<std::vector<std::size_t>,2> correction_counts;
        std::array<std::size_t,2> creation_counts{};
        for(int mode=0;mode<2;++mode) {
            kernel::OcctKernel engine;
            const auto folder=root/name/std::to_string(mode);std::filesystem::create_directories(folder);
            const auto count=kernel::body_calculation_count.load();
            const auto begin=std::chrono::steady_clock::now();
            auto result=workspace::prepare_sheet_from_body(part,cache,*seed,2,engine,true,{}, {},mode?
                workspace::SheetBodyPreparation::Automatic:workspace::SheetBodyPreparation::Materialized);
            creation_counts[mode]=kernel::body_calculation_count.load()-count;
            std::cout<<"box="<<name<<" skeleton="<<mode<<" preparation_seconds="
                <<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()
                <<" calculations="<<creation_counts[mode]<<std::endl;
            check(result.boundary_prepared_before_material==(mode!=0),"Box did not use the required preparation path");
            check(result.skipped==0,"Box conversion skipped a native region");
            const auto expected_walls=std::string_view(name)=="rectangular"||std::string_view(name)=="trapezoidal"||
                std::string_view(name)=="drafted"?4u:5u;
            check(result.created.size()==1+2*expected_walls,"Box conversion omitted a wall or Bend");
            check(part.serialized()==authored,"Conversion modified its source document");
            check_independent_overlap(converted_body_shape(result.document,result.calculated));
            result.document.save(folder/"converted.prtz",result.calculated);
            {
                auto flat=result.document;
                auto unbend=document::PartDocument::create_sketch_container();
                unbend.feature_kind=document::FeatureKind::Unbend;unbend.sheet_state.all=true;
                flat.insert_history_entry(document::PartHistoryKind::Feature,unbend.id);flat.history.push_back(unbend);
                const auto unfolded=workspace::calculate_part_with_resolved_references(engine,flat,&result.calculated);
                check(unfolded.back().calculation_errors.empty(),"Nominal box Unbend failed");
                check_independent_overlap(converted_body_shape(flat,unfolded));
                flat.save(folder/"nominal-unfolded.prtz",unfolded);
            }
            boundary_graph_correction_contract(result,engine,&correction_counts[mode],folder);
            if(mode==1&&(std::string_view(name)=="rectangular"||std::string_view(name)=="drafted-asymmetric")) {
                std::vector<kernel::BodyResult> edited_cache;
                auto edited=document::PartDocument::load(folder/"box-probe-corrected.prtz",&edited_cache);
                if(std::string_view(name)=="rectangular")
                    boundary_material_edit_contract(edited,edited_cache,folder/"fixed-material");
                auto* conversion=edited.find_container(result.container_id);
                conversion->sheet_from_body.thickness_override=false;
                for(const auto& binding:conversion->sheet_from_body.child_bindings) {
                    auto* feature=edited.find_container(binding.feature_id);
                    if(binding.bend)feature->bend.thickness_override=false;
                    else feature->flat.thickness_override=false;
                }
                boundary_material_edit_contract(std::move(edited),std::move(edited_cache),folder/"inherited-material");
            }
        }
        check(creation_counts[1]<creation_counts[0],"Boundary preparation did not remove intermediate material evaluations");
        check(correction_counts[0].size()==correction_counts[1].size(),"Box edit sequence changed");
        for(std::size_t i=0;i<correction_counts[0].size();++i)
            check(correction_counts[1][i]<=correction_counts[0][i],"Boundary correction added material evaluations");
        for(const auto* artifact:{"converted.prtz","nominal-unfolded.prtz","box-probe-corrected.prtz","box-probe-unfolded.prtz"}) {
            std::vector<kernel::BodyResult> first_cache,second_cache;
            auto first=document::PartDocument::load(root/name/"0"/artifact,&first_cache);
            auto second=document::PartDocument::load(root/name/"1"/artifact,&second_cache);
            std::cout<<"box="<<name<<" comparing="<<artifact<<std::endl;
            compare_independent_branch_solids(converted_body_shape(first,first_cache),converted_body_shape(second,second_cache));
            if(std::string_view(artifact).find("unfolded")!=std::string_view::npos)
                check_independent_overlap(converted_body_shape(second,second_cache));
            kernel::OcctKernel fresh_first,fresh_second;
            const auto first_definition=first.serialized(),second_definition=second.serialized();
            first_cache=workspace::calculate_part_with_resolved_references(fresh_first,first);
            second_cache=workspace::calculate_part_with_resolved_references(fresh_second,second);
            check(first_cache.back().calculation_errors.empty()&&second_cache.back().calculation_errors.empty(),"Box cold regeneration failed");
            check(first.serialized()==first_definition&&second.serialized()==second_definition,"Box cold regeneration changed native identities");
            compare_independent_branch_solids(converted_body_shape(first,first_cache),converted_body_shape(second,second_cache));
        }
    }
    bend_allowance_equation_probe();
    std::cout<<"Six-box boundary preparation, corrections, persistence, cold geometry, unfold and release passed"<<std::endl;
}
