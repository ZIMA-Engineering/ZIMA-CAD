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
void validate_loft(const HistoryContainer& feature,bool require_complete) {
    const auto& p=feature.loft;
    static_cast<void>(feature.sweep_precision.effective());
    if(feature.feature_kind!=FeatureKind::Loft||(require_complete&&p.profiles.size()<2))
        throw std::invalid_argument("Loft requires at least two different profile Sketches.");
    if(static_cast<unsigned>(p.result_type)>static_cast<unsigned>(ProfileResultType::Surface)||
       (p.thin_mode!=ThinMode::OneSide&&p.thin_mode!=ThinMode::OtherSide&&p.thin_mode!=ThinMode::Symmetric)||
       !std::isfinite(p.thickness)||p.thickness<=0.)
        throw std::invalid_argument("Invalid Loft result or thickness.");
    if(p.result_type==ProfileResultType::Surface&&feature.combine_mode!=CombineMode::Add)
        throw std::invalid_argument("A surface Loft cannot subtract material.");
    auto carrier=loft_sketch_editor_carrier(feature);validate_general_surface(carrier,require_complete);
    if(p.sketches.size()!=p.profiles.size()+p.guides.size())throw std::invalid_argument("Loft source Sketch is unavailable.");
    for(const auto& value:p.sketches)if(value.curve)throw std::invalid_argument("Loft source Sketch is unavailable.");
    std::set<std::string> unique;
    for(const auto* family:{&p.profiles,&p.guides})for(const auto& id:*family)
        if(id.empty()||!unique.insert(id).second||std::ranges::none_of(p.sketches,[&](const auto& value){return value.sketch_feature&&value.sketch_feature->id==id;}))
            throw std::invalid_argument("Select different profile and guide Sketches for Loft.");
    for(const auto& [profile,point]:p.correspondence_starts)
        if(point.empty()||std::ranges::find(p.profiles,profile)==p.profiles.end())
            throw std::invalid_argument("Loft source Sketch is unavailable.");
    for(const auto& profile:p.correspondence_reversed)
        if(std::ranges::find(p.profiles,profile)==p.profiles.end())
            throw std::invalid_argument("Loft source Sketch is unavailable.");
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
    std::vector<kernel::Sweep3DRequest::Section> sections;
    bool all_numbered=true;
    for(const auto& id:p.profiles) {
        const auto owned=std::ranges::find_if(p.sketches,[&](const auto& value){return value.sketch_feature->id==id;});
        const auto sketch=sketcher::Sketch::from_serialized(owned->sketch_serialized);
        const auto start=p.correspondence_starts.find(id);
        const auto first=start==p.correspondence_starts.end()?std::string{}:start->second;
        const bool reversed=p.correspondence_reversed.contains(id);
        const auto mapping=sweep3d_profile_correspondence(sketch,first,p.result_type!=ProfileResultType::Solid,reversed);
        all_numbered=all_numbered&&!mapping.point_ids.empty();
        sections.push_back(profile_correspondence_section(sketch,first,p.result_type!=ProfileResultType::Solid,false,reversed));
    }
    if(all_numbered) {
        const auto count=sections.front().profile.outer_vertex_source_ids.size();
        all_numbered=std::ranges::all_of(sections,[&](const auto& section){return section.profile.outer_vertex_source_ids.size()==count;});
    }
    if(!all_numbered&&(!p.correspondence_starts.empty()||!p.correspondence_reversed.empty()))
        throw std::runtime_error("Loft correspondence requires the same number of points on every profile.");
    if(all_numbered) {
        result.match_profile_vertices=true;
        result.profile_radial_directions.reserve(sections.size());
        for(std::size_t i=0;i<sections.size();++i) {
            auto& section=sections[i];auto& curve=result.profiles[i];
            curve.outer_profile=std::move(section.profile.outer_profile);
            curve.outer_edge_source_ids=std::move(section.profile.outer_edge_source_ids);
            curve.outer_vertex_source_ids=std::move(section.profile.outer_vertex_source_ids);
            curve.direction=section.profile_normal;curve.open_profile_end_id=section.thin_end_point_id;
            apply_container_placement(curve,feature.placement);
            auto radial=section.circle_radial_direction;
            if(radial)*radial=placement_transform_point(placement_rotation_matrix_from_euler_degrees(
                {feature.placement.rotation_x,feature.placement.rotation_y,feature.placement.rotation_z}),{},*radial);
            result.profile_radial_directions.push_back(radial);
        }
    }
    return result;
}
kernel::ViewerMesh loft_profile_anchor_mesh(const HistoryContainer& feature,std::size_t before) {
    kernel::ViewerMesh result;
    for(std::size_t i=0;i<std::min(before,feature.loft.sketches.size());++i) {
        const auto& owned=feature.loft.sketches[i];
        if(!owned.sketch_feature||std::ranges::find(feature.loft.profiles,owned.sketch_feature->id)==feature.loft.profiles.end())continue;
        try {
            const auto sketch=general_surface_display_sketch(feature,owned);
            const auto found=feature.loft.correspondence_starts.find(owned.sketch_feature->id);
            const auto order=sweep3d_profile_correspondence(sketch,found==feature.loft.correspondence_starts.end()?std::string{}:found->second,
                feature.loft.result_type!=ProfileResultType::Solid,
                feature.loft.correspondence_reversed.contains(owned.sketch_feature->id));
            for(std::size_t n=0;n<order.point_ids.size();++n) {
                kernel::ViewerPoint point{order.positions[n],{sketch.id,"point:"+order.point_ids[n],{}},std::to_string(n+1)};
                point.display_owner_id=sketch.id;result.points.push_back(std::move(point));
            }
        }catch(const std::exception&) { /* Incomplete profiles retain their ordinary Sketch preview. */ }
    }
    result.original_references.points=result.points;return result;
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
            const auto evaluated=sketch.evaluated_profile_sketch();
            auto edges=profile_preview_source_edges(evaluated);
            const auto& id=owned.sketch_feature->id;
            if(feature.loft.result_type==ProfileResultType::Thin&&
               std::ranges::find(feature.loft.profiles,id)!=feature.loft.profiles.end()) {
                std::optional<kernel::Vec3> start;
                const auto first=feature.loft.correspondence_starts.find(id);
                const auto order=sweep3d_profile_correspondence(evaluated,
                    first==feature.loft.correspondence_starts.end()?std::string{}:first->second,true,
                    feature.loft.correspondence_reversed.contains(id));
                if(!order.closed&&!order.positions.empty())start=order.positions.front();
                // Section-plane thickness guides, not a calculated offset skin.
                // Reuse the modeling preview's exact same side convention.
                edges=thin_profile_preview_edges(evaluated,feature.loft.thickness,feature.loft.thin_mode,true,start);
            }
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
    // Correspondence markers are real ordered profile points. Sketch picking
    // helpers (curve midpoints, circle centres and datum markers) are not Loft
    // correspondence stations and must not appear alongside their numbers.
    result.points=loft_profile_anchor_mesh(feature,feature.loft.sketches.size()).points;
    for(auto& edge:result.edges) {edge.reference.semantic_key="loft:input:from:"+edge.reference.owner_id+":"+edge.reference.semantic_key;edge.reference.owner_id=feature.id;edge.display_owner_id=feature.id;}
    for(auto& point:result.points){point.reference.semantic_key="loft:input:from:"+point.reference.owner_id+":"+point.reference.semantic_key;point.reference.owner_id=feature.id;point.display_owner_id=feature.id;}
    result.original_references.edges=result.edges;result.original_references.points=result.points;
    std::vector<std::vector<kernel::Vec3>> sections;
    for(const auto& id:feature.loft.profiles) {
        const auto owned=std::ranges::find_if(feature.loft.sketches,[&](const auto& item){return item.sketch_feature&&item.sketch_feature->id==id;});
        if(owned==feature.loft.sketches.end())return result;
        const auto prefix="loft:input:from:"+owned->sketch_feature->feature.sketch_id+":point:";
        std::vector<kernel::Vec3> points;
        for(const auto& point:result.points)if(point.reference.semantic_key.starts_with(prefix))points.push_back(point.position);
        if(points.empty()||(!sections.empty()&&sections.front().size()!=points.size()))return result;
        sections.push_back(std::move(points));
    }
    for(std::size_t section=1;section<sections.size();++section)for(std::size_t point=0;point<sections[section].size();++point) {
        kernel::ViewerEdge link;
        link.points={sections[section-1][point],sections[section][point]};
        link.reference={feature.id,"loft:correspondence:"+feature.loft.profiles[section]+":"+std::to_string(point),{}};
        link.overlay=true;link.preview_terminal_dashed=true;result.edges.push_back(std::move(link));
    }
    return result;
}
