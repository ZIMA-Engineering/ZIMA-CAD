static std::vector<kernel::ExtrusionRequest> projection_sketch_profiles(const sketcher::Sketch& evaluated) {
    std::vector<kernel::ExtrusionRequest> profiles;
    const auto append=[&](const auto& curves) {
        for(const auto& curve:curves)if(!curve.construction) {
            auto isolated=evaluated;
            // Isolate the modeling curve, not the constraint graph. A polygon
            // may still reference its construction circle, or another segment
            // through equal-length constraints. Removing those supports made
            // an otherwise valid Sketch fail validation in the profile adapter.
            const auto keep=[&](auto& values){for(auto& value:values)if(value.id!=curve.id)value.construction=true;};
            keep(isolated.segments);keep(isolated.circles);keep(isolated.arcs);
            keep(isolated.ellipses);keep(isolated.elliptical_arcs);keep(isolated.bsplines);
            profiles.push_back(body_profile_request(isolated,1.,ExtrusionDirection::Forward,
                ProfileResultType::Surface,1.,ThinMode::OneSide));
        }
    };
    append(evaluated.segments);append(evaluated.circles);append(evaluated.arcs);
    append(evaluated.ellipses);append(evaluated.elliptical_arcs);append(evaluated.bsplines);
    return profiles;
}

void resolve_fem_condition_sketch_plane(FemCondition& condition,const kernel::ViewerReferenceGeometry& geometry) {
    if(!condition.projected_sketch||!condition.placement.reference_valid)return;
    auto sketch=sketcher::Sketch::from_serialized(condition.projected_sketch->serialized);
    if(!sketch.plane_auto)return;
    sketch.plane=automatic_sketch_work_plane(condition.placement.references,geometry).value_or(sketcher::SketchPlane::XY);
    sketch.refresh_default_frame();condition.projected_sketch->serialized=sketch.serialized();
}
sketcher::Sketch fem_condition_sketch(const FemCondition& condition) {
    auto sketch=sketcher::Sketch::from_serialized(condition.projected_sketch->serialized);
    sketch.refresh_default_frame();
    const auto& p=condition.placement;
    const auto rotation=placement_rotation_matrix_from_euler_degrees({p.rotation_x,p.rotation_y,p.rotation_z});
    sketch.resolved_origin=placement_transform_point(rotation,{p.x,p.y,p.z},sketch.resolved_origin);
    sketch.resolved_x_axis=placement_transform_direction(rotation,sketch.resolved_x_axis);
    sketch.resolved_y_axis=placement_transform_direction(rotation,sketch.resolved_y_axis);
    sketch.resolved_normal=placement_transform_direction(rotation,sketch.resolved_normal);
    return sketch;
}

kernel::ProjectionRequest fem_condition_projection_request(const FemCondition& condition) {
    const auto sketch=fem_condition_sketch(condition);
    if(std::ranges::any_of(sketch.external_references,[](const auto& r){return r.broken;}))
        throw std::invalid_argument("The condition geometry is unavailable on the FEM source.");
    kernel::ProjectionRequest request;
    request.profiles=projection_sketch_profiles(sketch.evaluated_profile_sketch());
    request.sketch_origin=sketch.world_point(0,0);
    request.sketch_normal=sketch.normal();
    return request;
}

kernel::ProjectionRequest fem_condition_planar_request(const FemCondition& condition) {
    const auto placed=fem_condition_sketch(condition);
    if(std::ranges::any_of(placed.external_references,[](const auto& r){return r.broken;}))
        throw std::invalid_argument("The condition geometry is unavailable on the FEM source.");
    const auto sketch=placed.evaluated_profile_sketch();
    kernel::ProjectionRequest request;
    request.sketch_origin=sketch.world_point(0,0);
    request.sketch_normal=sketch.normal();
    if(condition.region==FemRegionKind::Surface) {
        auto profile=extrusion_request(sketch,1.,ExtrusionDirection::Forward,true);
        profile.surface_result=true;
        profile.planar_profile=true;
        request.profiles.push_back(std::move(profile));
        request.output=kernel::ProjectionResult::Surface;
    }else request.profiles=projection_sketch_profiles(sketch);
    return request;
}

void refresh_fem_condition_sketch(FemCondition& condition,const std::string& document_id,
        const kernel::ViewerMesh& source) {
    if(!condition.projected_sketch)return;
    auto sketch=fem_condition_sketch(condition);
    if(sketch.external_references.empty())return;
    for(const auto& reference:sketch.external_references)
        if(reference.source_document_id!=document_id||!reference.source_instance_path.empty()||
                !reference.context_assembly_document_id.empty())
            throw std::invalid_argument("The condition geometry is unavailable on the FEM source.");
    kernel::ViewerReferenceGeometry body_geometry;
    if(std::ranges::any_of(sketch.external_references,[](const auto& r){return r.body_edge;}))
        body_geometry.edges=source.edges;
    static_cast<void>(sketch.refresh_external_references(document_id,source.original_references,false,&body_geometry));
    sketch.refresh_default_frame();
    condition.projected_sketch->serialized=sketch.serialized();
}

// Feature-owned planar input, ordinary native curve adapters and the same
// history-boundary ownership checks as other surface operations. No OCCT here.
kernel::ProjectionRequest projection_request(const PartDocument& part,
        const HistoryContainer& container,double tolerance) {
    const auto& p=container.feature.projection;
    validate_feature_parameters(container.feature);
    if((p.output==ProjectionOutput::Surface||p.output==ProjectionOutput::Relief)&&p.regions.empty())
        throw std::runtime_error("Select a retained region on the target face before confirming the projection.");
    const auto source=std::ranges::find(part.sketches,container.feature.sketch_id,&sketcher::Sketch::id);
    if(source==part.sketches.end())throw std::runtime_error("Extrusion references a missing Sketch");
    if(container.feature.profile_source!=ProfileSource::Internal||source->owner_container_id!=container.id||
            (p.output!=ProjectionOutput::Relief&&container.combine_mode!=CombineMode::Add))
        throw std::runtime_error("Projection requires its own planar Sketch and a valid result operation.");
    if(std::ranges::any_of(source->offsets,[](const auto& value){return value.broken;})||
       std::ranges::any_of(source->curve_trims,[](const auto& value){return value.broken;}))
        throw std::runtime_error("Sketch contains an unresolved offset or trim intersection");
    auto evaluated=source->evaluated_profile_sketch();
    kernel::ProjectionRequest result;result.sketch_id=source->id;
    result.sketch_origin=source->world_point(0,0);result.sketch_normal=source->normal();
    result.tolerance=tolerance;result.reverse=p.reverse;
    result.both_directions=p.direction==ProjectionDirection::SketchNormal;
    result.output=p.output==ProjectionOutput::Curves?kernel::ProjectionResult::Curves:
        p.output==ProjectionOutput::Split?kernel::ProjectionResult::Split:
        p.output==ProjectionOutput::Surface?kernel::ProjectionResult::Surface:kernel::ProjectionResult::Relief;
    result.height=p.height;result.inward=container.combine_mode==CombineMode::Subtract;
    if(result.output==kernel::ProjectionResult::Curves)
        for(const auto& point:evaluated.viewer_mesh(false).points)
            if(!point.construction&&point.always_visible&&point.reference.semantic_key.starts_with("point:"))
                result.display_points.push_back(point.position);
    // Project every authored curve independently. A projection source need not
    // form the single wire required by a sweep; incidence stays in point IDs.
    result.profiles=projection_sketch_profiles(evaluated);
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
            if(result.output==kernel::ProjectionResult::Split||result.output==kernel::ProjectionResult::Relief)
                throw std::runtime_error("Projection cannot modify a read-only external target.");
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
    if(result.output==kernel::ProjectionResult::Surface||result.output==kernel::ProjectionResult::Relief)for(const auto& region:p.regions) {
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
