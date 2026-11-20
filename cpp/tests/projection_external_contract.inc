void verify_external_projection() {
    const auto directory=std::filesystem::temp_directory_path()/("zima-projection-external-"+document::PartDocument::create_default().document_id);
    std::filesystem::create_directory(directory);
    for(bool nested:{false,true})for(auto output:{document::ProjectionOutput::Curves,document::ProjectionOutput::Surface}) {
        kernel::OcctKernel engine;
        const auto fixture=[](double height,double body_z) {
            auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
            static_cast<void>(sketch.add_rectangle(-30,-30,30,30));auto feature=document::PartDocument::create_feature_container(sketch.id);
            feature.feature.sides[0].length=height;sketch.owner_container_id=feature.id;part.sketches={sketch};part.history={feature};
            document::BodyHistoryGraph graph;const auto id=graph.create_body("Projection test Body");graph.insert({document::PartHistoryKind::Feature,feature.id});
            auto body=*graph.find(id);body.scope.placement.z=body_z;body.scope.placement.rotation_z=body.scope.placement.absolute_rotation_z=17.;
            graph.update_body(body);part.set_body_history(graph);part.resolve_constructions();return part;
        };
        auto source=fixture(20,4),part=fixture(1,2);
        auto source_cache=workspace::calculate_part_with_resolved_references(engine,source);
        auto cache=workspace::calculate_part_with_resolved_references(engine,part);
        const auto source_file=directory/(source.document_id+"_SKELETON.prtz"),part_file=directory/(part.document_id+".prtz");
        source.save(source_file,source_cache);part.save(part_file,cache);
        kernel::FaceReference target;
        for(const auto& face:source_cache.back().mesh.triangle_references)if(face.semantic_key.starts_with("end:from:"))target=face;
        require(target.valid(),"External Projection source cap missing");
        auto assembly=assembly::AssemblyDocument::create_default();
        auto fixed=assembly::AssemblyDocument::create_part_occurrence("Skeleton",source.document_id,source_file,source_cache.back());
        auto moving=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,part_file,cache.back());
        for(auto* occurrence:{&fixed,&moving}) {
            occurrence->placement.rotation_x=30;occurrence->placement.rotation_y=20;occurrence->placement.rotation_z=40;
        }
        const double rx=30*std::numbers::pi/180,ry=20*std::numbers::pi/180,rz=40*std::numbers::pi/180;
        const double x=40*std::cos(rx)*std::sin(ry),y=-40*std::sin(rx);
        const kernel::Vec3 displacement{x*std::cos(rz)-y*std::sin(rz),x*std::sin(rz)+y*std::cos(rz),40*std::cos(rx)*std::cos(ry)};
        fixed.placement.x=displacement.x;fixed.placement.y=displacement.y;fixed.placement.z=displacement.z;
        assembly.components={fixed,moving};
        auto source_path=assembly::InstancePath{}.child(fixed.occurrence_id),dependent_path=assembly::InstancePath{}.child(moving.occurrence_id);
        if(nested) {
            const auto file=directory/(assembly.document_id+".asmz");assembly.save(file);
            auto parent=assembly::AssemblyDocument::create_default();
            auto wrapper=assembly::AssemblyDocument::create_assembly_occurrence("Rotated wrapper",assembly.document_id,file,assembly);
            wrapper.placement.x=51;wrapper.placement.y=-29;wrapper.placement.z=13;
            wrapper.placement.rotation_x=-20;wrapper.placement.rotation_y=31;wrapper.placement.rotation_z=14;
            parent.components={wrapper};source_path=assembly::InstancePath{}.child(wrapper.occurrence_id).child(fixed.occurrence_id);
            dependent_path=assembly::InstancePath{}.child(wrapper.occurrence_id).child(moving.occurrence_id);assembly=std::move(parent);
        }
        workspace::Workspace live;live.add_part(part,cache,part_file);live.add_assembly(assembly,directory/(assembly.document_id+".asmz"));live.activate(assembly.document_id);
        static_cast<void>(live.activate_occurrence(assembly.document_id,dependent_path));
        auto feature=part.history.front();feature.feature.type=document::FeatureType::Projection;feature.feature.projection.output=output;
        auto sketch=part.sketches.front();sketch.segments.clear();sketch.points.clear();sketch.constraints.clear();static_cast<void>(sketch.add_circle(0,0,4));
        document::ExtrusionParameters::EndTarget requested;requested.reference=target;requested.reference.instance_path=source_path.encoded();
        const auto before=live.open_part(part.document_id)->session.document().serialized();const auto calls=kernel::body_calculation_count.load();
        const auto pending=workspace::prepare_skeleton_end_target(live,part,feature.id,requested,nullptr,true);
        require(pending.reference==requested.reference&&pending.kind==document::EndTargetKind::Face,"Projection picking changed the bounded source identity");
        require(live.open_part(part.document_id)->session.document().serialized()==before&&kernel::body_calculation_count.load()==calls,"External Projection picking calculated or changed the document");
        feature.feature.projection.targets={pending};
        if(output==document::ProjectionOutput::Surface)feature.feature.projection.regions={{pending.reference,{0,0,62},{}}};
        std::cout<<"External Projection "<<static_cast<int>(output)<<" nested="<<nested<<": commit"<<std::endl;
        workspace::commit_profile(live,engine,part.document_id,feature,workspace::ProfileEditMode::Replace,sketch);
        auto* current=live.open_part(part.document_id);
        const auto verify=[&](double expected) {
            const auto& result=current->session.calculated_boundaries().back();require(result.calculation_errors.empty(),"External Projection failed");
            bool measured=false;
            for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id)
                for(const auto& point:edge.points) {
                    // Native output is published in Part coordinates (Body Z=2).
                    require(std::abs(point.z-(expected+2))<1e-6&&std::abs(std::hypot(point.x,point.y)-4)<1e-6,"Projection violated the independent occurrence/Body equation");measured=true;
                }
            require(measured,"External Projection has no native output edges");
        };
        verify(62);
        std::cout<<"External Projection: no-op and rejection"<<std::endl;
        const auto stored=current->session.document().serialized();const auto count=kernel::body_calculation_count.load();
        workspace::commit_profile(live,engine,part.document_id,*current->session.document().find_container(feature.id),workspace::ProfileEditMode::Replace);
        require(current->session.document().serialized()==stored&&kernel::body_calculation_count.load()==count,"External Projection unchanged OK calculated or changed history");
        auto invalid=*current->session.document().find_container(feature.id);invalid.feature.projection.output=document::ProjectionOutput::Split;
        bool rejected=false;try {workspace::commit_profile(live,engine,part.document_id,invalid,workspace::ProfileEditMode::Replace);}catch(const std::exception&){rejected=true;}
        require(rejected&&current->session.document().serialized()==stored,"External Split mutated a read-only source or dependent document");
        require(current->session.undo()&&current->session.redo()&&current->session.document().serialized()==stored,"External Projection Undo/Redo changed identity");
        std::cout<<"External Projection: save/reopen"<<std::endl;
        current->session.document().save(part_file,current->session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened_cache;
        const auto reopened=document::PartDocument::load(part_file,&reopened_cache);kernel::OcctKernel cold;
        require(workspace::calculate_part(cold,reopened).back().calculation_errors.empty(),"External Projection cannot reopen using its native embedded source");
        source.history.front().feature.sides[0].length=25;
        source_cache=workspace::calculate_part_with_resolved_references(engine,source,&source_cache);live.add_part(source,source_cache,source_file);
        current=live.open_part(part.document_id);
        const auto immutable=live.open_part(source.document_id)->session.document().serialized();
        std::cout<<"External Projection: unsaved source regeneration"<<std::endl;
        static_cast<void>(workspace::regenerate_part(live,engine,part.document_id));verify(67);
        std::cout<<"External Projection: cycle and missing source"<<std::endl;
        require(live.open_part(source.document_id)->session.document().serialized()==immutable&&document::PartDocument::load(source_file).history.front().feature.sides[0].length==20,"Projection modified its read-only source");
        bool skeleton_rejected=false;try {workspace::require_acyclic_document_dependency(live,source.document_id,part.document_id,part_file);}catch(const workspace::DocumentDependencyError& error){skeleton_rejected=error.code=="skeleton_dependency";}
        require(skeleton_rejected,"Skeleton accepted an outgoing document dependency");
        // The Skeleton rule rejects earlier than cycle traversal. Exercise the
        // graph separately with an ordinary source role, without editing it.
        live.open_part(source.document_id)->path=directory/"ordinary-source.prtz";
        bool cycle=false;try {workspace::require_acyclic_document_dependency(live,source.document_id,part.document_id,part_file);}catch(const workspace::DocumentDependencyError& error){cycle=error.code=="dependency_cycle";}
        live.open_part(source.document_id)->path=source_file;
        require(cycle,"Projection target was omitted from the document dependency graph");
        auto missing=source;missing.history.front().feature.type=document::FeatureType::Sketch;
        std::cout<<"External Projection: calculate source without cap"<<std::endl;
        auto absent=workspace::calculate_part_with_resolved_references(engine,missing,&source_cache);
        live.open_part(source.document_id)->session.commit(std::move(missing),std::move(absent));
        const auto retained=current->session.document().serialized(current->session.calculated_boundaries());
        rejected=false;try {static_cast<void>(workspace::regenerate_part(live,engine,part.document_id));}catch(const std::exception&){rejected=true;}
        require(rejected&&current->session.document().serialized(current->session.calculated_boundaries())==retained,"Lost Projection target moved or replaced the last valid definition/result");
        std::cout<<"PASS external Projection output "<<static_cast<int>(output)<<" nested="<<nested<<": rotated Body/occurrence, read-only source, no-op, Undo/Redo, embedded native reopen, unsaved regeneration, cycle and reference loss\n";
    }
    require(std::filesystem::equivalent(directory.parent_path(),std::filesystem::temp_directory_path()),"Unsafe Projection fixture cleanup");std::filesystem::remove_all(directory);
}
