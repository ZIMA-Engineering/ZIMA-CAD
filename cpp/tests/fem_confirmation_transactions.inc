void fem_confirmation_transactions() {
    std::filesystem::create_directories("build");
    workspace::Workspace live;kernel::OcctKernel engine;
    auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Source"));part.set_body_history(graph);
    auto feature=test::rectangular_feature(part,{40,30,20});
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
    auto cache=workspace::calculate_part_with_resolved_references(engine,part);
    require(!cache.empty()&&cache.back().calculation_errors.empty(),"FEM confirmation source failed");
    const auto source_id=part.body_history.active_body_id();
    live.add_part(part,cache);live.activate(part.document_id);auto& session=live.open_part(part.document_id)->session;
    session.activate_body({});auto creation=workspace::prepare_fem_edit(session.document());
    document::BodyHistory body;body.scope.id=creation.object_id;body.name="Confirmation";body.fem=document::FemDefinition{source_id};
    require(workspace::commit_fem(live,creation,body),"FEM confirmation Body failed");
    const auto source=workspace::read_fem_source(session,body.scope.id);
    const auto calculation_count=kernel::body_calculation_count.load();
    for(const auto kind:{document::FemConditionKind::Constraint,document::FemConditionKind::Force,document::FemConditionKind::Pressure})
    for(const bool projecting:{false,true}) {
        document::FemCondition pending;pending.id="failed-"+std::to_string(int(kind))+"-"+std::to_string(projecting);pending.name="Pending";
        pending.kind=kind;pending.region=document::FemRegionKind::Surface;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=pending.id;sketch.plane_auto=false;sketch.plane_offset=100;
        static_cast<void>(sketch.add_circle(0,0,2));
        pending.projected_sketch=document::FemProjectedSketch{sketch.serialized(),{},{},projecting};
        if(projecting) {
            pending.placement.x=100;
            pending.projected_sketch->surface_seeds={{0,0,10}};
            const auto& mesh=source->mesh;
            for(std::size_t t=0;t<mesh.triangle_references.size();++t) {
                if(!std::ranges::all_of(std::views::iota(0,3),[&](int c){return std::abs(mesh.vertices[mesh.triangles[3*t+c]].z-10)<1e-7;}))continue;
                const auto& ref=mesh.triangle_references[t];pending.references={{ref.owner_id,ref.semantic_key,ref.instance_path}};break;
            }
            require(!pending.references.empty(),"Projected FEM confirmation has no top face");
        }
        const auto revision=session.revision();const auto before=session.document().serialized();
        const auto before_confirmation=kernel::body_calculation_count.load();
        workspace::ModelingConfirmation confirmation;
        const auto submit=[&](bool failure) {
            workspace::ModelingConfirmationAttempt attempt(confirmation,live,part.document_id);
            bool rejected=false;
            try {static_cast<void>(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),body.scope.id),pending));}
            catch(const std::exception&) {rejected=true;}
            require(rejected==failure&&attempt.failed()==failure,"FEM first/second OK acknowledgement failed");
            if(!failure)require(attempt.accepted(pending.id),"FEM second OK did not reuse its acknowledged definition");
        };
        submit(true);
        require(session.revision()==revision&&session.document().serialized()==before&&!confirmation.failure().empty(),"FEM first OK changed the document");
        pending.force_N=2000;submit(true);
        session.update_calculated_boundaries(session.calculated_boundaries());submit(true);
        submit(false);
        const auto saved=*workspace::find_fem_condition(session.document(),body.scope.id,pending.id);
        require(!saved.calculation_error.empty()&&saved.placement.reference_valid&&session.revision()==revision+1,
            "FEM second OK lost the error, changed placement validity or added extra transactions");
        require(saved.projected_sketch==pending.projected_sketch&&saved.references==pending.references,"FEM second OK changed authored geometry");
        rejects([&]{document::require_resolved_fem_conditions(*session.document().body_history.find(body.scope.id)->fem);});
        const auto shown=document::fem_condition_presentation(saved,source->mesh);
        require(shown.constraint_markers.front().label.ends_with("!")&&std::ranges::all_of(shown.edges,[](const auto& e){return e.color=="#FF5E52";}),"Unresolved FEM was not visibly broken");
        const auto file=std::filesystem::path("build")/(pending.id+"-confirmation.prtz");
        session.document().save(file,session.calculated_boundaries());
        std::vector<kernel::BodyResult> loaded_cache;auto reopened=document::PartDocument::load(file,&loaded_cache);
        require(*workspace::find_fem_condition(reopened,body.scope.id,pending.id)==saved,"FEM error or definition did not reopen");
        require(kernel::body_calculation_count.load()==before_confirmation,"FEM confirmation recalculated its CAD Body");
        const auto regenerated=workspace::calculate_part_with_resolved_references(engine,reopened);
        const auto after_regeneration=kernel::body_calculation_count.load();
        require(!regenerated.empty()&&*workspace::find_fem_condition(reopened,body.scope.id,pending.id)==saved,"Regeneration lost the unresolved FEM definition");
        require(session.undo()&&!workspace::find_fem_condition(session.document(),body.scope.id,pending.id)&&session.redo()&&
            *workspace::find_fem_condition(session.document(),body.scope.id,pending.id)==saved,"FEM unresolved Undo/Redo failed");
        pending=saved;pending.placement.x=0;sketch.plane_offset=projecting?30:5;pending.projected_sketch->serialized=sketch.serialized();
        require(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),body.scope.id),pending),"FEM repair did not commit");
        const auto fixed=*workspace::find_fem_condition(session.document(),body.scope.id,pending.id);
        require(fixed.calculation_error.empty(),"FEM repair retained its failure");
        document::require_resolved_fem_conditions(*session.document().body_history.find(body.scope.id)->fem);
        require(session.undo()&&!workspace::find_fem_condition(session.document(),body.scope.id,pending.id)->calculation_error.empty()&&session.redo(),"FEM repair Undo/Redo failed");
        require(workspace::read_fem_source(session,body.scope.id).shares_with(source),"FEM acknowledgement or repair replaced its CAD source");
        require(kernel::body_calculation_count.load()==after_regeneration,"FEM repair recalculated its CAD Body");
    }
    // The shared acknowledgement avoids reevaluation on the second identical
    // submission, but rejects stale input, even if a source changes in-place.
    workspace::ModelingConfirmation confirmation;document::FemCondition value;value.id="counted";value.name="Counted";
    int evaluations{};
    for(int i=0;i<2;++i) {
        workspace::ModelingConfirmationAttempt attempt(confirmation,live,part.document_id);
        try {attempt.validate(value,body.scope.id,[&](auto&){++evaluations;throw std::runtime_error("The condition placement is unresolved.");});}
        catch(const std::runtime_error&){require(i==0,"Second FEM OK recalculated its failure");}
        if(i==0)value.calculation_error.clear();
    }
    require(evaluations==1,"Second FEM OK repeated geometry validation");
    // Only the explicit regeneration above may calculate the CAD Body.
    require(kernel::body_calculation_count.load()>calculation_count,"FEM regeneration fixture was not exercised");
}
