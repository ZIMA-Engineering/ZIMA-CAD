// Normal offset calculation is confined to explicit OK / regeneration. All
// children are defined by a native parent and a role before history locates them.
struct ThickenedSurfaceData {PrimitiveData operand,remaining;};
// OCCT's joined offset already supports coherent shell thickening. Expose its
// thickening mode through the same history interface used for single faces.
class SurfaceShellThickener:public BRepOffsetAPI_MakeThickSolid {
public:
    bool ParametricC0Failure()const {return myOffsetShape.Error()==BRepOffset_C0Geometry;}
    void PerformShell(const TopoDS_Shape& source,double thickness,double tolerance) {
        NotDone();myLastUsedAlgo=OffsetAlgo_JOIN;
        myOffsetShape.Initialize(source,thickness,tolerance,BRepOffset_Skin,false,false,
            GeomAbs_Intersection,true,false);
        myOffsetShape.MakeOffsetShape();
        if(!myOffsetShape.IsDone())return;
        myShape=myOffsetShape.Shape();Done();
    }
};
ThickenedSurfaceData make_thickened_surface_data(const SurfaceThickenRequest& request,const std::string& owner,
    const TopoDS_Shape& input_shape,const std::vector<OwnedFace>& input_faces,
    const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices,
    bool symbolic_form=false) {
    auto profile_start=std::chrono::steady_clock::now();
    const auto profile=[&](const char* phase) {
        const auto now=std::chrono::steady_clock::now();
        if(owner=="FORM_THICKNESS"&&std::getenv("ZIMA_CPP_FORM_PROFILE"))
            std::fprintf(stderr,"FORM thickness %s: %.6f s\n",phase,std::chrono::duration<double>(now-profile_start).count());
        profile_start=now;
    };
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
    TopoDS_Shape source=selected->shape;
    for(TopExp_Explorer shell(input_shape,TopAbs_SHELL);shell.More();shell.Next()) {
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(shell.Current(),TopAbs_FACE,members);
        if(members.Contains(selected->shape)){source=shell.Current();break;}
    }
    TopTools_IndexedMapOfShape source_faces;
    TopExp::MapShapes(source,TopAbs_FACE,source_faces);
    TopExp::MapShapes(source,TopAbs_EDGE,source_edges);
    TopExp::MapShapes(source,TopAbs_VERTEX,source_vertices);
    BRepBuilderAPI_Copy copy(source,true,false);
    const auto copied=copy.Shape();
    BRepOffsetAPI_MakeOffsetShape offset;
    TopoDS_Shape start=copied;
    SurfaceShellThickener thick;
    profile("source copy");
    try {
        if(request.side==SurfaceThicknessSide::Symmetric) {
            if(source.ShapeType()==TopAbs_SHELL) {
                offset.PerformByJoin(copied,-request.thickness*.5,request.tolerance,
                    BRepOffset_Skin,false,false,GeomAbs_Intersection,false);
                if(!offset.IsDone()&&offset.MakeOffset().Error()==BRepOffset_C0Geometry)
                    offset.PerformBySimple(copied,-request.thickness*.5);
            } else offset.PerformBySimple(copied,-request.thickness*.5);
            if(!offset.IsDone()||offset.Shape().IsNull())throw std::runtime_error("Surface thickening could not create a valid solid.");
            start=offset.Shape();
        }
        const auto thickness=request.side==SurfaceThicknessSide::Second?-request.thickness:request.thickness;
        if(source.ShapeType()==TopAbs_SHELL) {
            thick.PerformShell(start,thickness,request.tolerance);
            // Joined offsets reject a C0 spline parameterization even when
            // the bounded shell is geometrically smooth. OCCT's simple shell
            // offset preserves those exact source surfaces; accept it only
            // through the same solid validity, intersection and ancestry gates.
            if(!thick.IsDone()&&thick.ParametricC0Failure())
                thick.MakeThickSolidBySimple(start,thickness);
        }
        else thick.MakeThickSolidBySimple(start,thickness);
    } catch(const Standard_Failure&) {
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    }
    if(!thick.IsDone()||thick.Shape().IsNull())throw std::runtime_error("Surface thickening could not create a valid solid.");
    profile("offset");
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
    profile("solid validity");
    // FORM is an explicitly idealized manufacturing indication, not a forming
    // simulation. Keep the exact normal offset and structural/ancestry gates;
    // the expensive all-pairs interference certification belongs to the
    // ordinary Surface Thicken command, whose default policy remains strict.
    if(!symbolic_form&&!BRepAlgoAPI_Check(solid,false,true).IsValid())
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    profile("self interference");
    GProp_GProps volume;BRepGProp::VolumeProperties(solid,volume);
    if(!std::isfinite(volume.Mass())||volume.Mass()<=0.)
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    profile("positive volume");
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
    TopTools_IndexedMapOfShape caps;
    for(const auto& face:input_faces)if(source_faces.Contains(face.shape)) {
        const auto cap_start=start_shape(face.shape),cap_end=generated(thick,cap_start,TopAbs_FACE);
        if(!result_faces.Contains(cap_start)||!result_faces.Contains(cap_end))
            throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        caps.Add(cap_start);caps.Add(cap_end);
        result.faces.push_back({result_faces.FindKey(result_faces.FindIndex(cap_start)),{owner,key("cap:start",face.reference),{}}});
        result.faces.push_back({result_faces.FindKey(result_faces.FindIndex(cap_end)),{owner,key("cap:end",face.reference),{}}});
    }
    struct Rim {TopoDS_Shape start,end;EdgeReference parent;};
    struct Endpoint {TopoDS_Shape start,end;VertexReference parent;};
    std::vector<Rim> rims;std::vector<Endpoint> endpoints;
    const auto artifacts=shell_parameterization_artifacts(source);
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
        const auto& face=result_faces.FindKey(i);if(caps.Contains(face))continue;
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
        std::optional<Endpoint> endpoint;
        std::set<std::string> parents;
        for(const auto& point:endpoints)if(vertices.Contains(point.start)&&vertices.Contains(point.end)) {
            // A zero-length sweep endpoint can carry both cap identities.
            // They are aliases only when OCCT history locates exactly the
            // same source and offset vertices; proximity is not sufficient.
            if(endpoint&&(!endpoint->start.IsSame(point.start)||!endpoint->end.IsSame(point.end)))
                throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
            endpoint=point;parents.insert(encoded_topology_reference(point.parent));
        }
        if(!endpoint)throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
        auto identity=key("longitudinal",endpoint->parent);
        if(parents.size()>1)identity="thicken:longitudinal:from:"+encoded_topology_ancestry(parents)+
            ":parents:"+encoded_topology_reference_set(parents);
        result.edges.push_back({edge,{owner,std::move(identity),{}}});
    }
    require_complete_unique_shell_topology(solid,TopAbs_FACE,result.faces,"faces");
    require_complete_unique_shell_topology(solid,TopAbs_EDGE,result.edges,"edges");
    require_complete_unique_shell_topology(solid,TopAbs_VERTEX,result.vertices,"vertices");
    // Consume the selected connected shell (or lone face), preserving other
    // sheets and solids. Return the solid as an ordinary Add
    // operand: the existing Boolean path handles contact with preceding solids.
    auto operand=std::move(result);PrimitiveData remaining;
    BRep_Builder builder;TopoDS_Compound combined;builder.MakeCompound(combined);
    const auto retain=[&](auto&& self,const TopoDS_Shape& shape)->void {
        if(shape.ShapeType()==TopAbs_COMPOUND){for(TopoDS_Iterator it(shape);it.More();it.Next())self(self,it.Value());return;}
        bool affected=source_faces.Contains(shape);
        if(!affected)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())if(source_faces.Contains(it.Current())){affected=true;break;}
        if(!affected){builder.Add(combined,shape);return;}
        if(shape.ShapeType()!=TopAbs_FACE)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
            if(!source_faces.Contains(it.Current()))builder.Add(combined,it.Current());
    };
    retain(retain,input_shape);if(TopoDS_Iterator(combined).More())remaining.shape=combined;
    TopTools_IndexedMapOfShape retained_edges,retained_vertices;
    for(const auto& face:input_faces)if(!source_faces.Contains(face.shape)) {
        remaining.faces.push_back(face);TopExp::MapShapes(face.shape,TopAbs_EDGE,retained_edges);TopExp::MapShapes(face.shape,TopAbs_VERTEX,retained_vertices);
    }
    for(const auto& edge:input_edges)if(retained_edges.Contains(edge.shape))remaining.edges.push_back(edge);
    for(const auto& vertex:input_vertices)if(retained_vertices.Contains(vertex.shape))remaining.vertices.push_back(vertex);
    return {std::move(operand),std::move(remaining)};
}
