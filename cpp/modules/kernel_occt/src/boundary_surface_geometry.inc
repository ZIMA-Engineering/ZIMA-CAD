// Explicit calculation only. All topology identities are authored before OCCT
// construction; shared curve handles locate their resulting boundary edges.
PrimitiveData make_boundary_surface_data(const BoundarySurfaceRequest& request,const std::string& owner) {
    const double tolerance=request.tolerance;
    if(!std::isfinite(tolerance)||tolerance<=0||request.region_id.empty())
        throw std::runtime_error("Invalid boundary surface definition.");
    struct Input {TopoDS_Edge edge;std::string key,first_key,last_key;};
    std::array<std::vector<Input>,4> chains;
    std::array<std::pair<gp_Pnt,gp_Pnt>,4> ends;
    std::set<std::string> edge_keys;
    for(std::size_t i=0;i<4;++i) {
        const auto& input=request.boundaries[i];
        if(request.source_owners[i].empty())throw std::runtime_error("Invalid boundary surface definition.");
        const auto* curves=std::get_if<ExtrusionRequest::CurvedProfile>(&input.outer_profile);
        if(!curves||curves->curves.empty()||input.outer_edge_source_ids.size()!=curves->curves.size()||
            input.outer_vertex_source_ids.size()!=curves->curves.size()||input.open_profile_end_id.empty())
            throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
        std::vector<TopoDS_Edge> edges;
        make_profile_wire(input.outer_profile,input.direction,std::nullopt,&edges);
        if(edges.size()!=curves->curves.size())throw std::runtime_error("Boundary curve identity could not be resolved.");
        for(std::size_t j=0;j<edges.size();++j) {
            const auto prefix="boundary:source:"+request.source_owners[i]+":";
            const auto last=j+1<edges.size()?input.outer_vertex_source_ids[j+1]:input.open_profile_end_id;
            if(input.outer_edge_source_ids[j].empty()||input.outer_vertex_source_ids[j].empty()||
                !edge_keys.insert(prefix+"edge:from:"+input.outer_edge_source_ids[j]).second)
                throw std::runtime_error("Invalid boundary surface definition.");
            chains[i].push_back({edges[j],prefix+"edge:from:"+input.outer_edge_source_ids[j],
                prefix+"vertex:from:"+input.outer_vertex_source_ids[j],prefix+"vertex:from:"+last});
        }
        ends[i]={BRep_Tool::Pnt(TopExp::FirstVertex(edges.front(),true)),BRep_Tool::Pnt(TopExp::LastVertex(edges.back(),true))};
        if(ends[i].first.Distance(ends[i].second)<=tolerance)
            throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
    }
    int orientation=-1;
    for(int mask=0;mask<16;++mask) {
        bool joined=true;
        for(int i=0;i<4;++i) {
            const int next=(i+1)%4;
            const auto end=(mask&(1<<i))?ends[i].first:ends[i].second;
            const auto start=(mask&(1<<next))?ends[next].second:ends[next].first;
            joined&=end.Distance(start)<=tolerance;
        }
        if(joined){orientation=mask;break;}
    }
    if(orientation<0)throw std::runtime_error("The four boundaries must join in perimeter order without gaps.");
    std::vector<Input> inputs;BRepBuilderAPI_MakeWire wire;
    for(int i=0;i<4;++i) {
        auto chain=chains[i];
        if(orientation&(1<<i)) {
            std::reverse(chain.begin(),chain.end());
            for(auto& input:chain){input.edge.Reverse();std::swap(input.first_key,input.last_key);}
        }
        for(auto& input:chain){wire.Add(input.edge);inputs.push_back(std::move(input));}
    }
    if(!wire.IsDone())throw std::runtime_error("The four boundaries must join in perimeter order without gaps.");
    // Two authored endpoints meet at each corner. Pick their canonical source
    // identity before filling, independent of which boundary row comes first.
    for(std::size_t i=0;i<inputs.size();++i) {
        auto& next=inputs[(i+1)%inputs.size()];
        const auto key=std::min(inputs[i].last_key,next.first_key);
        inputs[i].last_key=key;next.first_key=key;
    }
    if(!BRepAlgoAPI_Check(wire.Wire(),true,true).IsValid())
        throw std::runtime_error("Boundary curves must not cross or overlap.");
    const auto locate=[&](const TopoDS_Shape& shape,const TopoDS_Edge& input) {
        Standard_Real a,b;const auto curve=BRep_Tool::Curve(input,a,b);
        TopoDS_Edge match;unsigned count=0;
        for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());Standard_Real first,last;
            if(BRep_Tool::Curve(edge,first,last)==curve&&std::abs(first-a)<1e-12&&std::abs(last-b)<1e-12){match=edge;++count;}
        }
        if(count!=1)throw std::runtime_error("Boundary curve identity could not be resolved.");
        return match;
    };
    for(auto& input:inputs)input.edge=locate(wire.Wire(),input.edge);
    // Reject crossing/overlapping input edges before a filling algorithm can
    // silently relax conflicting constraints. Adjacent endpoints are allowed.
    for(std::size_t i=0;i<inputs.size();++i)for(std::size_t j=i+1;j<inputs.size();++j) {
        BRepExtrema_DistShapeShape distance(inputs[i].edge,inputs[j].edge);
        if(!distance.IsDone())throw std::runtime_error("Boundary intersection validation failed.");
        if(distance.Value()>tolerance)continue;
        if(j!=i+1&&!(i==0&&j+1==inputs.size()))
            throw std::runtime_error("Boundary curves must not cross or overlap.");
        const auto endpoint=[&](const TopoDS_Edge& e,const gp_Pnt& p){
            return p.Distance(BRep_Tool::Pnt(TopExp::FirstVertex(e,true)))<=tolerance||
                p.Distance(BRep_Tool::Pnt(TopExp::LastVertex(e,true)))<=tolerance;};
        for(int solution=1;solution<=distance.NbSolution();++solution)
            if(!endpoint(inputs[i].edge,distance.PointOnShape1(solution))||!endpoint(inputs[j].edge,distance.PointOnShape2(solution)))
                throw std::runtime_error("Boundary curves must not cross or overlap.");
    }
    BRepOffsetAPI_MakeFilling fill(3,25,3,false,1e-7,tolerance/4,0.01,0.1,8,32);
    for(const auto& input:inputs)fill.Add(input.edge,GeomAbs_C0);
    fill.Build();
    if(!fill.IsDone()||!BRepCheck_Analyzer(fill.Shape()).IsValid())
        throw std::runtime_error("OCCT could not create a valid boundary surface.");
    if(!std::isfinite(fill.G0Error())||fill.G0Error()>tolerance)
        throw std::runtime_error("Boundary surface exceeds the document geometry tolerance.");
    GProp_GProps area;BRepGProp::SurfaceProperties(fill.Shape(),area);
    if(!std::isfinite(area.Mass())||area.Mass()<=tolerance*tolerance)
        throw std::runtime_error("Boundary surface has no valid area.");
    PrimitiveData result;result.shape=fill.Shape();
    FaceReference face{owner,"surface:from:"+request.region_id,{}};face.surface_result=true;
    result.faces.push_back({result.shape,face});
    std::vector<TopoDS_Vertex> seen;
    for(const auto& input:inputs) {
        const auto edge=locate(result.shape,input.edge);
        result.edges.push_back({edge,{owner,input.key,{}}});
        for(const auto& [vertex,key]:std::array<std::pair<TopoDS_Vertex,std::string>,2>{
            std::pair{TopExp::FirstVertex(edge,true),input.first_key},std::pair{TopExp::LastVertex(edge,true),input.last_key}}) {
            if(std::ranges::none_of(seen,[&](const auto& v){return v.IsSame(vertex);})) {
                result.vertices.push_back({vertex,{owner,key,{}}});seen.push_back(vertex);
            }
        }
    }
    return result;
}
