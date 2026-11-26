void sphere_face_placement_verify(const kernel::OcctKernel& kernel, fs::path directory) {
    kernel::RevolutionRequest sphere;sphere.surface_result=true;sphere.axis_point={7,-3,11};
    sphere.axis_direction={0,1,0};sphere.profile_region_id="sphere-region";
    sphere.outer_profile=kernel::ExtrusionRequest::CurvedProfile{{kernel::ExtrusionRequest::ArcCurve{{7,-23,11},{27,-3,11},{7,17,11}}}};
    sphere.outer_edge_source_ids={"meridian"};sphere.outer_vertex_source_ids={"south"};sphere.open_profile_end_id="north";
    kernel::StepProduct product;product.definition_id="sphere-source";product.name="Sphere";
    product.body=kernel.evaluate_history({{"sphere",sphere}}).back();
    require(product.body.calculation_errors.empty(),"Spherical source calculation failed");
    const auto step=directory/"sphere.step";kernel.export_step(product,document::path_to_utf8(step));
    auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
    const auto native=directory/"sphere-source.prtz";imported.document.save(native,imported.calculated);
    const auto& faces=imported.calculated.back().mesh.original_references.triangle_references;
    const auto face=std::ranges::find_if(faces,[](const auto& f){const auto surface=f.surface?f.surface:f.contour_surface;return surface&&surface->kind==kernel::SurfaceGeometry::Kind::Sphere;});
    require(face!=faces.end(),"Spherical source lacks analytic metadata");
    auto group=assembly::AssemblyDocument::create_default();
    auto fixed=assembly::AssemblyDocument::create_part_occurrence("Fixed",imported.document.document_id,native,imported.calculated.back());
    auto moving=assembly::AssemblyDocument::create_part_occurrence("Moving",imported.document.document_id,native,imported.calculated.back());
    fixed.grounded=true;fixed.placement={35,14,-9,15,-22,31};moving.placement={100,-25,40,17,23,-36};
    group.components={fixed,moving};
    const auto ref=[&](const auto& occurrence){return assembly::MateReference{assembly::MateReferenceKind::SphereFace,assembly::InstancePath{{occurrence.occurrence_id}},face->owner_id,face->semantic_key};};
    assembly::ComponentPlacementReference row;row.mate_type=assembly::MateKind::PointCoincident;row.component_reference=ref(moving);row.target_reference=ref(fixed);
    group.components[1].placement_references={row};
    app::ComponentReferenceKindIndex index;index.rebuild(group.build_scene().original_references);
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Face;candidate.instance_path=row.component_reference.instance_path.encoded();candidate.owner_id=face->owner_id;candidate.semantic_key=face->semantic_key;
    require(index.kind(candidate)==assembly::MateReferenceKind::SphereFace,"Sphere picker classified the surface as planar");
    const auto verify_center=[&](auto& doc) {
        doc.calculate_placement_references();
        const auto a=doc.resolve_point(row.component_reference),b=doc.resolve_point(row.target_reference);
        require(a.status==assembly::MateStatus::Valid&&b.status==assembly::MateStatus::Valid,"Sphere center is unresolved");
        require(std::hypot(a.point.x-b.point.x,a.point.y-b.point.y,a.point.z-b.point.z)<1e-6,"Sphere centers do not coincide");
        require(doc.component_constraint_state(moving.occurrence_id).remaining_dof==3,"Spherical joint does not retain three rotational freedoms");
    };
    for(double angle:{-60.,30.,85.,0.}) {
        group.components[1].placement.rotation_x=angle;group.components[1].placement.rotation_z=angle/2;
        verify_center(group);
        const auto saved=directory/"sphere-mate.asmz";group.save(saved);auto reopened=assembly::AssemblyDocument::load(saved);
        require(reopened.components[1].placement_references==group.components[1].placement_references,"Spherical source identities changed during persistence");verify_center(reopened);
    }
    auto base=group;base.components[1].placement_references.clear();
    workspace::Workspace live;live.add_part(imported.document,imported.calculated,native);live.add_assembly(base,directory/"sphere-command.asmz");live.activate(base.document_id);live.display_top_level(base.document_id);
    command_host::Host host(live,kernel,directory);
    const auto json_ref=[](const assembly::MateReference& r){return Json{{"kind","sphere_face"},{"instance_path",r.instance_path.encoded()},{"owner",r.owner_id},{"key",r.semantic_key}};};
    const auto changed=run(host,"component.set",{{"instance_path",row.component_reference.instance_path.encoded()},{"placement_references",Json::array({{{"kind","point_coincident"},{"component",json_ref(row.component_reference)},{"target",json_ref(row.target_reference)}}})}});
    require(changed.data.at("placement_references")[0].at("component").at("kind")=="sphere_face","Component command lost spherical source type");
    run(host,"undo");require(live.open_assembly(base.document_id)->session.document().components[1].placement_references.empty(),"Sphere mate Undo failed");run(host,"redo");
    auto restored=live.open_assembly(base.document_id)->session.document();verify_center(restored);
}
