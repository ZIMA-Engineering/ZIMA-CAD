// Exact angular contacts are operation-local and run only during calculation.
struct RevolutionContacts {
    TopoDS_Face face;
    gp_Ax1 axis;
    double tolerance;
    std::pair<double,double> angles(const gp_Pnt& point) const {
        const gp_Vec n(axis.Direction()),delta(axis.Location(),point);
        const gp_Pnt center=axis.Location().Translated(n*delta.Dot(n));
        const gp_Vec radial(center,point);
        if(radial.Magnitude()<=tolerance)
            throw std::runtime_error("Revolution profile misses target surface.");
        Handle(Geom_Circle) circle=new Geom_Circle(gp_Ax2(center,axis.Direction(),gp_Dir(radial)),radial.Magnitude());
        GeomAPI_IntCS intersection(circle,BRep_Tool::Surface(face));
        if(!intersection.IsDone()||intersection.NbSegments()!=0)
            throw std::runtime_error("Revolution target does not define an unambiguous end.");
        std::vector<double> hits;
        for(int i=1;i<=intersection.NbPoints();++i) {
            double u{},v{},angle{};intersection.Parameters(i,u,v,angle);
            BRepClass_FaceClassifier classified(face,gp_Pnt2d(u,v),tolerance);
            if(classified.State()!=TopAbs_IN&&classified.State()!=TopAbs_ON)continue;
            angle=std::fmod(angle,2*std::numbers::pi);if(angle<0)angle+=2*std::numbers::pi;
            if(angle*radial.Magnitude()<=tolerance)
                throw std::runtime_error("Revolution target does not define an unambiguous end.");
            GeomLProp_SLProps normal(BRep_Tool::Surface(face),u,v,1,Precision::Confusion());
            const gp_Vec tangent=n.Crossed(gp_Vec(center,intersection.Point(i)));
            if(!normal.IsNormalDefined()||std::abs(tangent.Normalized().Dot(gp_Vec(normal.Normal())))<1e-8)
                throw std::runtime_error("Revolution target does not define an unambiguous end.");
            hits.push_back(angle);
        }
        std::sort(hits.begin(),hits.end());
        hits.erase(std::unique(hits.begin(),hits.end(),[](double a,double b){return std::abs(a-b)<1e-9;}),hits.end());
        if(hits.empty())throw std::runtime_error("Revolution profile misses target surface.");
        return {hits.front(),hits.size()>1?hits[1]:2*std::numbers::pi};
    }
};

RevolutionContacts revolution_contacts(const RevolutionRequest& request,
        const std::optional<TopoDS_Face>& exact,double tolerance) {
    const auto& limit=*request.end_limit;
    TopoDS_Face face;
    if(limit.planar) {
        const gp_Pln plane(gp_Pnt(limit.origin.x,limit.origin.y,limit.origin.z),gp_Dir(limit.normal.x,limit.normal.y,limit.normal.z));
        face=BRepBuilderAPI_MakeFace(plane,-5'000'000.,5'000'000.,-5'000'000.,5'000'000.).Face();
    } else {
        if(!exact)throw std::runtime_error("Exact Revolution target surface is missing or ambiguous.");
        face=*exact;
    }
    if(request.mirror_end_limit) {
        gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),
            gp_Dir(request.profile_normal.x,request.profile_normal.y,request.profile_normal.z)));
        face=TopoDS::Face(BRepBuilderAPI_Transform(face,mirror,true).Shape());
    }
    return {face,gp_Ax1(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),
        gp_Dir(request.axis_direction.x,request.axis_direction.y,request.axis_direction.z)),std::max(Precision::Confusion(),tolerance)};
}

double revolution_limit_span(const std::vector<TopoDS_Wire>& wires,const RevolutionContacts& contact,double* actual_span) {
    double maximum{},next=2*std::numbers::pi;
    for(const auto& wire:wires)for(TopExp_Explorer it(wire,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        for(int sample=0;sample<=32;++sample) {
            const auto [first,second]=contact.angles(curve.Value(curve.FirstParameter()+
                (curve.LastParameter()-curve.FirstParameter())*sample/32.));
            maximum=std::max(maximum,first);next=std::min(next,second);
        }
    }
    // Do not sweep into a later crossing. The overrun is removed by exact Common.
    const double margin=std::min(.02,(next-maximum)*.25);
    if(!std::isfinite(maximum)||maximum<=0||margin<=1e-7||maximum+margin>=2*std::numbers::pi)
        throw std::runtime_error("Revolution target does not define an unambiguous end.");
    if(actual_span)*actual_span=maximum*180/std::numbers::pi;
    return (maximum+margin)*180/std::numbers::pi;
}

void clip_revolution_end(PrimitiveData& result,const RevolutionRequest& request,
        const RevolutionContacts& contact,const TopoDS_Shape& first,const TopoDS_Shape& last,
        const gp_Pnt& keep_point,const std::string& owner) {
    BRepPrimAPI_MakeHalfSpace half_space(contact.face,keep_point);
    BRepAlgoAPI_Common clip;set_boolean_inputs(clip,result.shape,half_space.Solid());
    clip.SetFuzzyValue(contact.tolerance);clip.SetToFillHistory(true);clip.Build();
    if(!clip.IsDone()||clip.Shape().IsNull()||!BRepCheck_Analyzer(clip.Shape(),true,false,true).IsValid())
        throw std::runtime_error("Revolution target does not define an unambiguous end.");
    TopTools_IndexedMapOfShape actual;TopExp::MapShapes(clip.Shape(),actual);
    // Every region must retain its complete start and lose its entire overrun.
    // Otherwise Common has kept a later orbit section or an incomplete end.
    for(TopExp_Explorer it(first,request.surface_result?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(!actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw std::runtime_error("Revolution target does not define an unambiguous end.");
    if(actual.Contains(last)||!clip.Modified(last).IsEmpty())
        throw std::runtime_error("Revolution profile misses target surface.");
    for(TopExp_Explorer it(last,request.surface_result?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw std::runtime_error("Revolution profile misses target surface.");
    const std::string role=request.first_cap_is_start?"end":"start";
    FaceReference cap{owner,profile_cap_semantic_key(role,request.profile_region_id),{}};cap.surface_result=request.surface_result;
    locate_clipped_profile_end(clip,result,owner,role);
    result.faces=propagate_topology(clip,result.faces,std::vector<OwnedFace>{{contact.face,cap}});
    std::erase_if(result.faces,[&](const auto& item){return !actual.Contains(item.shape);});
    result.edges=propagate_topology(clip,result.edges,{});
    result.vertices=propagate_topology(clip,result.vertices,{});
    std::erase_if(result.edges,[&](const auto& item){return !actual.Contains(item.shape);});
    std::erase_if(result.vertices,[&](const auto& item){return !actual.Contains(item.shape);});
    // Define replacement rims/points from the existing profile ancestry, then
    // locate them by adjacency in the calculated result (never by enumeration).
    for(const auto& face:result.faces)if(face.reference==cap) {
        for(TopExp_Explorer rim(face.shape,TopAbs_EDGE);rim.More();rim.Next()) {
            for(const auto& side:result.faces)if(side.reference.semantic_key.starts_with("generated:")) {
                bool adjacent=false;
                for(TopExp_Explorer e(side.shape,TopAbs_EDGE);e.More();e.Next())adjacent=adjacent||e.Current().IsSame(rim.Current());
                if(adjacent)result.edges.push_back({rim.Current(),{owner,role+":"+side.reference.semantic_key.substr(10),{}}});
            }
        }
    }
    for(const auto& edge:result.edges)if(edge.reference.semantic_key.starts_with("generated:"))
        for(TopExp_Explorer vertex(edge.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
            bool on_end=false;
            for(const auto& face:result.faces)if(face.reference==cap)
                for(TopExp_Explorer v(face.shape,TopAbs_VERTEX);v.More();v.Next())on_end=on_end||v.Current().IsSame(vertex.Current());
            if(on_end)result.vertices.push_back({vertex.Current(),{owner,role+":"+edge.reference.semantic_key.substr(10),{}}});
        }
    result.shape=clip.Shape();
}
