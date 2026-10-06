// Explicit calculation only. Native parents define names; traversal only
// locates shapes. An ambiguous ancestry is rejected instead of renumbered.
PrimitiveData make_surface_intersection_data(const SurfaceIntersectionRequest& request,const std::string& owner,
    const std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& edges,const std::vector<OwnedVertex>& vertices) {
    if(!std::isfinite(request.tolerance)||request.tolerance<=0||request.faces[0]==request.faces[1])
        throw std::runtime_error("Intersection requires two different original faces and a positive tolerance.");
    std::array<TopoDS_Face,2> selected;
    std::set<std::string> face_parents;
    TopTools_IndexedMapOfShape source_edges,source_vertices;
    for(unsigned side=0;side<2;++side) {
        const auto& reference=request.faces[side];
        if(!reference.valid()||!reference.instance_path.empty())throw std::runtime_error("Intersection face reference is invalid.");
        for(const auto& face:faces)if(same_face_identity(face.reference,reference)) {
            if(!selected[side].IsNull()&&!selected[side].IsSame(face.shape))throw std::runtime_error("Intersection face reference is ambiguous.");
            selected[side]=TopoDS::Face(face.shape);
        }
        if(selected[side].IsNull())throw std::runtime_error("Intersection source face is missing at its input boundary.");
        face_parents.insert(encoded_topology_reference(reference));
        for(TopExp_Explorer it(selected[side],TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());
            // A periodic face's parameter seam does not bound its domain.
            if(!BRep_Tool::IsClosed(edge,selected[side]))source_edges.Add(edge);
        }
        TopExp::MapShapes(selected[side],TopAbs_VERTEX,source_vertices);
    }
    // Intersection is symmetric. Use native parent order for the kernel's
    // parameter direction so exchanging the two input fields cannot exchange
    // persisted start/end references. Retain each source face's orientation.
    if(encoded_topology_reference(request.faces[1])<encoded_topology_reference(request.faces[0]))std::swap(selected[0],selected[1]);
    const auto ancestry=[](const std::set<std::string>& parents) {
        std::string key;for(const auto& parent:parents)key+=std::to_string(parent.size())+":"+parent;return key;
    };
    const auto parent_key=ancestry(face_parents);
    BRepAlgoAPI_Common overlap(selected[0],selected[1]);
    overlap.SetNonDestructive(true);overlap.SetFuzzyValue(request.tolerance);overlap.Build();
    if(!overlap.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
    GProp_GProps area;BRepGProp::SurfaceProperties(overlap.Shape(),area);
    if(std::abs(area.Mass())>request.tolerance*request.tolerance)
        throw std::runtime_error("Coincident surface areas do not define a unique intersection curve.");
    BRepAlgoAPI_Section section(selected[0],selected[1],false);
    section.SetNonDestructive(true);section.SetFuzzyValue(request.tolerance);
    section.Approximation(true);section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);section.Build();
    if(!section.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
    PrimitiveData result;result.shape=section.Shape();
    TopTools_IndexedMapOfShape result_edges,result_vertices,curve_vertices;
    TopExp::MapShapes(result.shape,TopAbs_EDGE,result_edges);TopExp::MapShapes(result.shape,TopAbs_VERTEX,result_vertices);
    for(int i=1;i<=result_edges.Extent();++i)TopExp::MapShapes(result_edges.FindKey(i),TopAbs_VERTEX,curve_vertices);
    const auto endpoint_parents=[&](const TopoDS_Vertex& vertex) {
        const auto point=BRep_Tool::Pnt(vertex);std::set<std::string> parents;
        for(const auto& source:vertices)if(source_vertices.Contains(source.shape)&&
            BRep_Tool::Pnt(TopoDS::Vertex(source.shape)).Distance(point)<=request.tolerance)
            parents.insert(encoded_topology_reference(source.reference));
        for(const auto& source:edges)if(source_edges.Contains(source.shape)) {
            BRepExtrema_DistShapeShape distance(vertex,source.shape);
            if(distance.IsDone()&&distance.Value()<=request.tolerance)parents.insert(encoded_topology_reference(source.reference));
        }
        return parents;
    };
    if(result_edges.Extent()>1) {
        // Section may split one geometric branch at a periodic support seam.
        // Join only shared vertices and the same analytic/C1 spline domain.
        // The endpoints of each completed branch retain native ancestry.
        // No source shape is modified.
#if __has_include(<NCollection_HSequence.hxx>)
        Handle(NCollection_HSequence<TopoDS_Shape>) sequence=new NCollection_HSequence<TopoDS_Shape>;
#else
        Handle(TopTools_HSequenceOfShape) sequence=new TopTools_HSequenceOfShape;
#endif
        for(int i=1;i<=result_edges.Extent();++i)sequence->Append(result_edges.FindKey(i));
#if __has_include(<NCollection_HSequence.hxx>)
        const auto wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence,request.tolerance,true);
#else
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence,request.tolerance,true,wires);
#endif
        BRep_Builder builder;TopoDS_Compound connected;builder.MakeCompound(connected);
        for(int i=1;i<=wires->Length();++i) {
            const auto wire=TopoDS::Wire(wires->Value(i));
            ShapeUpgrade_UnifySameDomain merge(wire,true,false,true);
            merge.SetSafeInputMode(true);merge.SetLinearTolerance(request.tolerance);merge.Build();
            TopTools_IndexedMapOfShape parts;TopExp::MapShapes(merge.Shape(),TopAbs_EDGE,parts);
            if(parts.Extent()==1){builder.Add(connected,merge.Shape());continue;}
            // Fuzzy section splines can have a small derivative mismatch at
            // their support seam. Concatenate their exact bounded geometry
            // with full C0 knot multiplicity; do not fit a smoother curve.
            GeomConvert_CompCurveToBSplineCurve joined;
            for(BRepTools_WireExplorer explorer(wire);explorer.More();explorer.Next()) {
                const auto edge=explorer.Current();double start,end;
                const auto basis=BRep_Tool::Curve(edge,start,end);
                Handle(Geom_TrimmedCurve) bounded=new Geom_TrimmedCurve(basis,start,end);
                if(edge.Orientation()==TopAbs_REVERSED)bounded->Reverse();
                const auto spline=GeomConvert::CurveToBSplineCurve(bounded);
                const auto previous=joined.BSplineCurve();
                const int degree=previous.IsNull()?spline->Degree():std::max(previous->Degree(),spline->Degree());
                if(!joined.Add(spline,request.tolerance,true,true,degree))throw std::runtime_error("Surface intersection calculation failed.");
            }
            TopoDS_Vertex first,last;TopExp::Vertices(wire,first,last);
            const auto joined_curve=joined.BSplineCurve();
            BRepBuilderAPI_MakeEdge joined_edge(joined_curve,first,last,joined_curve->FirstParameter(),joined_curve->LastParameter());
            if(!joined_edge.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
            builder.Add(connected,joined_edge.Edge());
        }
        for(int i=1;i<=result_vertices.Extent();++i)if(!curve_vertices.Contains(result_vertices.FindKey(i)))builder.Add(connected,result_vertices.FindKey(i));
        result.shape=connected;
        result_edges.Clear();result_vertices.Clear();curve_vertices.Clear();
        TopExp::MapShapes(result.shape,TopAbs_EDGE,result_edges);TopExp::MapShapes(result.shape,TopAbs_VERTEX,result_vertices);
        for(int i=1;i<=result_edges.Extent();++i)TopExp::MapShapes(result_edges.FindKey(i),TopAbs_VERTEX,curve_vertices);
    }
    std::map<std::string,TopoDS_Shape> named_vertices,named_edges;
    const auto name_vertex=[&](const TopoDS_Vertex& vertex,bool isolated) {
        const auto existing=std::ranges::find_if(result.vertices,[&](const auto& item){return item.shape.IsSame(vertex);});
        if(existing!=result.vertices.end())return existing->reference.semantic_key;
        const auto parents=endpoint_parents(vertex);
        if(parents.empty()&&!isolated)throw std::runtime_error("Intersection endpoint ancestry is ambiguous.");
        const auto key="intersection:point:from:"+parent_key+(parents.empty()?":contact":":at:"+ancestry(parents));
        if(const auto found=named_vertices.find(key);found!=named_vertices.end()&&!found->second.IsSame(vertex))
            throw std::runtime_error("Intersection endpoint ancestry is ambiguous.");
        named_vertices.emplace(key,vertex);result.vertices.push_back({vertex,{owner,key,{}}});return key;
    };
    for(int i=1;i<=result_edges.Extent();++i) {
        const auto edge=TopoDS::Edge(result_edges.FindKey(i));
        const auto first=TopExp::FirstVertex(edge,true),last=TopExp::LastVertex(edge,true);
        const bool closed=first.IsSame(last)||(BRepAdaptor_Curve(edge).IsClosed()&&
            BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(last))<=request.tolerance);
        std::string role;
        if(closed)role=":closed";
        else role=":at:"+ancestry({name_vertex(first,false),name_vertex(last,false)});
        const auto key="intersection:curve:from:"+parent_key+role;
        if(!named_edges.emplace(key,edge).second)throw std::runtime_error("Intersection branch ancestry is ambiguous.");
        result.edges.push_back({edge,{owner,key,{}}});
    }
    for(int i=1;i<=result_vertices.Extent();++i)if(!curve_vertices.Contains(result_vertices.FindKey(i)))
        static_cast<void>(name_vertex(TopoDS::Vertex(result_vertices.FindKey(i)),true));
    return result;
}
