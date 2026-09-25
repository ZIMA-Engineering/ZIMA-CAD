void cylinder_axis_verify(const kernel::OcctKernel& kernel, fs::path directory) {
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
    auto axis=document::PartDocument::create_construction(document::ConstructionKind::Axis);
    axis.definition=document::ConstructionDefinition::CylinderAxis;
    axis.references={{"",face->owner_id,face->semantic_key}};
    const auto shape=part->session.calculated_boundaries().back().kernel_shape;
    require(workspace::commit_construction(live,id,axis,workspace::ConstructionEditMode::Create),"Cannot commit cylinder axis");
    const auto current=[&]{return *part->session.document().find_construction(axis.id);};
    require(current().reference_valid&&std::abs(current().origin.z-10)<1e-6&&
        std::abs(std::abs(current().direction.z)-1)<1e-6,"Cylinder axis is not centred and coaxial");
    require(part->session.calculated_boundaries().back().kernel_shape==shape,"Axis creation recalculated the body");
    const auto idle=part->session.document().construction_viewer_mesh();
    const auto axis_point=[&](const auto& point){return point.reference.owner_id==current().container_origin.id && point.reference.semantic_key=="point";};
    require(std::ranges::none_of(idle.points,axis_point),"Idle cylinder axis displays an unwanted point");
    require(std::ranges::any_of(idle.original_references.points,axis_point),"Cylinder axis lost its original point reference");
    require(std::ranges::any_of(idle.axes,[&](const auto& item){return item.reference.owner_id==current().entity_id;}),
        "Hiding cylinder axis point also hid its axis");
    const auto identity=current().entity_id;
    command_host::Host host(live,kernel,directory);
    run(host,"undo"); require(!part->session.document().find_construction(axis.id),"Axis Undo retained a container");
    run(host,"redo"); require(current().entity_id==identity,"Axis Redo changed identity");
    auto moved=source; moved.placement.x=31; moved.placement.y=-17; moved.placement.z=9;
    moved.placement.rotation_x=37; moved.placement.absolute_rotation_x=37;
    static_cast<void>(workspace::commit_imported_feature(live,kernel,id,moved));
    const double radians=37*std::numbers::pi/180;
    require(current().reference_valid&&std::abs(current().origin.x-31)<1e-6&&
        std::abs(current().origin.y-(-17-10*std::sin(radians)))<1e-6&&
        std::abs(current().origin.z-(9+10*std::cos(radians)))<1e-6&&
        std::abs(std::abs(current().direction.y)-std::sin(radians))<1e-6,
        "Cylinder axis did not follow translated and rotated import");
    const auto native=directory/"axis.prtz";
    part->session.document().save(native,part->session.calculated_boundaries());
    std::vector<kernel::BodyResult> loaded_cache;
    auto loaded=document::PartDocument::load(native,&loaded_cache);
    require(*loaded.find_construction(axis.id)==current(),"Native save lost cylinder axis definition");
    loaded.resolve_constructions(loaded_cache.back().mesh.original_references);
    require(loaded.find_construction(axis.id)->reference_valid,"Reopened cylinder axis cannot resolve");
    auto invalid=current(); const auto position=invalid.origin;
    require(!document::resolve_construction(invalid,{})&&invalid.origin==position,
        "Missing cylindrical face silently moved the axis");
    auto wrong=geometry;
    for(auto& ref:wrong.triangle_references)if(ref.surface) {
        auto plane=std::make_shared<kernel::SurfaceGeometry>(*ref.surface);
        plane->kind=kernel::SurfaceGeometry::Kind::Plane;ref.surface=std::move(plane);
    }
    require(!document::resolve_construction(invalid,wrong),"Non-cylindrical face was accepted");
    // Repeated Assembly occurrences keep distinct source paths.
    assembly::AssemblyDocument group=assembly::AssemblyDocument::create_default();
    auto one=assembly::AssemblyDocument::create_part_occurrence("One",id,native,loaded_cache.back());
    auto two=assembly::AssemblyDocument::create_part_occurrence("Two",id,native,loaded_cache.back());two.placement.x=100;
    group.components={one,two};
    const auto scene=group.build_scene();
    const auto occurrence_face=std::ranges::find_if(scene.original_references.triangle_references,[&](const auto& ref) {
        return ref.surface&&ref.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder&&
            ref.instance_path==assembly::InstancePath{{two.occurrence_id}}.encoded();
    });
    require(occurrence_face!=scene.original_references.triangle_references.end(),"Repeated cylinder occurrence is missing");
    auto assembly_axis=axis;assembly_axis.references={{occurrence_face->instance_path,occurrence_face->owner_id,occurrence_face->semantic_key}};
    group.constructions={assembly_axis};group.resolve_constructions();
    require(group.constructions.front().reference_valid&&std::abs(group.constructions.front().origin.x-131)<1e-6,
        "Cylinder axis resolved the wrong Assembly occurrence");
    const auto assembly_file=directory/"axis.asmz";group.save(assembly_file);
    require(assembly::AssemblyDocument::load(assembly_file).constructions.front().definition==document::ConstructionDefinition::CylinderAxis,
        "Assembly persistence lost cylinder axis definition");
    // Cylindrical faces feed the exact same placement equations as axes.
    group.constructions.clear();
    group.components[0].grounded=true;
    group.components[1].placement.rotation_y=23;
    const auto face_reference=[&](const std::string& occurrence) {
        return assembly::MateReference{assembly::MateReferenceKind::CylinderFace,
            assembly::InstancePath{{occurrence}},face->owner_id,face->semantic_key};
    };
    const auto moving=face_reference(two.occurrence_id), fixed=face_reference(one.occurrence_id);
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
