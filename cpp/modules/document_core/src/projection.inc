// Feature-owned planar input, ordinary native curve adapters and the same
// history-boundary ownership checks as other surface operations. No OCCT here.
kernel::ProjectionRequest projection_request(const PartDocument& part,
        const HistoryContainer& container,double tolerance) {
    const auto& p=container.feature.projection;
    validate_feature_parameters(container.feature);
    const auto source=std::ranges::find(part.sketches,container.feature.sketch_id,&sketcher::Sketch::id);
    if(source==part.sketches.end())throw std::runtime_error("Extrusion references a missing Sketch");
    if(container.feature.profile_source!=ProfileSource::Internal||source->owner_container_id!=container.id||
            container.combine_mode!=CombineMode::Add)
        throw std::runtime_error("Projection requires an owned planar Sketch and does not add or subtract solid material.");
    if(std::ranges::any_of(source->offsets,[](const auto& value){return value.broken;})||
       std::ranges::any_of(source->curve_trims,[](const auto& value){return value.broken;}))
        throw std::runtime_error("Sketch contains an unresolved offset or trim intersection");
    auto evaluated=source->evaluated_profile_sketch();
    kernel::ProjectionRequest result;result.sketch_id=source->id;
    result.sketch_origin=source->world_point(0,0);result.sketch_normal=source->normal();
    result.tolerance=tolerance;result.reverse=p.reverse;
    result.output=p.output==ProjectionOutput::Curves?kernel::ProjectionResult::Curves:
        p.output==ProjectionOutput::Split?kernel::ProjectionResult::Split:kernel::ProjectionResult::Surface;
    // Project every authored curve independently. A projection source need not
    // form the single wire required by a sweep; incidence stays in point IDs.
    const auto append=[&](const auto& curves) {
        for(const auto& curve:curves)if(!curve.construction) {
            auto isolated=evaluated;
            const auto keep=[&](auto& values){std::erase_if(values,[&](const auto& value){return value.id!=curve.id;});};
            keep(isolated.segments);keep(isolated.circles);keep(isolated.arcs);
            keep(isolated.ellipses);keep(isolated.elliptical_arcs);keep(isolated.bsplines);
            auto profile=body_profile_request(isolated,1.,ExtrusionDirection::Forward,ProfileResultType::Surface,1.,ThinMode::OneSide);
            result.profiles.push_back(std::move(profile));
        }
    };
    append(evaluated.segments);append(evaluated.circles);append(evaluated.arcs);
    append(evaluated.ellipses);append(evaluated.elliptical_arcs);append(evaluated.bsplines);
    result.direction=result.sketch_normal;
    if(p.direction==ProjectionDirection::Reference) {
        const auto& ref=p.direction_reference;
        if(!ref.valid()||ref.owner_id==container.id)throw std::runtime_error("Projection direction reference is missing or is not straight.");
        auto geometry=part.origin_viewer_mesh().original_references;
        append_reference_geometry(geometry,part.history_origin_reference_geometry_before(container.id));
        append_reference_geometry(geometry,part.construction_viewer_mesh().original_references);
        geometry=part.construction_reference_geometry_for(container.id,std::move(geometry));
        ConstructionReference reference;reference.owner_id=ref.owner_id;reference.semantic_key=ref.semantic_key;reference.instance_path=ref.instance_path;
        if(const auto axis=placement_reference_axis(reference,geometry,{},true)) {
            if(part.find_construction(ref.owner_id)&&!boundary_surface_owner_allowed(part,container.id,ref.owner_id))
                throw std::runtime_error("Projection direction reference is missing or is not straight.");
            result.direction=axis->direction;
        } else {
            if(!boundary_surface_owner_allowed(part,container.id,ref.owner_id)||!ref.instance_path.empty())
                throw std::runtime_error("Projection direction reference is missing or is not straight.");
            result.direction_reference=ref;
        }
    }
    for(const auto& target:p.targets) {
        if(target.external_source) {
            if(result.output==kernel::ProjectionResult::Split)
                throw std::runtime_error("Projection cannot split a read-only external target.");
            const auto& external=*target.external_source;
            if(external.source_face->kernel_shape.empty())
                throw std::runtime_error("Projection source ancestry is missing.");
            auto local=target.reference;local.instance_path.clear();
            result.external_targets.push_back({target.reference,std::make_shared<const kernel::OrderedUpToSource>(
                kernel::OrderedUpToSource{external.source_face,local,external.source_to_body})});
        } else if(!boundary_surface_support_allowed(part,container.id,target.reference))
            throw std::runtime_error("Projection target must be a preceding bounded face in the active Body.");
        result.targets.push_back(target.reference);
    }
    if(result.output==kernel::ProjectionResult::Surface)for(const auto& region:p.regions) {
        auto seed=region.seed;
        if(const auto* target=part.find_container(region.target.owner_id);target&&region.target.instance_path.empty()) {
            const auto& placement=target->placement;
            seed=placement_transform_point(placement_rotation_matrix_from_euler_degrees(
                {placement.rotation_x,placement.rotation_y,placement.rotation_z}),
                {placement.x,placement.y,placement.z},seed);
        }
        result.regions.push_back({region.target,seed,region.boundary_key});
    }
    if(result.profiles.empty()||result.targets.empty())
        throw std::runtime_error("Projection requires source curves, target faces and a positive tolerance.");
    return result;
}
