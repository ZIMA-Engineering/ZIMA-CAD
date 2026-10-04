HistoryContainer create_surface_sewing() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::SurfaceSewing;result.name="Sešít plochy";return result;
}
kernel::SurfaceSewingRequest surface_sewing_request(const PartDocument& part,const HistoryContainer& feature) {
    kernel::SurfaceSewingRequest request;request.faces=feature.surface_sewing.faces;
    request.tolerance=precision_value(part.document_precision,"linear_tolerance",0.001);
    if(request.faces.size()<2)throw std::runtime_error("Sewing requires at least two surfaces and a positive tolerance.");
    for(const auto& face:request.faces)if(!boundary_surface_support_allowed(part,feature.id,face))
        throw std::runtime_error("Sewing requires calculated surfaces at its input boundary.");
    return request;
}
