// Exact ordinary 2D Sketch adapters; no OCCT calculation or document mutation.
HistoryContainer create_loft() {
    auto feature=PartDocument::create_sketch_container();
    feature.feature_kind=FeatureKind::Loft;feature.name="Přechod / Loft";
    for(unsigned i=0;i<2;++i) {
        auto sketch=create_general_surface_sketch(feature);feature.loft.profiles.push_back(sketch.sketch_feature->id);
        feature.loft.sketches.push_back(std::move(sketch));
    }
    resolve_loft_sketches(feature,{});return feature;
}
void validate_loft(const HistoryContainer& feature) {
    const auto& p=feature.loft;
    static_cast<void>(feature.sweep_precision.effective());
    if(feature.feature_kind!=FeatureKind::Loft||p.profiles.size()<2||p.guides.size()==1)
        throw std::invalid_argument("Loft requires at least two profiles and either no guides or at least two guides.");
    if(static_cast<unsigned>(p.result_type)>static_cast<unsigned>(ProfileResultType::Surface)||
       (p.thin_mode!=ThinMode::OneSide&&p.thin_mode!=ThinMode::OtherSide&&p.thin_mode!=ThinMode::Symmetric)||
       !std::isfinite(p.thickness)||p.thickness<=0.)
        throw std::invalid_argument("Invalid Loft result or thickness.");
    if(p.result_type==ProfileResultType::Surface&&feature.combine_mode!=CombineMode::Add)
        throw std::invalid_argument("A surface Loft cannot subtract material.");
    auto carrier=loft_sketch_editor_carrier(feature);validate_general_surface(carrier);
    if(p.sketches.size()!=p.profiles.size()+p.guides.size())throw std::invalid_argument("Loft source Sketch is unavailable.");
    for(const auto& value:p.sketches)if(value.curve)throw std::invalid_argument("Loft source Sketch is unavailable.");
    std::set<std::string> unique;
    for(const auto* family:{&p.profiles,&p.guides})for(const auto& id:*family)
        if(id.empty()||!unique.insert(id).second||std::ranges::none_of(p.sketches,[&](const auto& value){return value.sketch_feature&&value.sketch_feature->id==id;}))
            throw std::invalid_argument("Select different profile and guide Sketches for Loft.");
}
HistoryContainer loft_sketch_editor_carrier(const HistoryContainer& feature) {
    auto carrier=feature;carrier.feature_kind=FeatureKind::GeneralSurface;carrier.combine_mode=CombineMode::Add;
    carrier.general_surface.boundaries=std::move(carrier.loft.sketches);carrier.loft={};return carrier;
}
void resolve_loft_sketches(HistoryContainer& feature,const kernel::ViewerReferenceGeometry& geometry) {
    auto carrier=loft_sketch_editor_carrier(feature);
    for(std::size_t i=0;i<carrier.general_surface.boundaries.size();++i)
        resolve_general_surface_sketch(carrier.general_surface.boundaries[i],general_surface_boundary_reference_geometry(carrier,i,geometry));
    feature.loft.sketches=std::move(carrier.general_surface.boundaries);
}
kernel::LoftRequest loft_request(const PartDocument& part,const HistoryContainer& feature) {
    validate_loft(feature);kernel::LoftRequest result;
    const auto& p=feature.loft;result.region_id=feature.feature_id;
    result.make_solid=p.result_type!=ProfileResultType::Surface;result.thin=p.result_type==ProfileResultType::Thin;
    result.thickness=p.thickness;
    result.thickness_side=p.thin_mode==ThinMode::OneSide?kernel::SurfaceThicknessSide::Second:
        p.thin_mode==ThinMode::OtherSide?kernel::SurfaceThicknessSide::First:kernel::SurfaceThicknessSide::Symmetric;
    result.tolerance=feature.sweep_precision.effective();
    const auto append=[&](const auto& owners,auto& curves,auto& target_owners) {
        for(const auto& id:owners) {
            const auto found=std::ranges::find_if(p.sketches,[&](const auto& value){return value.sketch_feature&&value.sketch_feature->id==id;});
            if(found==p.sketches.end()||found->curve)throw std::runtime_error("Loft source Sketch is unavailable.");
            const auto sketch=sketcher::Sketch::from_serialized(found->sketch_serialized);
            auto curve=body_profile_request(sketch,1.,ExtrusionDirection::Forward,ProfileResultType::Surface,1.,ThinMode::OneSide);
            curve.surface_result=false;
            apply_container_placement(curve,feature.placement);
            curves.push_back(std::move(curve));target_owners.push_back(id);
        }
    };
    append(p.profiles,result.profiles,result.profile_owners);
    append(p.guides,result.guides,result.guide_owners);
    return result;
}
kernel::ViewerMesh loft_definition_mesh(const PartDocument& part,const HistoryContainer& feature) {
    auto result=general_surface_definition_mesh(loft_sketch_editor_carrier(feature));
    for(auto& edge:result.edges) {edge.reference.semantic_key="loft:input:from:"+edge.reference.owner_id+":"+edge.reference.semantic_key;edge.reference.owner_id=feature.id;edge.display_owner_id=feature.id;}
    for(auto& point:result.points){point.reference.semantic_key="loft:input:from:"+point.reference.owner_id+":"+point.reference.semantic_key;point.reference.owner_id=feature.id;point.display_owner_id=feature.id;}
    result.original_references.edges=result.edges;result.original_references.points=result.points;return result;
}
