// Explicit calculation only. Native curve packets are converted without sampling.
occ::handle<Geom_BSplineCurve> loft_network_curve(const TopoDS_Wire& wire,double tolerance) {
    GeomConvert_CompCurveToBSplineCurve joined;
    for(BRepTools_WireExplorer it(wire);it.More();it.Next()) {
        double first,last;auto curve=BRep_Tool::Curve(it.Current(),first,last);
        if(curve.IsNull())throw std::runtime_error("Loft contains a missing native curve.");
        occ::handle<Geom_TrimmedCurve> segment=new Geom_TrimmedCurve(curve,first,last);
        if(it.Current().Orientation()==TopAbs_REVERSED)segment->Reverse();
        if(!joined.Add(segment,tolerance,true,true))
            throw std::runtime_error("Loft requires one connected contour in each Sketch.");
    }
    const auto result=joined.BSplineCurve();
    if(result.IsNull())throw std::runtime_error("Loft requires nonempty Sketches.");
    return result;
}
// A closed guide can cross each station more than once. Keep every branch
// between opposite end profiles; Gordon then validates the intermediate network.
std::vector<occ::handle<Geom_Curve>> loft_guide_branches(
    const TopoDS_Wire& guide,const TopoDS_Wire& first,const TopoDS_Wire& last,double tolerance) {
    const auto curve=loft_network_curve(guide,tolerance);
    if(!guide.Closed()&&!curve->IsClosed())return {curve};
    struct Contact {double parameter;bool last;};std::vector<Contact> contacts;
    for(bool end:{false,true}) {
        BRepAlgoAPI_Section section(guide,end?last:first,false);section.Build();
        if(!section.IsDone())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
        for(TopExp_Explorer it(section.Shape(),TopAbs_VERTEX);it.More();it.Next()) {
            GeomAPI_ProjectPointOnCurve projection(BRep_Tool::Pnt(TopoDS::Vertex(it.Current())),curve);
            if(projection.NbPoints()==0||projection.LowerDistance()>tolerance)continue;
            double parameter=projection.LowerDistanceParameter();
            if(std::abs(parameter-curve->LastParameter())<1e-10)parameter=curve->FirstParameter();
            if(std::ranges::none_of(contacts,[&](const auto& old){return std::abs(old.parameter-parameter)<1e-10;}))
                contacts.push_back({parameter,end});
        }
    }
    std::ranges::sort(contacts,{},&Contact::parameter);
    std::vector<occ::handle<Geom_Curve>> branches;
    for(std::size_t i=0;i<contacts.size();++i) {
        const auto& a=contacts[i];const auto& b=contacts[(i+1)%contacts.size()];
        if(a.last==b.last)continue;
        occ::handle<Geom_Curve> branch;
        if(i+1<contacts.size())branch=new Geom_TrimmedCurve(curve,a.parameter,b.parameter);
        else {
            GeomConvert_CompCurveToBSplineCurve joined;
            for(const auto interval:{std::pair{a.parameter,curve->LastParameter()},std::pair{curve->FirstParameter(),b.parameter}})
                if(interval.second-interval.first>1e-10&&!joined.Add(new Geom_TrimmedCurve(curve,interval.first,interval.second),tolerance,true,true))
                    throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
            branch=joined.BSplineCurve();
        }
        if(a.last)branch->Reverse();
        branches.push_back(std::move(branch));
    }
    if(branches.empty())throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
    return branches;
}
PrimitiveData make_loft_data(const LoftRequest& request,const std::string& owner) {
    const double tolerance=request.tolerance;
    if(request.profiles.size()<2||!std::isfinite(tolerance)||tolerance<=0||
       request.profile_owners.size()!=request.profiles.size()||request.guide_owners.size()!=request.guides.size())
        throw std::runtime_error("Loft requires at least two different profile Sketches.");
    if(request.guides.size()==1)
        throw std::runtime_error("A guided Loft requires at least two guide Sketches.");
    if(request.thin) {
        if(!request.make_solid)throw std::runtime_error("Thin Loft requires a solid result.");
        auto skin_request=request;skin_request.thin=false;
        const bool closed=std::ranges::all_of(request.profiles,[](const auto& p){return p.open_profile_end_id.empty();});
        skin_request.make_solid=closed;
        auto skin=make_loft_data(skin_request,owner);
        if(closed) {
            // Orient the closed carrier once, then discard its end caps.
            // Inward/outward thickness uses actual material sides, independent
            // of Gordon's internal curve sorting or surface parameterization.
            std::erase_if(skin.faces,[](const auto& face){return face.reference.semantic_key.starts_with("loft:cap:");});
            TopoDS_Shell shell;BRep_Builder builder;builder.MakeShell(shell);
            for(const auto& face:skin.faces)builder.Add(shell,face.shape);
            skin.shape=shell;
        }
        if(skin.faces.empty())throw std::runtime_error("Thin Loft has no surface to thicken.");
        for(auto& face:skin.faces)face.reference.surface_result=true;
        // Offset construction needs a finer working tolerance than network
        // contact validation. Keep the requested input budget unchanged.
        SurfaceThickenRequest thickness{skin.faces.front().reference,request.thickness,request.thickness_side,std::min(tolerance,1e-7)};
        return make_thickened_surface_data(thickness,owner,skin.shape,skin.faces,skin.edges,skin.vertices).operand;
    }
    std::vector<TopoDS_Wire> wires,guides;
    std::vector<std::vector<TopoDS_Edge>> source_edges;
    const auto prepare=[&](const auto& family,auto& output,bool profile) {
        for(const auto& input:family) {
            if(!input.inner_profiles.empty()||!input.additional_profile_regions.empty())
                throw std::runtime_error("Loft requires one contour without holes in each Sketch.");
            if(profile&&request.make_solid&&!input.open_profile_end_id.empty())
                throw std::runtime_error("A solid Loft requires closed profile Sketches.");
            std::vector<TopoDS_Edge> edges;
            output.push_back(make_profile_wire(input.outer_profile,input.direction,std::nullopt,&edges));
            if(profile)source_edges.push_back(std::move(edges));
        }
    };
    prepare(request.profiles,wires,true);prepare(request.guides,guides,false);
    PrimitiveData result;
    std::vector<std::pair<TopoDS_Shape,std::string>> authored_faces;
    if(guides.empty()) {
        BRepOffsetAPI_ThruSections build(request.make_solid,false,tolerance);
        for(const auto& wire:wires)build.AddWire(wire);
        build.Build();
        if(!build.IsDone())throw std::runtime_error("Loft could not construct the profile transition.");
        result.shape=build.Shape();
        for(std::size_t edge=0;edge<source_edges.front().size();++edge) {
            const auto& profile=request.profiles.front();
            const auto key="loft:skin:from:"+request.profile_owners.front()+":"+
                (edge<profile.outer_edge_source_ids.size()?profile.outer_edge_source_ids[edge]:profile.outer_boundary_id);
            const auto& generated=build.Generated(source_edges.front()[edge]);
            for(TopTools_ListIteratorOfListOfShape it(generated);it.More();it.Next())
                if(it.Value().ShapeType()==TopAbs_FACE)authored_faces.emplace_back(it.Value(),key);
        }
        if(request.make_solid) {
            authored_faces.emplace_back(build.FirstShape(),"loft:cap:from:"+request.profile_owners.front());
            authored_faces.emplace_back(build.LastShape(),"loft:cap:from:"+request.profile_owners.back());
        }
    } else {
        NCollection_Array1<occ::handle<Geom_Curve>> profiles(1,static_cast<int>(wires.size()));
        std::vector<occ::handle<Geom_Curve>> branches;
        for(const auto& guide:guides) {
            auto parts=loft_guide_branches(guide,wires.front(),wires.back(),tolerance);
            branches.insert(branches.end(),parts.begin(),parts.end());
        }
        NCollection_Array1<occ::handle<Geom_Curve>> rails(1,static_cast<int>(branches.size()));
        for(std::size_t i=0;i<wires.size();++i)profiles(static_cast<int>(i+1))=loft_network_curve(wires[i],tolerance);
        for(std::size_t i=0;i<branches.size();++i)rails(static_cast<int>(i+1))=branches[i];
        // Exact native contacts should not inherit a coarse parameterization
        // tolerance: it can make a valid rational skin unsuitable for offsets.
        // Retry at the authored contact budget only when the fine network fails.
        double construction_tolerance=std::min(tolerance*.01,1e-7);
        GeomFill_Gordon build;build.Init(profiles,rails,construction_tolerance);
        build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);
        build.Perform();
        if(!build.IsDone()&&construction_tolerance<tolerance) {
            construction_tolerance=tolerance;build.Init(profiles,rails,tolerance);
            build.SetApproximationMode(GeomFill_Gordon::ApproximationMode::ExactOnly);build.Perform();
        }
        if(!build.IsDone()||build.IsApproximate())
            throw std::runtime_error("Loft requires a compatible intersecting profile and guide network.");
        const auto& report=build.Report();
        if(std::max({report.MaxContactGap,report.MaxReparametrizationDeviation,
                     report.MaxProfileDeviation,report.MaxGuideDeviation})>tolerance)
            throw std::runtime_error("Loft exceeds the document geometry tolerance.");
        const auto skin=BRepBuilderAPI_MakeFace(build.Surface(),construction_tolerance).Face();
        authored_faces.emplace_back(skin,"loft:skin:from:"+request.region_id);
        result.shape=skin;
        if(request.make_solid) {
            BRepBuilderAPI_Sewing sewing(construction_tolerance);sewing.Add(skin);
            for(const auto endpoint:{std::size_t{0},wires.size()-1}) {
                BRepBuilderAPI_MakeFace cap(wires[endpoint],true);
                if(!cap.IsDone())throw std::runtime_error("Loft requires planar closed end profiles.");
                sewing.Add(cap.Face());
                authored_faces.emplace_back(cap.Face(),"loft:cap:from:"+request.profile_owners[endpoint]);
            }
            sewing.Perform();
            for(auto& [face,key]:authored_faces)if(sewing.IsModifiedSubShape(face))face=sewing.ModifiedSubShape(face);
            auto shell=sewing.SewedShape();
            if(shell.ShapeType()!=TopAbs_SHELL)throw std::runtime_error("Loft did not form one closed shell.");
            BRepBuilderAPI_MakeSolid solid(TopoDS::Shell(shell));
            auto shape=solid.Solid();
            if(!BRepLib::OrientClosedSolid(shape))throw std::runtime_error("Loft did not form one closed solid.");
            result.shape=shape;
        }
    }
    if(result.shape.IsNull()||!BRepCheck_Analyzer(result.shape).IsValid())
        throw std::runtime_error("Loft produced invalid native geometry.");
    for(TopExp_Explorer it(result.shape,TopAbs_FACE);it.More();it.Next()) {
        bool found=false;
        for(const auto& [source,key]:authored_faces)if(!source.IsNull()&&it.Current().IsSame(source)) {
            FaceReference reference{owner,key,{}};reference.surface_result=!request.make_solid;
            result.faces.push_back({it.Current(),std::move(reference)});found=true;break;
        }
        if(!found)throw std::runtime_error("Loft face ancestry could not be resolved.");
    }
    std::vector<TopoDS_Vertex> seen_vertices;std::set<std::string> vertex_keys;
    for(TopExp_Explorer it(result.shape,TopAbs_VERTEX);it.More();it.Next()) {
        const auto vertex=TopoDS::Vertex(it.Current());
        if(std::ranges::any_of(seen_vertices,[&](const auto& old){return old.IsSame(vertex);}))continue;
        seen_vertices.push_back(vertex);const auto position=BRep_Tool::Pnt(vertex);std::string key;
        for(const auto endpoint:{std::size_t{0},wires.size()-1}) {
            const auto& p=request.profiles[endpoint];
            for(std::size_t i=0;i<source_edges[endpoint].size();++i) {
                if(position.Distance(BRep_Tool::Pnt(TopExp::FirstVertex(source_edges[endpoint][i],true)))>tolerance)continue;
                key="loft:point:from:"+request.profile_owners[endpoint]+":"+
                    (i<p.outer_vertex_source_ids.size()?p.outer_vertex_source_ids[i]:p.outer_boundary_id);break;
            }
            if(key.empty()&&!p.open_profile_end_id.empty()&&
               position.Distance(BRep_Tool::Pnt(TopExp::LastVertex(source_edges[endpoint].back(),true)))<=tolerance)
                key="loft:point:from:"+request.profile_owners[endpoint]+":"+p.open_profile_end_id;
            if(key.empty()&&p.open_profile_end_id.empty()) {
                BRepExtrema_DistShapeShape distance(vertex,wires[endpoint]);
                if(distance.IsDone()&&distance.Value()<=tolerance)key="loft:point:seam:from:"+request.profile_owners[endpoint];
            }
            if(!key.empty())break;
        }
        if(key.empty()||!vertex_keys.insert(key).second)throw std::runtime_error("Loft point ancestry could not be resolved.");
        result.vertices.push_back({vertex,{owner,std::move(key),{}}});
    }
    const auto lies_on=[&](const TopoDS_Edge& candidate,const TopoDS_Edge& source) {
        BRepAdaptor_Curve curve(candidate);
        for(double f:{0.,.125,.25,.5,.75,.875,1.}) {
            const auto point=curve.Value(curve.FirstParameter()+f*(curve.LastParameter()-curve.FirstParameter()));
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(point).Vertex(),source);
            if(!distance.IsDone()||distance.Value()>tolerance)return false;
        }
        return true;
    };
    std::vector<TopoDS_Edge> seen_edges;std::set<std::string> edge_keys;
    for(TopExp_Explorer it(result.shape,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());
        if(BRep_Tool::Degenerated(edge)||std::ranges::any_of(seen_edges,[&](const auto& old){return old.IsSame(edge);}))continue;
        seen_edges.push_back(edge);std::string key;
        for(const auto endpoint:{std::size_t{0},wires.size()-1}) {
            const auto& p=request.profiles[endpoint];
            for(std::size_t i=0;i<source_edges[endpoint].size();++i)if(lies_on(edge,source_edges[endpoint][i])) {
                key="loft:edge:from:"+request.profile_owners[endpoint]+":"+
                    (i<p.outer_edge_source_ids.size()?p.outer_edge_source_ids[i]:p.outer_boundary_id);break;
            }
            if(!key.empty())break;
        }
        if(key.empty())for(const auto& vertex:result.vertices) {
            if(!vertex.reference.semantic_key.starts_with("loft:point:from:"+request.profile_owners.front()+":")&&
               vertex.reference.semantic_key!="loft:point:seam:from:"+request.profile_owners.front())continue;
            if(vertex.shape.IsSame(TopExp::FirstVertex(edge,true))||vertex.shape.IsSame(TopExp::LastVertex(edge,true))) {
                key="loft:longitudinal:from:"+vertex.reference.semantic_key;break;
            }
        }
        if(key.empty()||!edge_keys.insert(key).second)throw std::runtime_error("Loft edge ancestry could not be resolved.");
        result.edges.push_back({edge,{owner,std::move(key),{}}});
    }
    return result;
}
