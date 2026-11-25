std::vector<WrapChart> native_wrap_charts(const WrappingRequest& request,const std::vector<OwnedFace>& available) {
    using namespace wrap_detail;
    std::vector<FaceReference> selected{request.target};
    selected.insert(selected.end(),request.continuations.begin(),request.continuations.end());
    std::vector<WrapChart> charts;
    const auto shape=[&](const FaceReference& reference) {
        TopoDS_Face result;
        for(const auto& face:available)if(same_face_identity(face.reference,reference)) {
            if(!result.IsNull()&&!result.IsSame(face.shape))throw std::runtime_error("Wrap target is ambiguous.");
            result=TopoDS::Face(face.shape);
        }
        if(result.IsNull())throw std::runtime_error("Wrap target is unavailable.");
        return result;
    };
    const auto contains=[&](const std::vector<FaceReference>& coverage,Vec3 point) {
        return std::ranges::any_of(coverage,[&](const auto& reference) {
            const auto state=BRepClass_FaceClassifier(shape(reference),gp_Pnt(point.x,point.y,point.z),request.tolerance).State();
            return state==TopAbs_IN||state==TopAbs_ON;
        });
    };
    for(const auto& reference:selected) {
        const auto target=shape(reference);
        if(std::ranges::any_of(charts,[&](const auto& chart){return std::ranges::find(chart.coverage,reference)!=chart.coverage.end();}))continue;
        const auto surface=analytic_surface(target);
        if(!surface)throw std::runtime_error("Select a cylindrical, conical, spherical or planar face to wrap.");
        auto coverage=continued_native_surfaces(available,{reference},request.tolerance);
        if(charts.empty()) {
            charts.push_back({*surface,reference,std::move(coverage),SurfaceWrap(*surface,request.origin,request.x_axis,request.y_axis,request.reverse_u,request.reverse_v),{}});
            continue;
        }
        auto& previous=charts.back();
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> old_edges;
        for(const auto& face:previous.coverage)TopExp::MapShapes(shape(face),TopAbs_EDGE,old_edges);
        std::optional<WrapJunction> join;
        for(const auto& face:coverage)for(TopExp_Explorer edge(shape(face),TopAbs_EDGE);edge.More();edge.Next()) {
            if(!old_edges.Contains(edge.Current()))continue;
            const BRepAdaptor_Curve curve(TopoDS::Edge(edge.Current()));
            if(curve.GetType()!=GeomAbs_Line)throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
            const auto point=curve.Value((curve.FirstParameter()+curve.LastParameter())/2);const auto direction=curve.Line().Direction();
            WrapJunction candidate{{point.X(),point.Y(),point.Z()},{direction.X(),direction.Y(),direction.Z()},{},
                "wrap:join:"+encoded_topology_reference(previous.anchor)+":"+encoded_topology_reference(reference)};
            if(join) {
                if(length(cross(sub(candidate.point,join->point),join->direction))>request.tolerance||
                        std::abs(dot(candidate.direction,join->direction))<1-1e-10)
                    throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
            } else join=std::move(candidate);
        }
        if(!join)throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
        const auto [u,v]=previous.map.source_coordinates(join->point);
        const auto across=unit(cross(previous.map.normal(u,v),join->direction));
        bool found=false;
        for(double step:{100*request.tolerance,10*request.tolerance,request.tolerance*2}) {
            const auto sample=[&](double sign) {
                const auto uv=previous.map.source_coordinates(add(join->point,scale(across,step*sign)),u,v);
                return previous.map.point(uv.first,uv.second);
            };
            const auto a=sample(1),b=sample(-1);const bool first=contains(previous.coverage,a),second=contains(previous.coverage,b);
            if(first!=second){join->previous_interior=first?a:b;found=true;break;}
        }
        if(!found)throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
        auto next=continue_wrap_chart(previous,*surface,reference,std::move(coverage),*join,request.tolerance);
        const auto boundary=next.limits.front();
        const double step=100*request.tolerance;
        if(!contains(next.coverage,next.map.point(u+boundary.u*step,v+boundary.v*step)))
            throw std::runtime_error("Wrap continuation requires connected cylindrical or planar faces with a straight common boundary.");
        charts.push_back(std::move(next));
    }
    return charts;
}
