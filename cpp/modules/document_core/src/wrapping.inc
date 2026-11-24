kernel::WrappingRequest wrapping_request(const PartDocument& part,
        const HistoryContainer& container,double tolerance) {
    validate_feature_parameters(container.feature);
    const auto& p=container.feature.wrapping;
    const auto source=std::ranges::find(part.sketches,container.feature.sketch_id,&sketcher::Sketch::id);
    if(source==part.sketches.end()||source->owner_container_id!=container.id||
            container.feature.profile_source!=ProfileSource::Internal)
        throw std::runtime_error("Wrap requires an owned planar Sketch.");
    if(!p.target)throw std::runtime_error("Select a cylindrical, conical or spherical face to wrap.");
    if(std::ranges::any_of(source->offsets,[](const auto& value){return value.broken;})||
       std::ranges::any_of(source->curve_trims,[](const auto& value){return value.broken;}))
        throw std::runtime_error("Sketch contains an unresolved offset or trim intersection");
    const auto& target=*p.target;
    kernel::WrappingRequest result;result.sketch_id=source->id;result.target=target.reference;
    result.origin={container.placement.x,container.placement.y,container.placement.z};result.x_axis=source->resolved_x_axis;
    result.y_axis=source->resolved_y_axis;result.normal=source->normal();
    result.height=p.height;result.tolerance=tolerance;result.reverse_u=p.reverse_u;result.reverse_v=p.reverse_v;
    result.inward=container.combine_mode==CombineMode::Subtract;
    result.output=p.output==WrappingOutput::Curves?kernel::WrappingResult::Curves:
        p.output==WrappingOutput::Surface?kernel::WrappingResult::Surface:
        p.output==WrappingOutput::Split?kernel::WrappingResult::Split:kernel::WrappingResult::Relief;
    if(target.external_source) {
        if(p.output==WrappingOutput::Relief||p.output==WrappingOutput::Split)throw std::runtime_error("Wrap cannot change material on a read-only external face.");
        const auto& external=*target.external_source;
        if(external.source_face->kernel_shape.empty())throw std::runtime_error("Wrap target is unavailable.");
        auto local=target.reference;local.instance_path.clear();
        result.external_target=std::make_shared<const kernel::OrderedUpToSource>(
            kernel::OrderedUpToSource{external.source_face,local,external.source_to_body});
    } else if(!boundary_surface_support_allowed(part,container.id,target.reference))
        throw std::runtime_error("Wrap target must be a preceding bounded face in the active Body.");
    const auto evaluated=source->evaluated_profile_sketch();
    result.images=evaluated.viewer_mesh(false).images;
    auto curves=evaluated;curves.texts.clear();
    const auto profiles=projection_sketch_profiles(curves);
    if(p.output==WrappingOutput::Curves||p.output==WrappingOutput::Split) {
        result.profiles=profiles;
        if(!evaluated.texts.empty()) {
            auto text=evaluated;text.segments.clear();text.circles.clear();text.arcs.clear();
            text.ellipses.clear();text.elliptical_arcs.clear();text.bsplines.clear();
            result.profiles.push_back(extrusion_request(text,1.,ExtrusionDirection::Forward,true));
        }
    } else if(!profiles.empty()||!evaluated.texts.empty())result.profiles.push_back(extrusion_request(evaluated,1.,ExtrusionDirection::Forward,true));
    if(result.profiles.empty()&&result.images.empty())throw std::runtime_error("Wrap requires Sketch curves or an image.");
    return result;
}
