// Owned native definitions reuse the ordinary Sketch/Curve3D adapters. No OCCT.
GeneralSurfaceBoundary create_general_surface_sketch(const HistoryContainer& parent) {
    auto sketch=sketcher::Sketch::create_default();
    auto feature=PartDocument::create_feature_container(sketch.id);
    feature.feature.type=FeatureType::Sketch;feature.name=sketch.name;feature.feature.automatic_name=sketch.name;
    sketch.owner_container_id=feature.id;sketch.plane_reference_owner_id=feature.container_origin.id;
    return {sketch.serialized(),std::nullopt,std::move(feature),parent.id};
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
        if(boundary.sketch_serialized.empty()==!boundary.curve || boundary.sketch_feature.has_value()==boundary.curve.has_value())
            throw std::invalid_argument("Invalid owned surface boundary.");
        if(boundary.curve) {
            const auto& curve=*boundary.curve;
            if(curve.kind!=ConstructionKind::Curve3D||curve.parent_construction_id!=feature.id||curve.suppressed||
                curve.origin!=kernel::Vec3{}||curve.rotation!=kernel::Vec3{}||curve.absolute_rotation!=kernel::Vec3{}||
                !curve.references.empty()||curve.orientation_back||curve.orientation_quarter_turns!=0||
                curve.rotation_offset_x!=0||curve.rotation_offset_y!=0||curve.rotation_offset_z!=0)
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
            const auto& owned=*boundary.sketch_feature;
            if(boundary.sketch_parent_id!=feature.id||owned.feature_kind!=FeatureKind::Feature||owned.feature.type!=FeatureType::Sketch||
                owned.feature.sketch_id!=sketch.id||owned.feature.profile_source!=ProfileSource::Internal||
                owned.feature_parent_id!=owned.id||owned.container_origin!=create_container_origin(owned.id)||
                !owned.general_surface.boundaries.empty()||owned.suppressed||
                sketch.owner_container_id!=owned.id||sketch.plane_reference_owner_id!=owned.container_origin.id||sketch.suppressed)
                throw std::invalid_argument("Invalid owned surface boundary.");
            unique(owned.id);unique(owned.feature_id);unique(owned.container_origin.id);
            for(const auto& child:owned.container_origin.children)unique(child.id);
            unique(sketch.id);sketch.validate();validate_feature_parameters(owned.feature);
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
            else {
                const auto& owned=*boundary.sketch_feature;
                for(const auto& id:{owned.id,owned.feature_id,owned.container_origin.id,
                        sketcher::Sketch::from_serialized(boundary.sketch_serialized).id})
                    if(!occupied.insert(id).second)throw std::invalid_argument("Invalid owned surface boundary.");
                for(const auto& child:owned.container_origin.children)
                    if(!occupied.insert(child.id).second)throw std::invalid_argument("Invalid owned surface boundary.");
            }
        }
    }
}
void reframe_general_surface(HistoryContainer& feature) {
    for(auto& boundary:feature.general_surface.boundaries)
        if(boundary.sketch_feature&&boundary.sketch_feature->placement.references.empty())
            resolve_general_surface_sketch(boundary,{});
}
void resolve_general_surface_sketch(GeneralSurfaceBoundary& boundary,const kernel::ViewerReferenceGeometry& local_geometry) {
    if(!boundary.sketch_feature)throw std::invalid_argument("Invalid owned surface boundary.");
    PartDocument carrier;carrier.history={*boundary.sketch_feature};
    auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
    carrier.history.front().feature.profile_plane_offset=sketch.plane_offset;
    carrier.sketches={std::move(sketch)};
    carrier.resolve_constructions(local_geometry);
    *boundary.sketch_feature=std::move(carrier.history.front());
    boundary.sketch_serialized=carrier.sketches.front().serialized();
}
sketcher::Sketch general_surface_display_sketch(const HistoryContainer& parent,const GeneralSurfaceBoundary& boundary) {
    auto sketch=sketcher::Sketch::from_serialized(boundary.sketch_serialized);
    const auto& p=parent.placement;
    const auto rotation=placement_rotation_matrix_from_euler_degrees({p.rotation_x,p.rotation_y,p.rotation_z});
    sketch.resolved_origin=placement_transform_point(rotation,{p.x,p.y,p.z},sketch.resolved_origin);
    sketch.resolved_x_axis=placement_transform_direction(rotation,sketch.resolved_x_axis);
    sketch.resolved_y_axis=placement_transform_direction(rotation,sketch.resolved_y_axis);
    sketch.resolved_normal=placement_transform_direction(rotation,sketch.resolved_normal);
    return sketch;
}
kernel::ViewerReferenceGeometry general_surface_boundary_reference_geometry(const HistoryContainer& parent,
        std::size_t before,const kernel::ViewerReferenceGeometry& world_geometry) {
    auto local=general_surface_local_reference_geometry(parent,world_geometry);
    std::set<std::string> excluded{parent.id,parent.feature_id,parent.container_origin.id};
    for(const auto& boundary:parent.general_surface.boundaries) {
        if(boundary.sketch_feature) {
            const auto& owned=*boundary.sketch_feature;
            excluded.insert(owned.id);excluded.insert(owned.feature_id);excluded.insert(owned.container_origin.id);
            excluded.insert(owned.feature.sketch_id);
            for(const auto& child:owned.container_origin.children)excluded.insert(child.id);
        }else if(boundary.curve) {
            const auto collect=[&](auto&& self,const ConstructionObject& curve)->void {
                excluded.insert(curve.id);excluded.insert(curve.entity_id);excluded.insert(curve.container_origin.id);
                for(const auto& child:curve.container_origin.children)excluded.insert(child.id);
                for(const auto& point:curve.curve_points)self(self,point);
            };collect(collect,*boundary.curve);
        }
    }
    for(auto& ref:local.triangle_references)if(excluded.contains(ref.owner_id))ref={};
    std::erase_if(local.edges,[&](const auto& edge){return excluded.contains(edge.reference.owner_id);});
    std::erase_if(local.points,[&](const auto& point){return excluded.contains(point.reference.owner_id);});
    std::erase_if(local.axes,[&](const auto& axis){return excluded.contains(axis.reference.owner_id);});
    PartDocument frame;frame.history={parent};frame.history.front().placement={};
    append_reference_geometry(local,frame.history_origin_reference_geometry_before({}));
    for(std::size_t i=0;i<std::min(before,parent.general_surface.boundaries.size());++i) {
        const auto& boundary=parent.general_surface.boundaries[i];
        if(boundary.sketch_feature) {
            frame.history={*boundary.sketch_feature};
            append_reference_geometry(local,frame.history_origin_reference_geometry_before({}));
            append_reference_geometry(local,sketcher::Sketch::from_serialized(boundary.sketch_serialized).placement_reference_geometry());
        }else if(boundary.curve) {
            frame.history.clear();frame.constructions={*boundary.curve};frame.constructions.front().parent_construction_id.clear();
            append_reference_geometry(local,frame.construction_viewer_mesh().original_references);
        }
    }
    return local;
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
            auto sketch=general_surface_display_sketch(feature,boundary);owner=sketch.id;
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
        }else if(!boundary.sketch_serialized.empty()) {
            mesh=general_surface_display_sketch(feature,boundary).viewer_mesh();
            PartDocument carrier;carrier.history={*boundary.sketch_feature};
            carrier.sketches={general_surface_display_sketch(feature,boundary)};
            auto marker=carrier.feature_result_mesh(*boundary.sketch_feature);
            append_reference_geometry(mesh.original_references,std::move(marker.original_references));
            mesh.points.insert(mesh.points.end(),marker.points.begin(),marker.points.end());
            auto references=carrier.history_origin_reference_geometry_before({});
            const auto& p=feature.placement;
            append_reference_geometry(mesh.original_references,transform_reference_geometry(std::move(references),
                {p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z},false));
        }
        append_reference_geometry(result.original_references,mesh.original_references);
        result.edges.insert(result.edges.end(),std::make_move_iterator(mesh.edges.begin()),std::make_move_iterator(mesh.edges.end()));
        result.points.insert(result.points.end(),std::make_move_iterator(mesh.points.begin()),std::make_move_iterator(mesh.points.end()));
        result.axes.insert(result.axes.end(),std::make_move_iterator(mesh.axes.begin()),std::make_move_iterator(mesh.axes.end()));
        result.dimensions.insert(result.dimensions.end(),std::make_move_iterator(mesh.dimensions.begin()),std::make_move_iterator(mesh.dimensions.end()));
    }
    return result;
}
