// These fallbacks run only after the ordinary offset route fails. They retain
// native ancestry and exact offset surfaces; no viewer mesh enters calculation.
struct LoftOffsetHistory {
    TopoDS_Shape shape;
    Handle(BRepTools_History) history;
    const TopoDS_Shape& Shape() const { return shape; }
    const auto& Modified(const TopoDS_Shape& source) const { return history->Modified(source); }
    const auto& Generated(const TopoDS_Shape& source) const { return history->Generated(source); }
    bool IsDeleted(const TopoDS_Shape& source) const { return history->IsRemoved(source); }
};

std::optional<std::array<TopoDS_Face,2>> loft_offset_caps(
    const TopoDS_Shape& shape, const std::vector<OwnedFace>& faces) {
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> members, caps;
    TopExp::MapShapes(shape,TopAbs_FACE,members);
    for (const auto& face:faces)
        if (face.reference.semantic_key.starts_with("loft:cap:") && members.Contains(face.shape))
            caps.Add(face.shape);
    if (caps.Extent()!=2) return std::nullopt;
    return std::array{TopoDS::Face(caps(1)),TopoDS::Face(caps(2))};
}

class LoftFixedCapOffset:public BRepOffsetAPI_MakeOffsetShape {
public:
    void Perform(const TopoDS_Shape& source,const std::array<TopoDS_Face,2>& caps,
        double distance,double tolerance) {
        NotDone(); myLastUsedAlgo=OffsetAlgo_JOIN;
        myOffsetShape.Initialize(source,distance,tolerance,BRepOffset_Skin,false,false,
            GeomAbs_Intersection,false,false);
        for (const auto& cap:caps) myOffsetShape.SetOffsetOnFace(cap,0.);
        myOffsetShape.MakeOffsetShape();
        if (myOffsetShape.IsDone()) { myShape=myOffsetShape.Shape(); Done(); }
    }
};

std::optional<LoftOffsetHistory> loft_fixed_cap_offset(const PrimitiveData& carrier,
    const std::array<TopoDS_Face,2>& caps,double distance,double tolerance) {
    if (distance==0.) return LoftOffsetHistory{carrier.shape,new BRepTools_History};
    // Copy first: an unsuccessful OCCT attempt must not mutate the carrier.
    try {
        BRepBuilderAPI_Copy copy(carrier.shape,true,false);
        const std::array copied_caps{TopoDS::Face(copy.ModifiedShape(caps[0])),
            TopoDS::Face(copy.ModifiedShape(caps[1]))};
        LoftFixedCapOffset offset;
        offset.Perform(copy.Shape(),copied_caps,distance,tolerance);
        if (offset.IsDone()&&!offset.Shape().IsNull()&&
            BRepCheck_Analyzer(offset.Shape(),true,false,true).IsValid()&&
            BRepAlgoAPI_Check(offset.Shape(),false,true).IsValid()) {
            NCollection_List<TopoDS_Shape> source,copied;
            source.Append(carrier.shape); copied.Append(copy.Shape());
            Handle(BRepTools_History) history=new BRepTools_History(source,copy);
            history->Merge(new BRepTools_History(copied,offset));
            return LoftOffsetHistory{offset.Shape(),history};
        }
    } catch (const Standard_Failure&) {}
    if (auto result=detail::sectioned_normal_offset(carrier.shape,caps,distance,{0.,0.},tolerance))
        return LoftOffsetHistory{result->shape,result->history};
    return std::nullopt;
}

PrimitiveData make_closed_thin_loft(const PrimitiveData& carrier,
    const SurfaceThickenRequest& request,const std::string& owner) {
    const auto fail=[] { throw std::runtime_error("Surface thickening could not create a valid solid."); };
    const auto caps=loft_offset_caps(carrier.shape,carrier.faces);
    if (!caps) fail();
    const double lower=request.side==SurfaceThicknessSide::First?0.:
        request.side==SurfaceThicknessSide::Second?-request.thickness:-request.thickness*.5;
    const double upper=request.side==SurfaceThicknessSide::Second?0.:
        request.side==SurfaceThicknessSide::First?request.thickness:request.thickness*.5;
    auto inside=loft_fixed_cap_offset(carrier,*caps,lower,request.tolerance);
    auto outside=loft_fixed_cap_offset(carrier,*caps,upper,request.tolerance);
    if (!inside||!outside) fail();
    const auto layers=[&](LoftOffsetHistory& offset,std::string_view end) {
        PrimitiveData layer; layer.shape=offset.Shape();
        NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> members;
        TopExp::MapShapes(layer.shape,members);
        const auto descendant=[&](const TopoDS_Shape& source) {
            NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> found;
            if (members.Contains(source)) found.Add(source);
            for (const auto* children:{&offset.Modified(source),&offset.Generated(source)})
                for (const auto& child:*children)
                    if (child.ShapeType()==source.ShapeType()&&members.Contains(child)) found.Add(child);
            if (found.Extent()!=1)
                throw std::runtime_error("Surface thickening could not resolve native topology ancestry.");
            return members(members.FindIndex(found(1)));
        };
        const auto key=[&](std::string_view role,const auto& reference) {
            return "thicken:"+std::string(role)+":"+std::string(end)+":from:"+encoded_topology_reference(reference);
        };
        for (const auto& face:carrier.faces) {
            std::string identity;
            if (face.reference.semantic_key.starts_with("loft:cap:")) {
                NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> edges;
                TopExp::MapShapes(face.shape,TopAbs_EDGE,edges);
                std::set<std::string> parents;
                for (const auto& edge:carrier.edges) if (edges.Contains(edge.shape))
                    parents.insert(encoded_topology_reference(edge.reference));
                if (parents.empty()) fail();
                identity="thicken:wall:from:"+encoded_topology_ancestry(parents)+
                    ":parents:"+encoded_topology_reference_set(parents);
            } else {
                auto parent=face.reference; parent.surface_result=true;
                identity=key("cap",parent);
            }
            layer.faces.push_back({descendant(face.shape),{owner,std::move(identity),{}}});
        }
        for (const auto& edge:carrier.edges)
            layer.edges.push_back({descendant(edge.shape),{owner,key("rim",edge.reference),{}}});
        for (const auto& vertex:carrier.vertices)
            layer.vertices.push_back({descendant(vertex.shape),{owner,key("point",vertex.reference),{}}});
        return layer;
    };
    auto inner=layers(*inside,request.side==SurfaceThicknessSide::Second?"end":"start");
    auto outer=layers(*outside,request.side==SurfaceThicknessSide::Second?"start":"end");
    BRepAlgoAPI_Cut cut;
    set_boolean_inputs(cut,outer.shape,inner.shape); cut.SetToFillHistory(true);
    cut.SetFuzzyValue(request.tolerance); cut.Build();
    if (!cut.IsDone()||cut.Shape().IsNull()||!BRepCheck_Analyzer(cut.Shape(),true,false,true).IsValid()||
        !BRepAlgoAPI_Check(cut.Shape(),false,true).IsValid()) fail();
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> solids;
    TopExp::MapShapes(cut.Shape(),TopAbs_SOLID,solids);
    GProp_GProps volume; BRepGProp::VolumeProperties(cut.Shape(),volume);
    if (solids.Extent()!=1||!std::isfinite(volume.Mass())||volume.Mass()<=0.) fail();
    auto faces=outer.faces; faces.insert(faces.end(),inner.faces.begin(),inner.faces.end());
    auto edges=outer.edges; edges.insert(edges.end(),inner.edges.begin(),inner.edges.end());
    auto vertices=outer.vertices; vertices.insert(vertices.end(),inner.vertices.begin(),inner.vertices.end());
    PrimitiveData result; result.shape=cut.Shape();
    result.faces=shell_result_faces(cut,result.shape,faces,edges,vertices,owner,"thicken:wall-face");
    result.edges=complete_shell_edges(result.shape,result.faces,
        keep_unambiguous_topology_references(propagate_topology(cut,outer.edges,inner.edges)),owner,"thicken");
    result.vertices=complete_shell_vertices(result.shape,result.edges,
        keep_unambiguous_topology_references(propagate_topology(cut,outer.vertices,inner.vertices)),{},owner,"thicken:vertex");
    require_complete_unique_shell_topology(result.shape,TopAbs_FACE,result.faces,"faces");
    require_complete_unique_shell_topology(result.shape,TopAbs_EDGE,result.edges,"edges");
    require_complete_unique_shell_topology(result.shape,TopAbs_VERTEX,result.vertices,"vertices");
    return result;
}
