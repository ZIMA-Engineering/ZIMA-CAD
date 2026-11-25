struct WrapSourceEdge {TopoDS_Edge shape;std::string key,start,end;};
using WrapSourceWire=std::vector<WrapSourceEdge>;
using WrapSourceRegion=std::vector<WrapSourceWire>;

std::vector<WrapSourceRegion> wrapping_source_regions(const ExtrusionRequest& profile,
        const WrappingRequest& request,const WrapChart& chart,const std::string& owner,const std::string& prefix) {
    using namespace wrap_detail;
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    auto original=make_body_profiles(profile,request.normal);
    std::vector<TopoDS_Wire> wires;std::vector<OwnedEdge> edges;std::vector<OwnedVertex> vertices;
    for(const auto& wire:original) {
        if(wire.edges.size()!=wire.curve_ids.size()||(!wire.point_ids.empty()&&wire.point_ids.size()!=wire.edges.size()))
            throw std::runtime_error("Wrap source ancestry is missing.");
        wires.push_back(wire.wire);
        for(std::size_t i=0;i<wire.edges.size();++i) {
            const auto key=prefix+":curve:"+field(wire.curve_ids[i]);
            const auto a=wire.point_ids.empty()?key+":seam":prefix+":point:"+field(wire.point_ids[i]);
            const auto b=wire.point_ids.empty()?a:prefix+":point:"+field(i+1<wire.point_ids.size()?wire.point_ids[i+1]:
                !profile.open_profile_end_id.empty()?profile.open_profile_end_id:wire.point_ids.front());
            edges.push_back({wire.edges[i],{owner,key,{}}});
            vertices.push_back({TopExp::FirstVertex(wire.edges[i],true),{owner,a,{}}});
            vertices.push_back({TopExp::LastVertex(wire.edges[i],true),{owner,b,{}}});
        }
    }
    const bool curves=request.output==WrappingResult::Curves;
    auto shape=profile_base(wires,curves);
    for(const auto& limit:chart.limits) {
        Bnd_Box bounds;BRepBndLib::Add(shape,bounds);
        if(bounds.IsVoid())return {};
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        double u0=1e100,v0=1e100,u1=-1e100,v1=-1e100;
        for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1}) {
            const auto delta=sub({x,y,z},request.origin);
            const double u=dot(delta,request.x_axis),v=dot(delta,request.y_axis);
            u0=std::min(u0,u);v0=std::min(v0,v);u1=std::max(u1,u);v1=std::max(v1,v);
        }
        const double margin=std::max({1.,u1-u0,v1-v0});u0-=margin;v0-=margin;u1+=margin;v1+=margin;
        using UV=std::pair<double,double>;
        std::vector<UV> rectangle{{u0,v0},{u1,v0},{u1,v1},{u0,v1}},polygon;
        auto previous=rectangle.back();double previous_side=limit.value(previous.first,previous.second);
        for(const auto& point:rectangle) {
            const double side=limit.value(point.first,point.second);
            if((side>=0)!=(previous_side>=0)) {
                const double t=previous_side/(previous_side-side);
                polygon.emplace_back(previous.first+t*(point.first-previous.first),previous.second+t*(point.second-previous.second));
            }
            if(side>=0)polygon.push_back(point);
            previous=point;previous_side=side;
        }
        if(polygon.size()<3)return {};
        const auto world=[&](UV p){const auto point=add(request.origin,add(scale(request.x_axis,p.first),scale(request.y_axis,p.second)));return gp_Pnt(point.x,point.y,point.z);};
        BRepBuilderAPI_MakeWire boundary;std::vector<OwnedEdge> cutting_edges;
        // Shared native vertices keep MakeWire from copying a cutting edge
        // while joining coincident endpoints and severing its authored ancestry.
        std::vector<TopoDS_Vertex> corners;
        for(const auto& point:polygon)corners.push_back(BRepBuilderAPI_MakeVertex(world(point)).Vertex());
        for(std::size_t i=0;i<polygon.size();++i) {
            const auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
            if(world(a).Distance(world(b))<Precision::Confusion())continue;
            const auto edge=BRepBuilderAPI_MakeEdge(corners[i],corners[(i+1)%corners.size()]).Edge();boundary.Add(edge);
            if(std::abs(limit.value(a.first,a.second))<request.tolerance&&std::abs(limit.value(b.first,b.second))<request.tolerance)
                cutting_edges.push_back({edge,{owner,limit.key,{}}});
        }
        if(!boundary.IsDone())throw std::runtime_error("Wrapped region is not valid.");
        const auto mask=BRepBuilderAPI_MakeFace(boundary.Wire()).Face();
        BRepAlgoAPI_Common clip;set_boolean_inputs(clip,shape,mask);clip.SetFuzzyValue(request.tolerance);clip.Build();
        if(!clip.IsDone()||clip.HasErrors())throw std::runtime_error("Wrapped region is not valid.");
        edges=propagate_topology(clip,edges,cutting_edges);
        vertices=propagate_topology(clip,vertices,std::vector<OwnedVertex>{});shape=clip.Shape();
    }
    const auto edge_key=[&](const TopoDS_Edge& edge) {
        std::set<std::string> keys;
        for(const auto& source:edges)if(source.shape.IsSame(edge))keys.insert(source.reference.semantic_key);
        if(keys.empty())throw std::runtime_error("Wrap source ancestry is missing.");
        if(keys.size()==1)return *keys.begin();
        std::string key="wrap:shared";for(const auto& source:keys)key+=":"+field(source);return key;
    };
    const auto vertex_key=[&](const TopoDS_Vertex& vertex) {
        for(const auto& source:vertices)if(source.shape.IsSame(vertex))return source.reference.semantic_key;
        std::set<std::string> parents;
        std::string branch;
        for(TopExp_Explorer it(shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());
            if(!TopExp::FirstVertex(edge).IsSame(vertex)&&!TopExp::LastVertex(edge).IsSame(vertex))continue;
            const auto key=edge_key(edge);parents.insert(key);
            if(key.starts_with(prefix+":curve:")) {
                const BRepAdaptor_Curve curve(edge);gp_Pnt point;gp_Vec tangent;
                curve.D1(BRep_Tool::Parameter(vertex,edge),point,tangent);
                const Vec3 direction{tangent.X(),tangent.Y(),tangent.Z()};
                for(const auto& limit:chart.limits) {
                    const Vec3 p{point.X(),point.Y(),point.Z()};const auto delta=sub(p,request.origin);
                    if(std::abs(limit.value(dot(delta,request.x_axis),dot(delta,request.y_axis)))>request.tolerance)continue;
                    // Curves clipped by a face have no generated seam edge;
                    // the authored chart boundary is still their second parent.
                    parents.insert(limit.key);
                    // Both adjacent charts name the same crossing. The sign is
                    // measured against the canonical (not interior) seam normal.
                    const double canonical=limit.u<0||(limit.u==0&&limit.v<0)?-1.:1.;
                    const double derivative=canonical*(limit.u*dot(direction,request.x_axis)+limit.v*dot(direction,request.y_axis));
                    branch+=":"+field(limit.key)+(derivative<0?":negative":":positive");
                }
            }
        }
        if(parents.size()<2)throw std::runtime_error("Wrap source ancestry is missing.");
        std::string key=prefix+":intersection";for(const auto& parent:parents)key+=":"+field(parent);
        return key+branch;
    };
    const auto describe=[&](const TopoDS_Edge& edge) {
        auto key=edge_key(edge);const auto a=vertex_key(TopExp::FirstVertex(edge,true)),b=vertex_key(TopExp::LastVertex(edge,true));
        if(a.starts_with(prefix+":intersection")||b.starts_with(prefix+":intersection"))
            key+=":between:"+field(std::min(a,b))+":"+field(std::max(a,b));
        return WrapSourceEdge{edge,std::move(key),a,b};
    };
    std::vector<WrapSourceRegion> result;
    if(curves) {
        for(TopExp_Explorer edge(shape,TopAbs_EDGE);edge.More();edge.Next())result.push_back({{describe(TopoDS::Edge(edge.Current()))}});
    } else for(TopExp_Explorer face(shape,TopAbs_FACE);face.More();face.Next()) {
        const auto native=TopoDS::Face(face.Current());const auto outer=BRepTools::OuterWire(native);
        WrapSourceRegion region;
        const auto append=[&](const TopoDS_Wire& wire) {
            WrapSourceWire described;
            for(BRepTools_WireExplorer edge(wire,native);edge.More();edge.Next())described.push_back(describe(edge.Current()));
            region.push_back(std::move(described));
        };
        append(outer);
        for(TopExp_Explorer wire(native,TopAbs_WIRE);wire.More();wire.Next())if(!wire.Current().IsSame(outer))append(TopoDS::Wire(wire.Current()));
        result.push_back(std::move(region));
    }
    return result;
}
