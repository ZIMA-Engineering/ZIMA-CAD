// Owned Sketch placement is expressed in the Loft frame, including its datum references.
GeneralSurfaceBoundary create_loft_sketch(const HistoryContainer& parent) {
    auto result=create_general_surface_sketch(parent);
    std::vector<ConstructionReference> position,orientation;std::array<bool,3> locks{};
    for(const auto* plane:{"xz","xy","yz"}) {
        ConstructionReference reference;reference.owner_id=parent.container_origin.id;
        reference.semantic_key=std::string("origin:plane:")+plane;reference.supports_offset=true;
        static_cast<void>(assign_placement_reference({position,orientation,locks},true,position.size(),std::move(reference)));
    }
    result.sketch_feature->placement.references=combined_placement_references(position,orientation);
    return result;
}
// Exact ordinary 2D Sketch adapters; no OCCT calculation or document mutation.
HistoryContainer create_loft() {
    auto feature=PartDocument::create_sketch_container();
    feature.feature_kind=FeatureKind::Loft;feature.name="Přechod";
    return feature;
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
    for(std::size_t i=0;i<carrier.general_surface.boundaries.size();++i) {
        auto& boundary=carrier.general_surface.boundaries[i];const auto previous=boundary;
        resolve_general_surface_sketch(boundary,general_surface_boundary_reference_geometry(carrier,i,geometry));
        if(!boundary.sketch_feature->placement.reference_valid) {
            // The persisted parent-local frame remains authoritative when an anchor is unavailable.
            boundary=previous;boundary.sketch_feature->placement.reference_valid=false;
        }
    }
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
kernel::ViewerMesh loft_definition_mesh(const PartDocument& part,const HistoryContainer& feature,const kernel::ViewerReferenceGeometry& geometry) {
    auto carrier=loft_sketch_editor_carrier(feature);
    reframe_general_surface(carrier);
    auto result=general_surface_definition_mesh(carrier);
    // Keep the established owned-Sketch annotations/origin presentation, but
    // draw exactly the same evaluated material curves as a modeling profile.
    // The editor mesh includes construction/reference strokes and untrimmed
    // corner legs; it is not the authored Loft creation wire.
    result.edges.clear();
    for(const auto& owned:carrier.general_surface.boundaries) {
        if(!owned.sketch_feature||owned.sketch_serialized.empty())continue;
        const auto sketch=general_surface_display_sketch(carrier,owned);
        try {
            auto edges=profile_preview_source_edges(sketch.evaluated_profile_sketch());
            result.edges.insert(result.edges.end(),std::make_move_iterator(edges.begin()),std::make_move_iterator(edges.end()));
        }catch(const std::exception&) {
            // A partial draft still displays its supported native curves.
            auto edges=profile_preview_source_edges(sketch);
            result.edges.insert(result.edges.end(),std::make_move_iterator(edges.begin()),std::make_move_iterator(edges.end()));
        }
    }
    kernel::ViewerMesh parameters;
    for(std::size_t i=0;i<feature.loft.sketches.size();++i) {
        const auto& owned=feature.loft.sketches[i];if(!owned.sketch_feature)continue;
        const auto& f=*owned.sketch_feature;
        auto dimensions=container_placement_dimensions(f.id,f.placement,general_surface_boundary_reference_geometry(carrier,i,geometry));
        for(auto& dimension:dimensions)if(std::abs(dimension.value)>1e-12)parameters.dimensions.push_back(std::move(dimension));
        const auto sketch=sketcher::Sketch::from_serialized(owned.sketch_serialized);
        if(std::abs(sketch.plane_offset)>1e-12) {
            const auto b=sketch.resolved_origin,n=sketch.resolved_normal,x=sketch.resolved_x_axis;
            const kernel::Vec3 a{b.x-n.x*sketch.plane_offset,b.y-n.y*sketch.plane_offset,b.z-n.z*sketch.plane_offset};
            kernel::ViewerDimension dimension{a,b,{a.x+x.x*8,a.y+x.y*8,a.z+x.z*8},{b.x+x.x*8,b.y+x.y*8,b.z+x.z*8},sketch.plane_offset,{f.id,"parameter:profile_offset",{}}};
            dimension.value_lock_key="profile_offset";dimension.locked=f.value_locks.contains("profile_offset");
            parameters.dimensions.push_back(std::move(dimension));
        }
    }
    parameters=general_surface_place_definition_mesh(feature.placement,std::move(parameters));
    result.dimensions.insert(result.dimensions.end(),parameters.dimensions.begin(),parameters.dimensions.end());
    for(auto& edge:result.edges) {edge.reference.semantic_key="loft:input:from:"+edge.reference.owner_id+":"+edge.reference.semantic_key;edge.reference.owner_id=feature.id;edge.display_owner_id=feature.id;}
    for(auto& point:result.points){point.reference.semantic_key="loft:input:from:"+point.reference.owner_id+":"+point.reference.semantic_key;point.reference.owner_id=feature.id;point.display_owner_id=feature.id;}
    result.original_references.edges=result.edges;result.original_references.points=result.points;return result;
}
