void verify_inspection_reordering(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","inspection-history"}});
    const auto document=live.active_document_id();auto* state=live.open_part(document);
    const auto block=[&](const char* size) {
        return test::rectangular_commands([&](const char* n,Json a){return run(host,n,std::move(a));},
            {{"length_mm",size},{"width_mm",size},{"height_mm",size}}).data.at("container").get<std::string>();
    };
    const auto first=block("4");
    const auto mass=run(host,"body_properties.create",{{"name","Historical mass"}}).data.at("object").get<std::string>();
    auto edit=workspace::prepare_measurement_edit(live,document);auto measure=edit.initial;
    measure.references={{kernel::MeasurementKind::Object,first,{},{}}};
    require(workspace::commit_measurement(live,edit,measure),"Measurement fixture did not commit");
    const auto second=block("10");
    const auto anchor=[&](const std::string& id) {
        for(const auto& row:state->session.document().body_properties)if(row.id==id)return row.after_object_id;
        for(const auto& row:state->session.document().measurements)if(row.id==id)return row.after_object_id;
        throw std::runtime_error("Missing moved inspection row");
    };
    const auto snapshot=state->session.document().serialized();
    const auto revision=state->session.revision();
    const auto* cache=state->session.calculated_boundaries().data();
    require(run(host,"history.can_move",{{"object",second},{"before",mass}}).data.at("would_change")==true,
        "Feature cannot pass a body measurement");
    require(state->session.revision()==revision&&cache==state->session.calculated_boundaries().data(),
        "Inspection move query changed the model");
    run(host,"history.move",{{"object",second},{"before",mass}});
    require(anchor(mass)==second&&anchor(measure.id)==second&&state->session.revision()==revision+1,
        "Feature did not pass both inspection rows in one transaction");
    volume(kernel,*state,1000);
    auto mass_edit=workspace::prepare_body_properties_edit(live,document,mass);
    workspace::calculate_body_properties(live,kernel,document,mass_edit.initial);
    require(std::abs(workspace::prepare_body_properties_edit(live,document,mass).initial.volume-1000)<1e-6,
        "Moved body measurement retained its old history boundary");
    run(host,"undo");
    require(state->session.document().serialized()==snapshot,"Inspection crossing Undo lost authored state");
    run(host,"redo");require(anchor(mass)==second&&anchor(measure.id)==second,"Inspection crossing Redo failed");
    run(host,"history.move",{{"object",second}});
    require(anchor(mass)==first&&anchor(measure.id)==first,"Feature could not move back across inspection rows");
    run(host,"history.move",{{"object",mass}});
    require(anchor(mass)==second&&anchor(measure.id)==first,"Moving mass record changed an unrelated measurement");
    run(host,"history.move",{{"object",measure.id}});
    require(anchor(measure.id)==second,"Ordinary measurement could not move to the end");
    const auto stable=state->session.revision();
    require(!run(host,"history.move",{{"object",measure.id}}).data.at("changed").get<bool>()&&state->session.revision()==stable,
        "Unchanged measurement move created Undo history");
    require(!run(host,"history.move",{{"object",mass}}).data.at("changed").get<bool>()&&state->session.revision()==stable,
        "Moving inside a fixed inspection-family grouping created a false Undo step");
    require(host.execute_text("history.move "+measure.id+" "+first).code=="history_dependency"&&
        state->session.revision()==stable,"Measurement moved before its geometric source");
    // A real centroid dependency blocks the reverse crossing, unlike a passive row.
    auto dependent=state->session.document();
    document::ConstructionReference reference;reference.owner_id=mass+":origin";reference.semantic_key="origin:plane:xy";
    dependent.find_container(second)->placement.references.push_back(reference);
    for(auto& row:dependent.body_properties)if(row.id==mass)row.after_object_id=first;
    state->session.commit(std::move(dependent),state->session.calculated_boundaries());
    const auto dependency_revision=state->session.revision();
    require(host.execute_text("history.can_move "+second+" "+mass).code=="history_dependency"&&
        host.execute_text("history.move "+second+" "+mass).code=="history_dependency"&&
        state->session.revision()==dependency_revision,"Feature crossed its own centroid dependency");
    run(host,"undo");run(host,"save");
    const auto reopened=document::PartDocument::load(directory/"inspection-history.prtz");
    require(reopened.measurements==state->session.document().measurements&&
        reopened.body_properties.front().after_object_id==anchor(mass),"Moved inspection anchors did not survive reopening");
    const auto other=run(host,"body.create",{{"name","Other"}}).data.at("body").get<std::string>();
    const auto other_feature=block("2");const auto other_revision=state->session.revision();
    require(host.execute_text("history.move "+mass+" "+other_feature).code=="inactive_body"&&
        state->session.revision()==other_revision,"Inspection row escaped its inactive Body");
    run(host,"body.activate",{{"body",state->session.document().body_history.owner(first)->scope.id}});
    require(host.execute_text("history.move "+measure.id+" "+other_feature).code=="invalid_history_move",
        "Measurement moved to a different Body");
    std::cout<<"Features and inspection rows reorder with dependency, analysis, Undo/Redo and native persistence checks\n";
}
