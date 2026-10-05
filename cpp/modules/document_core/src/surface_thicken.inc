HistoryContainer create_surface_thicken() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::SurfaceThicken;result.name="Zesílit plochu";return result;
}
void validate_surface_thicken_parameters(const SurfaceThickenParameters& value) {
    if(!value.face.valid()||!value.face.instance_path.empty())
        throw std::invalid_argument("Select a calculated surface to thicken.");
    if(!std::isfinite(value.thickness)||value.thickness<=0.)
        throw std::invalid_argument("Surface thickness must be positive.");
    if(static_cast<unsigned>(value.side)>2)
        throw std::invalid_argument("Invalid surface thickening definition.");
}
kernel::SurfaceThickenRequest surface_thicken_request(const PartDocument& part,const HistoryContainer& feature) {
    const auto& value=feature.surface_thicken;validate_surface_thicken_parameters(value);
    if(!boundary_surface_support_allowed(part,feature.id,value.face))
        throw std::invalid_argument("Select a preceding surface in the active Body.");
    return {value.face,value.thickness,value.side,precision_value(part.document_precision,"linear_tolerance",0.001)};
}
