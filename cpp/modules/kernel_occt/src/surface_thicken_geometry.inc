// Normal offset calculation is confined to explicit OK / regeneration. All
// children are defined by a native parent and a role before history locates them.
struct ThickenedSurfaceData {PrimitiveData operand,remaining;};
ThickenedSurfaceData make_thickened_surface_data(const SurfaceThickenRequest& request,const std::string& owner,
    const TopoDS_Shape& input_shape,const std::vector<OwnedFace>& input_faces,
    const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices) {
    if(!request.face.valid()||!request.face.instance_path.empty()||input_shape.IsNull())
        throw std::invalid_argument("Select a calculated surface to thicken.");
    if(!std::isfinite(request.thickness)||request.thickness<=0.)
        throw std::invalid_argument("Surface thickness must be positive.");
    if(static_cast<unsigned>(request.side)>2||!std::isfinite(request.tolerance)||request.tolerance<=0.)
        throw std::invalid_argument("Invalid surface thickening definition.");
    std::optional<OwnedFace> selected;
    for(const auto& face:input_faces)if(same_face_identity(face.reference,request.face)) {
        if(selected&&!selected->shape.IsSame(face.shape))
            throw std::runtime_error("Surface thickening reference is ambiguous.");
        selected=face;
    }
    if(!selected||!selected->reference.surface_result)
        throw std::runtime_error("Select a calculated surface to thicken.");
    TopTools_IndexedMapOfShape actual_faces,source_edges,source_vertices;
    TopExp::MapShapes(input_shape,TopAbs_FACE,actual_faces);
    if(!actual_faces.Contains(selected->shape))throw std::runtime_error("Select a calculated surface to thicken.");
    for(TopExp_Explorer solid(input_shape,TopAbs_SOLID);solid.More();solid.Next())
        for(TopExp_Explorer face(solid.Current(),TopAbs_FACE);face.More();face.Next())
            if(face.Current().IsSame(selected->shape))throw std::runtime_error("Select a calculated surface to thicken.");
    TopExp::MapShapes(selected->shape,TopAbs_EDGE,source_edges);
    TopExp::MapShapes(selected->shape,TopAbs_VERTEX,source_vertices);
    BRepBuilderAPI_Copy copy(selected->shape,true,false);
    const auto copied=copy.Shape();
    BRepOffsetAPI_MakeOffsetShape offset;
    TopoDS_Shape start=copied;
    BRepOffsetAPI_MakeThickSolid thick;
    try {
        if(request.side==SurfaceThicknessSide::Symmetric) {
            offset.PerformBySimple(copied,-request.thickness*.5);
            if(!offset.IsDone()||offset.Shape().IsNull())throw std::runtime_error("Surface thickening could not create a valid solid.");
            start=offset.Shape();
        }
        thick.MakeThickSolidBySimple(start,request.side==SurfaceThicknessSide::Second?-request.thickness:request.thickness);
    } catch(const Standard_Failure&) {
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    }
    if(!thick.IsDone()||thick.Shape().IsNull())throw std::runtime_error("Surface thickening could not create a valid solid.");
    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(thick.Shape(),TopAbs_SOLID,solids);
    if(solids.Extent()!=1)throw std::runtime_error("Surface thickening could not create a valid solid.");
    auto solid=TopoDS::Solid(solids.FindKey(1));
    // Simple offset may return an inward-oriented shell even for a positive
    // offset. Orienting the closed solid fixes volume without changing its side.
    if(!BRepLib::OrientClosedSolid(solid))
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    // OrientClosedSolid may reverse the outer SOLID itself. Fillet then carries
    // that reversed wrapper into its new shell and rejects a valid result. Fold
    // the cumulative child orientations into a forward solid wrapper, keeping
    // the same exact shells, faces, edges and points (and therefore ancestry).
    BRep_Builder solid_builder;TopoDS_Solid forward_solid;solid_builder.MakeSolid(forward_solid);
    for(TopoDS_Iterator child(solid);child.More();child.Next())solid_builder.Add(forward_solid,child.Value());
    solid=forward_solid;
    if(!BRepCheck_Analyzer(solid,true,false,true).IsValid())
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    if(!BRepAlgoAPI_Check(solid,false,true).IsValid())
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    GProp_GProps volume;BRepGProp::VolumeProperties(solid,volume);
    if(!std::isfinite(volume.Mass())||volume.Mass()<=0.)
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    TopTools_IndexedMapOfShape result_faces,result_edges,result_vertices;
    TopExp::MapShapes(solid,TopAbs_FACE,result_faces);TopExp::MapShapes(solid,TopAbs_EDGE,result_edges);TopExp::MapShapes(solid,TopAbs_VERTEX,result_vertices);
    const auto generated=[&](auto& algorithm,const TopoDS_Shape& source,TopAbs_ShapeEnum type) {
        TopTools_IndexedMapOfShape matches;
        for(const auto& value:algorithm.Generated(source))if(value.ShapeType()==type)matches.Add(value);
        for(const auto& value:algorithm.Modified(source))if(value.ShapeType()==type)matches.Add(value);
        if(matches.Extent()!=1)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        return matches.FindKey(1);
    };
    const auto start_shape=[&](const TopoDS_Shape& source) {
        const auto shape=copy.ModifiedShape(source);
        return request.side==SurfaceThicknessSide::Symmetric?generated(offset,shape,source.ShapeType()):shape;
    };
    PrimitiveData result;result.shape=solid;
    const auto key=[&](std::string_view role,const auto& parent) {
        return "thicken:"+std::string(role)+":from:"+encoded_topology_reference(parent);
    };
    const auto cap_start=start_shape(selected->shape);const auto cap_end=generated(thick,cap_start,TopAbs_FACE);
    if(!result_faces.Contains(cap_start)||!result_faces.Contains(cap_end))
        throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
    result.faces.push_back({result_faces.FindKey(result_faces.FindIndex(cap_start)),{owner,key("cap:start",request.face),{}}});
    result.faces.push_back({result_faces.FindKey(result_faces.FindIndex(cap_end)),{owner,key("cap:end",request.face),{}}});
    struct Rim {TopoDS_Shape start,end;EdgeReference parent;};
    struct Endpoint {TopoDS_Shape start,end;VertexReference parent;};
    std::vector<Rim> rims;std::vector<Endpoint> endpoints;
    const auto artifacts=shell_parameterization_artifacts(selected->shape);
    for(const auto& edge:input_edges)if(source_edges.Contains(edge.shape)&&!artifacts.Contains(edge.shape)) {
        const auto a=start_shape(edge.shape),b=generated(thick,a,TopAbs_EDGE);
        if(!result_edges.Contains(a)||!result_edges.Contains(b))throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        if(std::ranges::any_of(rims,[&](const auto& item){return item.start.IsSame(a)&&item.parent==edge.reference;}))continue;
        rims.push_back({a,b,edge.reference});
        result.edges.push_back({a,{owner,key("rim:start",edge.reference),{}}});
        result.edges.push_back({b,{owner,key("rim:end",edge.reference),{}}});
    }
    for(const auto& vertex:input_vertices)if(source_vertices.Contains(vertex.shape)&&!artifacts.Contains(vertex.shape)) {
        const auto a=start_shape(vertex.shape),b=generated(thick,a,TopAbs_VERTEX);
        if(!result_vertices.Contains(a)||!result_vertices.Contains(b))throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        if(std::ranges::any_of(endpoints,[&](const auto& item){return item.start.IsSame(a)&&item.parent==vertex.reference;}))continue;
        endpoints.push_back({a,b,vertex.reference});
        result.vertices.push_back({a,{owner,key("point:start",vertex.reference),{}}});
        result.vertices.push_back({b,{owner,key("point:end",vertex.reference),{}}});
    }
    for(int i=1;i<=result_faces.Extent();++i) {
        const auto& face=result_faces.FindKey(i);if(face.IsSame(cap_start)||face.IsSame(cap_end))continue;
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(face,TopAbs_EDGE,edges);
        std::optional<EdgeReference> parent;
        for(const auto& rim:rims)if(edges.Contains(rim.start)&&edges.Contains(rim.end)) {
            if(parent&&*parent!=rim.parent)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
            parent=rim.parent;
        }
        if(!parent)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        result.faces.push_back({face,{owner,key("wall",*parent),{}}});
    }
    const auto result_artifacts=shell_parameterization_artifacts(solid);
    for(int i=1;i<=result_edges.Extent();++i) {
        const auto& edge=result_edges.FindKey(i);
        if(result_artifacts.Contains(edge)||std::ranges::any_of(result.edges,[&](const auto& item){return item.shape.IsSame(edge);}))continue;
        TopTools_IndexedMapOfShape vertices;TopExp::MapShapes(edge,TopAbs_VERTEX,vertices);
        std::optional<VertexReference> parent;
        for(const auto& point:endpoints)if(vertices.Contains(point.start)&&vertices.Contains(point.end)) {
            if(parent&&*parent!=point.parent)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
            parent=point.parent;
        }
        if(!parent)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        result.edges.push_back({edge,{owner,key("longitudinal",*parent),{}}});
    }
    require_complete_unique_shell_topology(solid,TopAbs_FACE,result.faces,"faces");
    require_complete_unique_shell_topology(solid,TopAbs_EDGE,result.edges,"edges");
    require_complete_unique_shell_topology(solid,TopAbs_VERTEX,result.vertices,"vertices");
    // Consume only the selected free face. Return the solid as an ordinary Add
    // operand: the existing Boolean path handles contact with preceding solids.
    auto operand=std::move(result);PrimitiveData remaining;
    BRep_Builder builder;TopoDS_Compound combined;builder.MakeCompound(combined);
    const auto retain=[&](auto&& self,const TopoDS_Shape& shape)->void {
        if(shape.ShapeType()==TopAbs_COMPOUND){for(TopoDS_Iterator it(shape);it.More();it.Next())self(self,it.Value());return;}
        bool affected=shape.IsSame(selected->shape);
        if(!affected)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())if(it.Current().IsSame(selected->shape)){affected=true;break;}
        if(!affected){builder.Add(combined,shape);return;}
        if(shape.ShapeType()!=TopAbs_FACE)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
            if(!it.Current().IsSame(selected->shape))builder.Add(combined,it.Current());
    };
    retain(retain,input_shape);if(TopoDS_Iterator(combined).More())remaining.shape=combined;
    TopTools_IndexedMapOfShape retained_edges,retained_vertices;
    for(const auto& face:input_faces)if(!face.shape.IsSame(selected->shape)) {
        remaining.faces.push_back(face);TopExp::MapShapes(face.shape,TopAbs_EDGE,retained_edges);TopExp::MapShapes(face.shape,TopAbs_VERTEX,retained_vertices);
    }
    for(const auto& edge:input_edges)if(retained_edges.Contains(edge.shape))remaining.edges.push_back(edge);
    for(const auto& vertex:input_vertices)if(retained_vertices.Contains(vertex.shape))remaining.vertices.push_back(vertex);
    return {std::move(operand),std::move(remaining)};
}
