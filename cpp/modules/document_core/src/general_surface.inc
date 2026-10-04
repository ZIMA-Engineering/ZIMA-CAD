// Owned native definitions reuse the ordinary Sketch/Curve3D adapters. No OCCT.
GeneralSurfaceBoundary create_general_surface_sketch(const HistoryContainer& parent) {
    auto sketch=sketcher::Sketch::create_default();
    sketch.owner_container_id=parent.id;sketch.plane_reference_owner_id=parent.container_origin.id;
    return {sketch.serialized(),std::nullopt};
}
GeneralSurfaceBoundary create_general_surface_curve(const HistoryContainer& parent) {
    auto curve=PartDocument::create_construction(ConstructionKind::Curve3D);
    curve.parent_construction_id=parent.id;return {{},std::move(curve)};
}
HistoryContainer create_general_surface() {
    auto feature=PartDocument::create_sketch_container();
    feature.feature_kind=FeatureKind::GeneralSurface;feature.name="Obecná plocha";
    for(unsigned i=0;i<4;++i)feature.general_surface.boundaries.push_back(create_general_surface_sketch(feature));
    reframe_general_surface(feature);return feature;
}
void validate_general_surface(const HistoryContainer& feature) {
    if(feature.feature_kind!=FeatureKind::GeneralSurface||feature.combine_mode!=CombineMode::Add)
        throw std::invalid_argument("Invalid owned surface boundary.");
    if(feature.general_surface.boundaries.size()<2)
        throw std::invalid_argument("Boundary surface requires at least two boundaries.");
    std::set<std::string> ids{feature.id,feature.feature_id,feature.container_origin.id};
    const auto unique=[&](const std::string& id){if(id.empty()||!ids.insert(id).second)throw std::invalid_argument("Invalid owned surface boundary.");};
    for(const auto& boundary:feature.general_surface.boundaries) {
        if(boundary.sketch_serialized.empty()==!boundary.curve)
            throw std::invalid_argument("Invalid owned surface boundary.");
        if(boundary.curve) {
            const auto& curve=*boundary.curve;
            if(curve.kind!=ConstructionKind::Curve3D||curve.parent_construction_id!=feature.id||curve.suppressed)
                throw std::invalid_argument("Invalid owned surface boundary.");
            const auto register_curve=[&](auto&& self,const ConstructionObject& object)->void {
                unique(object.id);unique(object.entity_id);unique(object.container_origin.id);
                for(const auto& point:object.curve_points)self(self,point);
            };
            register_curve(register_curve,curve);
            auto carrier=curve;carrier.parent_construction_id.clear();
            static_cast<void>(serialize_construction_objects({carrier}));
        }else {
            const auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
            if(sketch.owner_container_id!=feature.id||sketch.plane_reference_owner_id!=feature.container_origin.id||sketch.suppressed)
                throw std::invalid_argument("Invalid owned surface boundary.");
            unique(sketch.id);sketch.validate();
        }
    }
}
void validate_general_surface_document_ownership(const PartDocument& part) {
    if(std::ranges::none_of(part.history,[](const auto& feature){return feature.feature_kind==FeatureKind::GeneralSurface;}))return;
    std::set<std::string> occupied{part.document_id};
    const auto construction=[&](auto&& self,const ConstructionObject& object)->void {
        occupied.insert(object.id);occupied.insert(object.entity_id);occupied.insert(object.container_origin.id);
        for(const auto& child:object.container_origin.children)occupied.insert(child.id);
        for(const auto& point:object.curve_points)self(self,point);
    };
    for(const auto& object:part.constructions)construction(construction,object);
    for(const auto& sketch:part.sketches)occupied.insert(sketch.id);
    for(const auto& feature:part.history) {
        occupied.insert(feature.id);occupied.insert(feature.feature_id);occupied.insert(feature.container_origin.id);
        for(const auto& child:feature.container_origin.children)occupied.insert(child.id);
        if(feature.feature_kind==FeatureKind::Sweep3D)construction(construction,feature.sweep3d.path);
        if(feature.feature_kind!=FeatureKind::GeneralSurface)
            visit_feature_sketches(feature,[&](const auto& data,std::size_t){occupied.insert(sketcher::Sketch::from_serialized(data).id);});
    }
    for(const auto& feature:part.history)if(feature.feature_kind==FeatureKind::GeneralSurface) {
        validate_general_surface(feature);
        const auto owned=[&](auto&& self,const ConstructionObject& object)->void {
            // Point entities also alias their Origin point child. Check each
            // native object's primary identity once, then reserve all aliases.
            for(const auto& id:{object.id,object.entity_id,object.container_origin.id})
                if(occupied.contains(id))throw std::invalid_argument("Invalid owned surface boundary.");
            occupied.insert(object.id);occupied.insert(object.entity_id);occupied.insert(object.container_origin.id);
            for(const auto& child:object.container_origin.children)occupied.insert(child.id);
            for(const auto& point:object.curve_points)self(self,point);
        };
        for(const auto& boundary:feature.general_surface.boundaries) {
            if(boundary.curve)owned(owned,*boundary.curve);
            else if(!occupied.insert(sketcher::Sketch::from_serialized(boundary.sketch_serialized).id).second)
                throw std::invalid_argument("Invalid owned surface boundary.");
        }
    }
}
void reframe_general_surface(HistoryContainer& feature) {
    const auto& p=feature.placement;
    const auto rotation=placement_rotation_matrix_from_euler_degrees({p.rotation_x,p.rotation_y,p.rotation_z});
    const kernel::Vec3 origin{p.x,p.y,p.z};
    for(auto& boundary:feature.general_surface.boundaries)if(!boundary.sketch_serialized.empty()) {
        auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
        if(sketch.owner_container_id!=feature.id||sketch.plane_reference_owner_id!=feature.container_origin.id)
            throw std::invalid_argument("Invalid owned surface boundary.");
        // Ordinary plane selection and offset stay local to the parent Origin.
        auto local=sketch;local.plane_reference_owner_id.clear();local.refresh_default_frame();
        sketch.resolved_origin=placement_transform_point(rotation,origin,local.resolved_origin);
        sketch.resolved_x_axis=placement_transform_direction(rotation,local.resolved_x_axis);
        sketch.resolved_y_axis=placement_transform_direction(rotation,local.resolved_y_axis);
        sketch.resolved_normal=placement_transform_direction(rotation,local.resolved_normal);
        const auto data=sketch.serialized();if(data!=boundary.sketch_serialized)boundary.sketch_serialized=data;
    }
}
ConstructionObject general_surface_display_curve(const HistoryContainer& parent,const ConstructionObject& source) {
    auto curve=source;curve.parent_construction_id.clear();
    const auto& p=parent.placement;const kernel::Vec3 rotation{p.rotation_x,p.rotation_y,p.rotation_z};
    const auto matrix=placement_rotation_matrix_from_euler_degrees(rotation);
    curve.origin=placement_transform_point(matrix,{p.x,p.y,p.z},source.origin);curve.entity_origin=curve.origin;
    curve.rotation=placement_euler_degrees_from_rotation_matrix(placement_rotation_matrix_multiply(
        matrix,placement_rotation_matrix_from_euler_degrees(source.rotation)));curve.absolute_rotation=curve.rotation;
    return curve;
}
kernel::ViewerReferenceGeometry general_surface_local_reference_geometry(const HistoryContainer& feature,kernel::ViewerReferenceGeometry geometry) {
    const auto& p=feature.placement;
    return transform_reference_geometry(std::move(geometry),{p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z});
}
kernel::ViewerMesh general_surface_place_definition_mesh(const Placement& placement,kernel::ViewerMesh mesh) {
    return body_placed_mesh(std::move(mesh),BodyPlacement{{},placement});
}
kernel::BoundarySurfaceRequest general_surface_request(const PartDocument& part,const HistoryContainer& source) {
    validate_general_surface(source);auto feature=source;reframe_general_surface(feature);
    // Small operation-local carrier: only owned definitions, no copied body,
    // cached mesh or history. The existing exact adapter never calculates OCCT.
    PartDocument carrier;carrier.document_precision=part.document_precision;
    auto fill=feature;fill.feature_kind=FeatureKind::BoundarySurface;fill.boundary_surface.boundaries.clear();
    for(const auto& boundary:feature.general_surface.boundaries) {
        std::string owner;
        if(boundary.curve) {
            auto curve=general_surface_display_curve(feature,*boundary.curve);owner=curve.id;
            carrier.constructions.push_back(std::move(curve));carrier.history_order.push_back({PartHistoryKind::Construction,owner});
        }else {
            auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);owner=sketch.id;
            HistoryContainer sketch_container;sketch_container.id=owner;
            sketch_container.feature_kind=FeatureKind::Sketch;sketch.owner_container_id=owner;
            carrier.history.push_back(std::move(sketch_container));carrier.sketches.push_back(std::move(sketch));
            carrier.history_order.push_back({PartHistoryKind::Feature,owner});
        }
        fill.boundary_surface.boundaries.push_back({owner,{}});
    }
    carrier.history_order.push_back({PartHistoryKind::Feature,feature.id});
    return boundary_surface_request(carrier,fill);
}
kernel::ViewerMesh general_surface_definition_mesh(const HistoryContainer& source) {
    auto feature=source;reframe_general_surface(feature);kernel::ViewerMesh result;
    for(const auto& boundary:feature.general_surface.boundaries) {
        kernel::ViewerMesh mesh;
        if(boundary.curve) {
            PartDocument carrier;carrier.constructions.push_back(general_surface_display_curve(feature,*boundary.curve));
            mesh=carrier.construction_viewer_mesh();
        }else if(!boundary.sketch_serialized.empty())mesh=sketcher::Sketch::from_serialized(boundary.sketch_serialized).viewer_mesh();
        append_reference_geometry(result.original_references,mesh.original_references);
        result.edges.insert(result.edges.end(),std::make_move_iterator(mesh.edges.begin()),std::make_move_iterator(mesh.edges.end()));
        result.points.insert(result.points.end(),std::make_move_iterator(mesh.points.begin()),std::make_move_iterator(mesh.points.end()));
        result.axes.insert(result.axes.end(),std::make_move_iterator(mesh.axes.begin()),std::make_move_iterator(mesh.axes.end()));
        result.dimensions.insert(result.dimensions.end(),std::make_move_iterator(mesh.dimensions.begin()),std::make_move_iterator(mesh.dimensions.end()));
    }
    return result;
}
