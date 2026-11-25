std::optional<SurfaceGeometry> analytic_surface(const TopoDS_Face& face);

std::vector<FaceReference> continued_native_surfaces(const std::vector<OwnedFace>& current,
        const std::vector<FaceReference>& selected,double tolerance) {
    std::vector<FaceReference> faces;
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> edges;
    std::map<int,std::vector<std::size_t>> incident;
    for(std::size_t i=0;i<current.size();++i) {
        auto face=current[i].reference;
        if(const auto support=analytic_surface(TopoDS::Face(current[i].shape)))
            face.surface=std::make_shared<SurfaceGeometry>(*support);
        faces.push_back(std::move(face));
        for(TopExp_Explorer edge(current[i].shape,TopAbs_EDGE);edge.More();edge.Next())
            incident[edges.Add(edge.Current())].push_back(i);
    }
    std::vector<std::pair<std::size_t,std::size_t>> adjacent;
    for(const auto& [edge,owners]:incident)for(std::size_t i=0;i<owners.size();++i)
        for(std::size_t j=i+1;j<owners.size();++j)if(owners[i]!=owners[j])adjacent.emplace_back(owners[i],owners[j]);
    auto result=selected;
    for(const auto i:surface_continuation_indices(faces,adjacent,selected,tolerance))
        if(std::ranges::find(result,faces[i])==result.end())result.push_back(faces[i]);
    return result;
}
