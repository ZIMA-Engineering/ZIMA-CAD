HistoryContainer create_surface_trim() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::SurfaceTrim;result.name="Oříznout plochu";return result;
}
void validate_surface_trim(const HistoryContainer& feature) {
    const auto& p=feature.surface_trim;
    if(!p.target.valid()||!p.target.instance_path.empty()||p.tools.empty()||!p.seed_valid||
        !std::isfinite(p.seed.x)||!std::isfinite(p.seed.y)||!std::isfinite(p.seed.z))
        throw std::runtime_error("Specify a surface, cutting tools and a retained region.");
    std::set<std::tuple<bool,std::string,std::string>> unique;
    for(const auto& tool:p.tools) {
        if(!tool.reference.valid()||!tool.reference.instance_path.empty()||
            !unique.emplace(tool.face,tool.reference.owner_id,tool.reference.semantic_key).second||
            (tool.face&&tool.reference.owner_id==p.target.owner_id&&tool.reference.semantic_key==p.target.semantic_key))
            throw std::runtime_error("Surface trim tool reference is invalid.");
    }
}
kernel::SurfaceTrimRequest surface_trim_request(const PartDocument& part,const HistoryContainer& feature) {
    validate_surface_trim(feature);
    kernel::SurfaceTrimRequest request;const auto& p=feature.surface_trim;
    request.target=p.target;request.tools=p.tools;request.seed=p.seed;request.expected_region_key=p.retained_region_key;
    request.tolerance=precision_value(part.document_precision,"linear_tolerance",0.001);
    if(!boundary_surface_support_allowed(part,feature.id,p.target))throw std::runtime_error("Trim requires a calculated surface at its input boundary.");
    for(const auto& tool:p.tools) {
        if(tool.face) {
            if(!boundary_surface_support_allowed(part,feature.id,{tool.reference.owner_id,tool.reference.semantic_key,{}}))
                throw std::runtime_error("Surface trim tool is missing at its input boundary.");
        }else {
            BoundaryCurveSource source{tool.reference.owner_id,tool.reference.semantic_key};source.kind=BoundaryCurveSource::Kind::Edge;
            if(!boundary_surface_source_allowed(part,feature.id,source))throw std::runtime_error("Surface trim tool is missing at its input boundary.");
        }
    }
    return request;
}
