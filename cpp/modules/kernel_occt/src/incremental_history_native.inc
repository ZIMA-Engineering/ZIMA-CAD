// Native continuation snapshots use the existing archive/binding implementation.
// They never assign identities or recover them from geometric similarity.
BodyResult snapshot_native_history_data(const PrimitiveData& data,
        const std::vector<TopoDS_Shape>* hidden=nullptr,std::vector<std::string>* locators=nullptr) {
    BodyResult result;result.physical_properties_current=false;
    if(!data.shape.IsNull()) {
        result.solid_present=TopExp_Explorer(data.shape,TopAbs_SOLID).More();
        persist_surface_topology(data.shape,data.faces,data.edges,data.vertices,result,hidden,locators);
    }
    result.sheet_cuts=data.sheet_cuts;result.mesh.original_references=data.profile_references;
    return result;
}

PrimitiveData restore_native_history_data(const BodyResult& snapshot,
        const std::vector<std::string>* locators=nullptr,std::vector<TopoDS_Shape>* hidden=nullptr) {
    if(snapshot.kernel_shape.empty())return {};
    auto result=bound_face_geometry(snapshot,true,locators,hidden);
    result.sheet_cuts=snapshot.sheet_cuts;result.profile_references=snapshot.mesh.original_references;
    return result;
}

BodyResult snapshot_native_history_sources(const std::vector<OwnedFace>& faces,
        const std::vector<OwnedEdge>& edges={},const std::vector<OwnedVertex>& vertices={}) {
    if(faces.empty()&&edges.empty()&&vertices.empty())return {};
    PrimitiveData data;TopoDS_Compound compound;BRep_Builder builder;builder.MakeCompound(compound);
    for(const auto& face:faces)builder.Add(compound,face.shape);
    for(const auto& edge:edges)builder.Add(compound,edge.shape);
    for(const auto& vertex:vertices)builder.Add(compound,vertex.shape);
    data.shape=compound;data.faces=faces;data.edges=edges;data.vertices=vertices;
    return snapshot_native_history_data(data);
}

struct HistoryReferenceCounts {
    std::size_t vertices{},triangles{},faces{},edges{},points{},axes{};
    explicit HistoryReferenceCounts(const ViewerReferenceGeometry& geometry):vertices(geometry.vertices.size()),
        triangles(geometry.triangles.size()),faces(geometry.triangle_references.size()),edges(geometry.edges.size()),
        points(geometry.points.size()),axes(geometry.axes.size()){}
};

std::optional<ViewerReferenceGeometry> native_history_reference_delta(
        const ViewerReferenceGeometry& geometry,const HistoryReferenceCounts& before) {
    if(geometry.vertices.size()<before.vertices||geometry.triangles.size()<before.triangles||
       geometry.triangle_references.size()<before.faces||geometry.edges.size()<before.edges||
       geometry.points.size()<before.points||geometry.axes.size()<before.axes)return std::nullopt;
    ViewerReferenceGeometry result;
    result.vertices.assign(geometry.vertices.begin()+before.vertices,geometry.vertices.end());
    result.triangles.assign(geometry.triangles.begin()+before.triangles,geometry.triangles.end());
    for(auto& index:result.triangles) {
        if(index<before.vertices)return std::nullopt;
        index-=static_cast<std::uint32_t>(before.vertices);
    }
    result.triangle_references.assign(geometry.triangle_references.begin()+before.faces,geometry.triangle_references.end());
    result.edges.assign(geometry.edges.begin()+before.edges,geometry.edges.end());
    result.points.assign(geometry.points.begin()+before.points,geometry.points.end());
    result.axes.assign(geometry.axes.begin()+before.axes,geometry.axes.end());
    return result;
}
