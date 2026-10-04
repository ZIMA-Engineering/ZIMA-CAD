HistoryContainer create_surface_intersection() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::SurfaceIntersection;result.name="Průsečík ploch";return result;
}
kernel::SurfaceIntersectionRequest surface_intersection_request(const PartDocument& part,const HistoryContainer& feature) {
    kernel::SurfaceIntersectionRequest request;request.faces=feature.surface_intersection.faces;
    request.tolerance=precision_value(part.document_precision,"linear_tolerance",0.001);
    if(request.faces[0]==request.faces[1])throw std::runtime_error("Intersection requires two different original faces and a positive tolerance.");
    for(const auto& face:request.faces)if(!boundary_surface_support_allowed(part,feature.id,face))
        throw std::runtime_error("Intersection source face is missing at its input boundary.");
    return request;
}
