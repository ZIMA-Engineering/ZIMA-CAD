void cylinder_face_placement_verify(const kernel::OcctKernel& kernel, fs::path directory) {
    kernel::StepProduct product;
    product.definition_id="cylinder-source"; product.name="Cylinder";
    auto native_source=document::PartDocument::create_default();
    auto cylinder=zima::test::circular_feature(native_source,5,20);
    native_source.insert_history_entry(document::PartHistoryKind::Feature,cylinder.id);
    native_source.history.push_back(cylinder);
    product.body=kernel.evaluate_history(native_source.kernel_operations()).back();
    const auto step=directory/"cylinder.step";
    kernel.export_step(product,document::path_to_utf8(step));
    auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
    workspace::Workspace live;
    const auto id=imported.document.document_id;
    live.add_part(imported.document,imported.calculated,directory/"cylinder.prtz"); live.activate(id);
    auto* part=live.open_part(id);
    const auto source=part->session.document().history.front();
    const auto geometry=part->session.calculated_boundaries().back().mesh.original_references;
    const auto face=std::ranges::find_if(geometry.triangle_references,[](const auto& ref) {
        return ref.surface && ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder;
    });
    require(face!=geometry.triangle_references.end(),"Imported cylinder lacks persisted analytic data");
    const auto native=directory/"cylinder-source.prtz";
    const auto loaded_cache=part->session.calculated_boundaries();
    part->session.document().save(native,loaded_cache);
    assembly::AssemblyDocument group=assembly::AssemblyDocument::create_default();
    auto one=assembly::AssemblyDocument::create_part_occurrence("One",id,native,loaded_cache.back());
    auto two=assembly::AssemblyDocument::create_part_occurrence("Two",id,native,loaded_cache.back());
    two.placement.x=100;
    group.components={one,two};
    command_host::Host host(live,kernel,directory);
    // Cylindrical faces feed the exact same placement equations as axes.
    group.components[0].grounded=true;
    group.components[1].placement.rotation_y=23;
    const auto face_reference=[&](const std::string& occurrence) {
        return assembly::MateReference{assembly::MateReferenceKind::CylinderFace,
            assembly::InstancePath{{occurrence}},face->owner_id,face->semantic_key};
    };
    const auto moving=face_reference(two.occurrence_id), fixed=face_reference(one.occurrence_id);
    // A visible STEP face has its analytic definition in the original packet,
    // not necessarily on its display triangle. Classify the actual picker
    // identity before passing the row to the native placement equations.
    app::ComponentReferenceKindIndex kinds;
    const auto scene=group.build_scene();
    kinds.rebuild(scene.original_references);
    for(const auto& reference:{moving,fixed}) {
        const auto visible=std::ranges::find_if(scene.triangle_references,[&](const auto& value) {
            return value.instance_path==reference.instance_path.encoded() &&
                value.owner_id==reference.owner_id && value.semantic_key==reference.semantic_key;
        });
        require(visible!=scene.triangle_references.end(),"Imported cylinder has no visible face");
        require(!visible->surface,"STEP regression fixture unexpectedly duplicates surface metadata");
        viewer::ViewerCandidate picked;
        picked.kind=viewer::CandidateKind::Face;picked.geometry=viewer::CandidateGeometry::Display;
        picked.instance_path=reference.instance_path.encoded();picked.owner_id=reference.owner_id;
        picked.semantic_key=reference.semantic_key;
        require(kinds.kind(picked)==assembly::MateReferenceKind::CylinderFace,
            "Visible STEP cylinder was classified as a planar face");
        picked.geometry=viewer::CandidateGeometry::OriginalReference;
        require(kinds.kind(picked)==assembly::MateReferenceKind::CylinderFace,
            "Cylinder kind depends on the picker packet");
        picked.instance_path+="/missing";
        require(kinds.kind(picked)==assembly::MateReferenceKind::Face,
            "Cylinder lookup leaked into another occurrence");
    }
    kinds.rebuild({});
    viewer::ViewerCandidate stale;
    stale.kind=viewer::CandidateKind::Face;stale.instance_path=moving.instance_path.encoded();
    stale.owner_id=moving.owner_id;stale.semantic_key=moving.semantic_key;
    require(kinds.kind(stale)==assembly::MateReferenceKind::Face,"Rebuilt scene retained a removed cylinder");
    for (const bool flip : {false,true,false}) {
        auto test=group;
        assembly::ComponentPlacementReference row;
        row.mate_type=assembly::MateKind::AxisCoincident;
        row.component_reference=moving;row.target_reference=fixed;row.flip=flip;
        test.components[1].placement_references={row};
        auto equivalent=test;
        for(auto& component:equivalent.components) {
            auto body=component.calculated_source.get();
            auto& original=body.mesh.original_references;
            const auto ref=std::ranges::find_if(original.triangle_references,[&](const auto& f) {
                return f.owner_id==face->owner_id&&f.semantic_key==face->semantic_key;
            });
            require(ref!=original.triangle_references.end()&&ref->surface,"Axis comparison source missing");
            original.axes.push_back({ref->surface->origin,ref->surface->axis,20,
                {face->owner_id,"test:equivalent-axis",""}});
            component.calculated_source=std::move(body);
        }
        auto& equivalent_row=equivalent.components[1].placement_references[0];
        for(auto* ref:{&equivalent_row.component_reference,&equivalent_row.target_reference}) {
            ref->kind=assembly::MateReferenceKind::Axis;ref->semantic_key="test:equivalent-axis";
        }
        equivalent.calculate_placement_references();
        test.calculate_placement_references();
        const auto actual_pose=test.components[1].placement,expected_pose=equivalent.components[1].placement;
        require(std::abs(actual_pose.x-expected_pose.x)<1e-6&&std::abs(actual_pose.y-expected_pose.y)<1e-6&&
            std::abs(actual_pose.z-expected_pose.z)<1e-6&&std::abs(actual_pose.rotation_x-expected_pose.rotation_x)<1e-6&&
            std::abs(actual_pose.rotation_y-expected_pose.rotation_y)<1e-6&&std::abs(actual_pose.rotation_z-expected_pose.rotation_z)<1e-6,
            "Cylinder mate changed the existing axis-to-axis placement result");
        const auto a=test.resolve_axis(moving),b=test.resolve_axis(fixed);
        require(a.status==assembly::MateStatus::Valid&&b.status==assembly::MateStatus::Valid,"Cylinder mate cannot resolve");
        const auto delta=kernel::Vec3{a.axis.point.x-b.axis.point.x,a.axis.point.y-b.axis.point.y,a.axis.point.z-b.axis.point.z};
        const auto direction=b.axis.direction;
        const double axial=delta.x*direction.x+delta.y*direction.y+delta.z*direction.z;
        require(std::hypot(delta.x-axial*direction.x,delta.y-axial*direction.y,delta.z-axial*direction.z)<1e-5,
            "Cylinder mating axes are not coincident");
        const double dot=a.axis.direction.x*direction.x+a.axis.direction.y*direction.y+a.axis.direction.z*direction.z;
        require(std::abs(dot-(flip?-1:1))<1e-6,"Cylinder mate lost absolute Flip orientation");
        require(test.component_constraint_state(two.occurrence_id).remaining_dof==2,"Cylinder mate changed axial/rotational freedoms");
        auto mixed=equivalent;
        mixed.components[1].placement_references[0].component_reference=moving;
        mixed.calculate_placement_references();
        require(mixed.component_constraint_state(two.occurrence_id).remaining_dof==2,"Cylinder-to-axis mate changed mobility");
        auto missing=moving;missing.semantic_key+="-missing";
        require(test.resolve_axis(missing).status==assembly::MateStatus::MissingReference,"Cylinder mate accepted missing face");
        const auto saved=directory/"cylinder-mate.asmz";test.save(saved);
        auto reopened=assembly::AssemblyDocument::load(saved);
        require(reopened.components[1].placement_references[0]==row,"Cylinder mate persistence changed reference or Flip");
        reopened.calculate_placement_references();
        require(reopened.component_constraint_state(two.occurrence_id).remaining_dof==2,"Reopened cylinder mate changed mobility");
    }
    live.add_assembly(group,directory/"mate-command.asmz");live.activate(group.document_id);
    live.display_top_level(group.document_id);
    const auto json_ref=[](const assembly::MateReference& ref) {
        return Json{{"kind","cylinder_face"},{"instance_path",ref.instance_path.encoded()},{"owner",ref.owner_id},{"key",ref.semantic_key}};
    };
    const auto patch=Json{{"instance_path",moving.instance_path.encoded()},
        {"placement_references",Json::array({Json{{"kind","axis_coincident"},{"component",json_ref(moving)},{"target",json_ref(fixed)}}})}};
    const auto edited=run(host,"component.set",patch);
    require(edited.data.at("placement_references")[0].at("component").at("kind")=="cylinder_face",
        "Component command dropped cylinder face kind");
    run(host,"undo");require(live.open_assembly(group.document_id)->session.document().components[1].placement_references.empty(),"Cylinder mate Undo did not restore input");
    run(host,"redo");run(host,"save");
    require(live.open_assembly(group.document_id)->session.document().components[1].placement_references[0].component_reference==moving,
        "Cylinder mate Redo lost face identity");
}
