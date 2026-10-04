// Included after the shared exact Sketch-chain adapter. No kernel calls here.
HistoryContainer create_boundary_surface() {
    auto result=PartDocument::create_sketch_container();
    result.feature_kind=FeatureKind::BoundarySurface;result.name="Zaplnit plochu";
    return result;
}
bool boundary_surface_owner_allowed(const PartDocument& part,const std::string& feature,const std::string& owner) {
    if(owner.empty()||owner==feature)return false;
    const auto position=[&](const std::string& id){return std::ranges::find(part.history_order,id,&PartHistoryEntry::id);};
    const auto input=position(owner),output=position(feature);
    if(input==part.history_order.end()||(output!=part.history_order.end()&&input>=output))return false;
    const auto* source_body=part.body_owner_for_object(owner);
    const auto* target_body=part.body_owner_for_object(feature);
    if(!target_body)target_body=part.body_history.find(part.body_history.active_body_id());
    if(source_body&&target_body&&source_body->scope.id!=target_body->scope.id)return false;
    if(output==part.history_order.end()&&target_body) {
        const auto entry=std::ranges::find(target_body->entries,owner,&PartHistoryEntry::id);
        if(entry==target_body->entries.end()||static_cast<std::size_t>(entry-target_body->entries.begin())>=target_body->cursor)return false;
    }
    if(const auto* curve=part.find_construction(owner))return !curve->suppressed;
    const auto* container=part.find_container(owner);return container&&!container->suppressed;
}
bool boundary_surface_support_allowed(const PartDocument& part,const std::string& feature,const kernel::FaceReference& support) {
    return support.valid()&&support.instance_path.empty()&&part.find_container(support.owner_id)&&
        boundary_surface_owner_allowed(part,feature,support.owner_id);
}
bool boundary_surface_source_allowed(const PartDocument& part,const std::string& feature,const BoundaryCurveSource& source) {
    if(!boundary_surface_owner_allowed(part,feature,source.owner_id))return false;
    if(source.kind==BoundaryCurveSource::Kind::Edge)
        return !source.curve_id.empty()&&part.find_container(source.owner_id);
    if(const auto* curve=part.find_construction(source.owner_id))
        return curve->kind==ConstructionKind::Curve3D&&!curve->suppressed&&source.curve_id.empty();
    const auto* container=part.find_container(source.owner_id);
    if(!container||container->suppressed)return false;
    if(container->feature_kind!=FeatureKind::Sketch&&
       !(container->feature_kind==FeatureKind::Feature&&container->feature.shows_sketch()))return false;
    if(source.curve_id.empty())return true;
    const auto sketch=std::ranges::find(part.sketches,source.owner_id,&sketcher::Sketch::owner_container_id);
    if(sketch==part.sketches.end())return false;
    const auto contains=[&](const auto& curves){return std::ranges::any_of(curves,[&](const auto& c){return c.id==source.curve_id&&!c.construction;});};
    return contains(sketch->segments)||contains(sketch->arcs)||contains(sketch->elliptical_arcs)||contains(sketch->bsplines);
}
kernel::BoundarySurfaceRequest boundary_surface_request(const PartDocument& part,const HistoryContainer& feature) {
    using E=kernel::ExtrusionRequest;
    kernel::BoundarySurfaceRequest result;result.region_id=feature.feature_id;
    result.tolerance=precision_value(part.document_precision,"linear_tolerance",0.001);
    result.angular_tolerance=precision_value(part.document_precision,"angular_tolerance",0.001)*std::acos(-1.)/180;
    const auto count=feature.boundary_surface.boundaries.size();
    if(count<2)throw std::runtime_error("Boundary surface requires at least two boundaries.");
    result.boundaries.resize(count);result.source_owners.resize(count);result.constraints.resize(count);
    std::set<std::tuple<std::string,std::string,BoundaryCurveSource::Kind>> unique;
    for(std::size_t i=0;i<count;++i) {
        const auto& source=feature.boundary_surface.boundaries[i];
        if(!boundary_surface_source_allowed(part,feature.id,source))
            throw std::runtime_error("Fill surface requires preceding curves or original edges in the active Body.");
        if(!unique.emplace(source.owner_id,source.curve_id,source.kind).second)
            throw std::runtime_error("Boundary surface requires different boundaries.");
        result.source_owners[i]=source.owner_id;
        auto& constraint=result.constraints[i];constraint.continuity=source.continuity;
        constraint.support=source.support;constraint.support_reversed=source.support_reversed;
        if(source.continuity!=kernel::SurfaceContinuity::G0 &&
            (source.kind!=BoundaryCurveSource::Kind::Edge||!source.support||!boundary_surface_support_allowed(part,feature.id,*source.support)))
            throw std::runtime_error("G1 and G2 require an original edge and its supporting face.");
        if(source.kind==BoundaryCurveSource::Kind::Edge) {
            constraint.edge=kernel::EdgeReference{source.owner_id,source.curve_id,{}};continue;
        }
        auto& boundary=result.boundaries[i];
        if(const auto* curve=part.find_construction(source.owner_id)) {
            if(!curve->reference_valid)throw std::runtime_error("Boundary curve has unresolved references.");
            const auto route=curve3d_route(*curve);E::CurvedProfile profile;
            const auto world=[&](kernel::Vec3 p){p=rotated_vector(p,curve->rotation);return kernel::Vec3{p.x+curve->origin.x,p.y+curve->origin.y,p.z+curve->origin.z};};
            const auto endpoint_id=[&](const kernel::Vec3& point) {
                std::string id;
                for(const auto& station:route.stations)if(std::hypot(station.origin.x-point.x,station.origin.y-point.y,station.origin.z-point.z)<1e-9) {
                    const auto candidate="point:from:"+station.point_id+(station.incoming?":incoming":":outgoing");
                    if(id.empty()||candidate<id)id=candidate;
                }
                if(id.empty())throw std::runtime_error("Boundary curve identity could not be resolved.");
                return id;
            };
            for(const auto& segment:route.segments) {
                boundary.outer_edge_source_ids.push_back(segment.source_id);
                boundary.outer_vertex_source_ids.push_back(endpoint_id(segment.start));
                if(segment.arc_midpoint)profile.curves.push_back(E::ArcCurve{world(segment.start),world(*segment.arc_midpoint),world(segment.end)});
                else if(!segment.bezier_control_points.empty()) {
                    E::BSplineCurve spline;spline.start=world(segment.start);spline.end=world(segment.end);
                    for(const auto& p:segment.bezier_control_points)spline.control_points.push_back(world(p));
                    profile.curves.push_back(std::move(spline));
                }else profile.curves.push_back(E::LineCurve{world(segment.start),world(segment.end)});
            }
            boundary.outer_profile=std::move(profile);boundary.direction={0,0,1};
            if(!route.segments.empty())boundary.open_profile_end_id=endpoint_id(route.segments.back().end);
        }else {
            const auto found=std::ranges::find(part.sketches,source.owner_id,&sketcher::Sketch::owner_container_id);
            if(found==part.sketches.end())throw std::runtime_error("Boundary Sketch is missing.");
            auto sketch=*found;
            if(!source.curve_id.empty()) {
                const auto keep=[&](auto& curves){std::erase_if(curves,[&](const auto& c){return c.id!=source.curve_id;});};
                keep(sketch.segments);keep(sketch.arcs);keep(sketch.elliptical_arcs);keep(sketch.bsplines);
                sketch.circles.clear();sketch.ellipses.clear();sketch.texts.clear();
            }
            const auto count=[](const auto& curves){return std::ranges::count_if(curves,[](const auto& c){return !c.construction;});};
            if(count(sketch.circles)||count(sketch.ellipses)||!sketch.texts.empty())
                throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
            const auto expected=count(sketch.segments)+count(sketch.arcs)+count(sketch.elliptical_arcs)+count(sketch.bsplines);
            std::string end;
            boundary=open_sweep_profile(sketch,end);boundary.open_profile_end_id=end;
            if(std::get<E::CurvedProfile>(boundary.outer_profile).curves.size()!=static_cast<std::size_t>(expected))
                throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
        }
        const auto* curves=std::get_if<E::CurvedProfile>(&boundary.outer_profile);
        if(!curves||curves->curves.empty())throw std::runtime_error("Each boundary must be one nonempty open curve chain.");
    }
    return result;
}

