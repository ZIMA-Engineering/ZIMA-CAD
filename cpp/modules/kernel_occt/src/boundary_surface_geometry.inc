// Explicit calculation only. All topology identities are authored before OCCT
// construction; shared curve handles locate their resulting boundary edges.
PrimitiveData make_boundary_surface_data(const BoundarySurfaceRequest& request,const std::string& owner,
        const std::vector<OwnedFace>& source_faces={},const std::vector<OwnedEdge>& source_edges={},
        const std::vector<OwnedVertex>& source_vertices={}) {
    const double tolerance=request.tolerance;
    if(!std::isfinite(tolerance)||tolerance<=0||request.region_id.empty()||
        !std::isfinite(request.angular_tolerance)||request.angular_tolerance<=0||
        !std::isfinite(request.curvature_tolerance)||request.curvature_tolerance<=0)
        throw std::runtime_error("Invalid boundary surface definition.");
    const auto count=request.boundaries.size();
    if(count<2||request.source_owners.size()!=count)
        throw std::runtime_error("Boundary surface requires at least two boundaries.");
    if(!request.constraints.empty()&&request.constraints.size()!=count)throw std::runtime_error("Invalid boundary surface definition.");
    struct Input {TopoDS_Edge edge;std::string key,first_key,last_key;TopoDS_Face support;SurfaceContinuity continuity{SurfaceContinuity::G0};};
    std::vector<std::vector<Input>> chains(count);
    std::vector<std::pair<gp_Pnt,gp_Pnt>> ends(count);
    std::set<std::string> edge_keys;
    struct SupportCopy {TopoDS_Face original;std::unique_ptr<BRepBuilderAPI_Copy> copy;};
    std::vector<SupportCopy> support_copies;
    for(std::size_t i=0;i<count;++i) {
        const auto& input=request.boundaries[i];
        if(request.source_owners[i].empty())throw std::runtime_error("Invalid boundary surface definition.");
        const BoundarySurfaceConstraint constraint=request.constraints.empty()?BoundarySurfaceConstraint{}:request.constraints[i];
        if(static_cast<unsigned>(constraint.continuity)>2)throw std::runtime_error("Invalid boundary surface definition.");
        if(constraint.edge) {
            if(!constraint.edge->valid()||!constraint.edge->instance_path.empty()||constraint.edge->owner_id!=request.source_owners[i])
                throw std::runtime_error("Invalid boundary surface definition.");
            TopoDS_Edge original;
            for(const auto& source:source_edges)if(source.reference==*constraint.edge) {
                if(!original.IsNull()&&!original.IsSame(source.shape))throw std::runtime_error("Original boundary edge is ambiguous.");
                original=TopoDS::Edge(source.shape);
            }
            if(original.IsNull())throw std::runtime_error("Original boundary edge is missing.");
            const auto vertex_key=[&](const TopoDS_Vertex& vertex) {
                std::string key;
                for(const auto& source:source_vertices)if(source.shape.IsSame(vertex)&&source.reference.owner_id==constraint.edge->owner_id) {
                    if(key.empty()||source.reference.semantic_key<key)key=source.reference.semantic_key;
                }
                if(key.empty())throw std::runtime_error("Boundary curve identity could not be resolved.");
                return "boundary:source:"+constraint.edge->owner_id+":vertex:from:"+key;
            };
            const auto key="boundary:source:"+constraint.edge->owner_id+":edge:from:"+constraint.edge->semantic_key;
            if(!edge_keys.insert(key).second)throw std::runtime_error("Boundary surface requires different boundaries.");
            TopoDS_Face support;
            if(constraint.continuity!=SurfaceContinuity::G0) {
                if(!constraint.support||!constraint.support->valid()||!constraint.support->instance_path.empty())
                    throw std::runtime_error("G1 and G2 require an original edge and its supporting face.");
                for(const auto& face:source_faces)if(same_face_identity(face.reference,*constraint.support)) {
                    if(!support.IsNull()&&!support.IsSame(face.shape))throw std::runtime_error("Supporting face is ambiguous.");
                    support=TopoDS::Face(face.shape);
                }
                if(support.IsNull())throw std::runtime_error("Supporting face is missing.");
                bool contains=false;
                for(TopExp_Explorer it(support,TopAbs_EDGE);it.More();it.Next())if(it.Current().IsSame(original)){contains=true;break;}
                if(!contains)throw std::runtime_error("The selected edge does not belong to the supporting face.");
                Standard_Real first,last;
                if(BRep_Tool::CurveOnSurface(original,support,first,last).IsNull())
                    throw std::runtime_error("G1 and G2 require an original edge and its supporting face.");
            }
            const auto first=TopExp::FirstVertex(original,true),last=TopExp::LastVertex(original,true);
            if(first.IsNull()||last.IsNull())throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
            const auto first_key=vertex_key(first),last_key=vertex_key(last);
            // Copy a constrained edge together with its face so their 2D curve
            // representations stay attached to the same copied surface. The
            // filling builder must not modify the immutable source geometry.
            TopoDS_Edge edge;
            if(support.IsNull()) {
                BRepBuilderAPI_Copy copy(original,true,false);edge=TopoDS::Edge(copy.Shape());
            } else {
                auto found=std::ranges::find_if(support_copies,[&](const auto& item){return item.original.IsSame(support);});
                if(found==support_copies.end()) {
                    support_copies.push_back({support,std::make_unique<BRepBuilderAPI_Copy>(support,true,false)});
                    found=std::prev(support_copies.end());
                }
                edge=TopoDS::Edge(found->copy->ModifiedShape(original));support=TopoDS::Face(found->copy->Shape());
            }
            if(constraint.support_reversed&&!support.IsNull())support.Reverse();
            chains[i].push_back({edge,key,first_key,last_key,support,constraint.continuity});
            ends[i]={BRep_Tool::Pnt(first),BRep_Tool::Pnt(last)};
            if(ends[i].first.Distance(ends[i].second)<=tolerance)
                throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
            continue;
        }
        if(constraint.continuity!=SurfaceContinuity::G0)throw std::runtime_error("G1 and G2 require an original edge and its supporting face.");
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
    // Two orientation states per chain keep this linear in boundary count,
    // including ambiguous tolerance-level joins, rather than trying 2^N masks.
    const auto start=[&](std::size_t i,int reversed){return reversed?ends[i].second:ends[i].first;};
    const auto end=[&](std::size_t i,int reversed){return reversed?ends[i].first:ends[i].second;};
    std::vector<bool> reversed(count);bool joined=false;
    for(int first=0;first<2&&!joined;++first) {
        std::vector<std::array<int,2>> previous(count,std::array<int,2>{-1,-1});
        previous[0][first]=first;
        for(std::size_t i=1;i<count;++i)for(int current=0;current<2;++current) {
            for(int prior=0;prior<2;++prior)if(previous[i-1][prior]>=0&&
                end(i-1,prior).Distance(start(i,current))<=tolerance) {
                previous[i][current]=prior;break;
            }
        }
        for(int last=0;last<2&&!joined;++last) {
            if(previous.back()[last]<0||end(count-1,last).Distance(start(0,first))>tolerance)continue;
            int current=last;
            for(std::size_t i=count;i-->0;) {
                reversed[i]=current!=0;current=previous[i][current];
            }
            joined=true;
        }
    }
    if(!joined)throw std::runtime_error("The boundaries must join in perimeter order without gaps.");
    std::vector<Input> inputs;BRepBuilderAPI_MakeWire wire;
    for(std::size_t i=0;i<count;++i) {
        auto chain=chains[i];
        if(reversed[i]) {
            std::reverse(chain.begin(),chain.end());
            for(auto& input:chain){input.edge.Reverse();std::swap(input.first_key,input.last_key);}
        }
        for(auto& input:chain){wire.Add(input.edge);inputs.push_back(std::move(input));}
    }
    if(!wire.IsDone())throw std::runtime_error("The boundaries must join in perimeter order without gaps.");
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
        // The face's wire use can reverse a located edge when OCCT chooses a
        // different surface orientation. Endpoint roles belong to the authored
        // input direction, not that runtime face use.
        match.Orientation(input.Orientation());
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
    const bool g2=std::ranges::any_of(inputs,[](const auto& input){return input.continuity==SurfaceContinuity::G2;});
    BRepOffsetAPI_MakeFilling fill(g2?4:3,25,3,false,1e-7,tolerance/4,request.angular_tolerance/4,request.curvature_tolerance/4,8,32);
    // A shared support already provides a suitable initial parametric surface.
    // Starting from it avoids solving an unnecessary plane-to-curved fit.
    if(!inputs.front().support.IsNull()&&std::ranges::all_of(inputs,[&](const auto& input){return !input.support.IsNull()&&input.support.IsSame(inputs.front().support);}))
        fill.LoadInitSurface(inputs.front().support);
    for(const auto& input:inputs) {
        if(input.continuity==SurfaceContinuity::G0)fill.Add(input.edge,GeomAbs_C0);
        else {
            // OCCT 8.0 forwards this enum to BRepFill_CurveConstraint's
            // integer Gi order. GeomAbs_G2 is 3 and is rejected there; order 2
            // is the actual curvature constraint (not a C1 downgrade).
            static_assert(static_cast<int>(GeomAbs_C1)==2);
            fill.Add(input.edge,input.support,input.continuity==SurfaceContinuity::G1?GeomAbs_G1:GeomAbs_C1);
        }
    }
    fill.Build();
    if(!fill.IsDone()||!BRepCheck_Analyzer(fill.Shape()).IsValid())
        throw std::runtime_error("OCCT could not create a valid boundary surface.");
    if(!std::isfinite(fill.G0Error())||fill.G0Error()>tolerance)
        throw std::runtime_error("Boundary surface exceeds the document geometry tolerance.");
    // OCCT adapts the point count per constraint during fitting. Its indexed
    // error accessors allocate only the initial point count and can overrun
    // those arrays. Use the maxima already evaluated by the fitting algorithm.
    const bool g1=std::ranges::any_of(inputs,[](const auto& input){return input.continuity!=SurfaceContinuity::G0;});
    if(g1&&(!std::isfinite(fill.G1Error())||fill.G1Error()>request.angular_tolerance))
        throw std::runtime_error("Fill surface does not satisfy the requested G1 continuity.");
    if(g2&&(!std::isfinite(fill.G2Error())||fill.G2Error()>request.curvature_tolerance))
        throw std::runtime_error("Fill surface does not satisfy the requested G2 continuity.");
    // Fitting errors concern the intermediate plate. Also verify derivatives
    // of the final approximated surface against each actual support. Compare
    // curvature operators in one tangent basis, including principal directions;
    // matching only the two curvature magnitudes would miss a rotated saddle.
    if(g1) {
        const auto output=BRep_Tool::Surface(TopoDS::Face(fill.Shape()));
        for(const auto& input:inputs) {
            if(input.continuity==SurfaceContinuity::G0)continue;
            Standard_Real first,last;const auto curve=BRep_Tool::CurveOnSurface(input.edge,input.support,first,last);
            const auto support=BRep_Tool::Surface(input.support);
            for(unsigned sample=0;sample<=64;++sample) {
                const auto parameter=first+(last-first)*(1-std::cos(std::numbers::pi*sample/64))/2;
                const auto uv=curve->Value(parameter);
                GeomLProp_SLProps source(support,uv.X(),uv.Y(),2,1e-9);
                GeomAPI_ProjectPointOnSurf projection(source.Value(),output,1e-9);
                if(projection.NbPoints()==0||projection.LowerDistance()>tolerance)
                    throw std::runtime_error("Boundary surface exceeds the document geometry tolerance.");
                double u,v;projection.LowerDistanceParameters(u,v);
                GeomLProp_SLProps target(output,u,v,2,1e-9);
                if(!source.IsNormalDefined()||!target.IsNormalDefined())
                    throw std::runtime_error("Fill surface does not satisfy the requested G1 continuity.");
                const gp_Vec normal(source.Normal()),other(target.Normal());
                if(std::atan2(normal.Crossed(other).Magnitude(),std::abs(normal.Dot(other)))>request.angular_tolerance)
                    throw std::runtime_error("Fill surface does not satisfy the requested G1 continuity.");
                if(input.continuity!=SurfaceContinuity::G2)continue;
                if(!source.IsCurvatureDefined()||!target.IsCurvatureDefined())
                    throw std::runtime_error("Fill surface does not satisfy the requested G2 continuity.");
                const auto tangent=source.D1U().Normalized(),across=normal.Crossed(tangent);
                const auto tensor=[&](GeomLProp_SLProps& properties) {
                    const double sign=gp_Vec(properties.Normal()).Dot(normal)<0?-1.:1.;
                    if(properties.IsUmbilic())return std::array<double,3>{sign*properties.MeanCurvature(),0,sign*properties.MeanCurvature()};
                    gp_Dir major,minor;properties.CurvatureDirections(major,minor);
                    const auto a=gp_Vec(major).Dot(tangent),b=gp_Vec(major).Dot(across);
                    const auto c=gp_Vec(minor).Dot(tangent),d=gp_Vec(minor).Dot(across);
                    const auto maximum=sign*properties.MaxCurvature(),minimum=sign*properties.MinCurvature();
                    return std::array<double,3>{maximum*a*a+minimum*c*c,maximum*a*b+minimum*c*d,maximum*b*b+minimum*d*d};
                };
                const auto a=tensor(source),b=tensor(target);
                const double trace=(a[0]-b[0]+a[2]-b[2])/2;
                const double spread=std::hypot((a[0]-b[0]-a[2]+b[2])/2,a[1]-b[1]);
                if(!std::isfinite(trace)||!std::isfinite(spread)||std::abs(trace)+spread>request.curvature_tolerance)
                    throw std::runtime_error("Fill surface does not satisfy the requested G2 continuity.");
            }
        }
    }
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
