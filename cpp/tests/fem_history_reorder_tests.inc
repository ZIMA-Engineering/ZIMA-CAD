void verify_fem_reordering(const kernel::OcctKernel& kernel,fs::path directory) {
    workspace::Workspace live;
    command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","fem-order"}});
    const auto id=live.active_document_id();
    test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},
        {{"length_mm","6"},{"width_mm","4"},{"height_mm","2"}});
    auto& session=live.open_part(id)->session;
    const auto source=session.document().body_history.active_body_id();
    run(host,"body.activate",{{"body",""}});
    const auto edit=workspace::prepare_fem_edit(session.document());
    document::BodyHistory fem;fem.scope.id=edit.object_id;fem.name="Analysis";
    fem.fem=document::FemDefinition{source};
    require(workspace::commit_fem(live,edit,fem),"FEM fixture did not commit");
    run(host,"body.activate",{{"body",""}});
    const auto mirror=run(host,"mirror.create",{{"source",source},{"local_plane","yz"},
        {"placement",{{"x",-10}}}}).data.at("object").get<std::string>();
    const auto original_order=session.document().body_history.order();
    const auto* cache=&session.calculated_boundaries();
    const auto source_snapshot=workspace::read_fem_source(session,fem.scope.id);
    const auto count=kernel::body_calculation_count.load();
    const auto revision=session.revision();
    run(host,"history.can_move",{{"object",mirror},{"before",fem.scope.id}});
    require(session.revision()==revision,"Reorder query changed revision");
    run(host,"history.move",{{"object",mirror},{"before",fem.scope.id}});
    require(session.document().body_history.order()==std::vector<std::string>{source,mirror,fem.scope.id},
        "Mirror did not move across FEM");
    require(session.revision()==revision+1&&cache==&session.calculated_boundaries()&&
        kernel::body_calculation_count.load()==count,"FEM crossing recalculated unchanged geometry");
    require(workspace::read_fem_source(session,fem.scope.id).shares_with(source_snapshot),
        "FEM crossing replaced the calculated source");
    run(host,"undo");require(session.document().body_history.order()==original_order,"FEM move Undo lost order");
    run(host,"redo");
    run(host,"history.move",{{"object",mirror},{"before",fem.scope.id}});
    require(session.revision()==revision+1,"Unchanged order created another transaction");
    require(!host.execute({{"command","history.move"},{"arguments",{{"object",mirror},{"before",source}}}}).ok&&
        !host.execute({{"command","history.move"},{"arguments",{{"object",fem.scope.id},{"before",source}}}}).ok,
        "FEM optimization bypassed source dependencies");
    require(cache==&session.calculated_boundaries()&&kernel::body_calculation_count.load()==count,
        "Undo/Redo or rejected order recalculated geometry");
    const auto path=directory/"fem-order.prtz";
    session.document().save(path,session.calculated_boundaries());
    std::vector<kernel::BodyResult> reopened_cache;
    const auto reopened=document::PartDocument::load(path,&reopened_cache);
    require(reopened.body_history==session.document().body_history&&!reopened_cache.empty()&&
        reopened_cache.back().source_fingerprint==cache->back().source_fingerprint,
        "FEM reorder save/reopen lost order or calculated geometry");
    auto fresh_document=reopened;
    auto fresh=workspace::calculate_part_with_resolved_references(kernel,fresh_document);
    workspace::ReferenceIndex retained_refs,fresh_refs;
    retained_refs.add_geometry(cache->back().mesh.original_references);
    fresh_refs.add_geometry(fresh.back().mesh.original_references);
    auto retained=cache->back();kernel.ensure_body_properties(retained);kernel.ensure_body_properties(fresh.back());
    require(retained_refs.keys==fresh_refs.keys&&std::abs(retained.volume-fresh.back().volume)<1e-7,
        "Reused reorder differs from a fresh calculation");
    const auto fresh_count=kernel::body_calculation_count.load();
    run(host,"history.move",{{"object",fem.scope.id},{"before",mirror}});
    require(session.document().body_history.order()==original_order&&cache==&session.calculated_boundaries()&&
        kernel::body_calculation_count.load()==fresh_count,"Reverse FEM crossing recalculated geometry");
}
