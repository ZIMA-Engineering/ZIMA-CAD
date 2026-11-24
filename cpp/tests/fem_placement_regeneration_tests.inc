void fem_placement_regeneration() {
    for(bool later_body:{false,true}) {
        workspace::Workspace live;kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();document::BodyHistoryGraph graph;
        const auto source_id=graph.create_body("Source");part.set_body_history(graph);
        auto feature=test::rectangular_feature(part,{40,30,20});
        part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);
        if(later_body) {
            graph=part.body_history;static_cast<void>(graph.create_body("Later material"));part.set_body_history(graph);
            auto second=test::rectangular_feature(part,{10,10,10});second.placement.x=100;
            part.insert_history_entry(document::PartHistoryKind::Feature,second.id);part.history.push_back(second);
        }
        auto cache=workspace::calculate_part_with_resolved_references(engine,part);
        live.add_part(part,std::move(cache));auto& session=live.open_part(part.document_id)->session;
        session.activate_body({});const auto edit=workspace::prepare_fem_edit(session.document());
        document::BodyHistory fem;fem.scope.id=edit.object_id;fem.name="Analysis";fem.fem=document::FemDefinition{source_id};
        require(workspace::commit_fem(live,edit,fem),"FEM placement fixture failed");
        const auto geometry=workspace::fem_placement_geometry(session,fem.scope.id);
        const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& r) {
            return r.owner_id==feature.id&&r.surface&&r.surface->kind==kernel::SurfaceGeometry::Kind::Plane;
        });
        require(face!=geometry.triangle_references.end(),"FEM placement fixture has no face");
        for(bool back:{false,true}) {
            document::FemCondition value;value.id=back?"back-force":"front-force";value.name=value.id;
            value.kind=document::FemConditionKind::Force;value.placement.orientation_back=back;
            document::ConstructionReference ref{face->instance_path,face->owner_id,face->semantic_key};
            ref.orientation_drives_rotation=true;ref.orientation_role="top";
            value.placement.references={ref};
            require(workspace::commit_fem_condition(live,workspace::prepare_fem_edit(session.document(),fem.scope.id),value),
                "FEM face placement did not commit");
        }
        const auto original=*session.document().body_history.find(fem.scope.id)->fem;
        auto regenerated=session.document();auto current=session.calculated_boundaries();
        for(int pass=0;pass<3;++pass) {
            current=workspace::calculate_part_with_resolved_references(engine,regenerated,&current);
            const auto& conditions=regenerated.body_history.find(fem.scope.id)->fem->conditions;
            require(conditions.size()==original.conditions.size(),"FEM regeneration lost a condition");
            for(std::size_t i=0;i<conditions.size();++i)
                require(conditions[i].placement.reference_valid&&conditions[i].placement==original.conditions[i].placement,
                    "Regeneration lost FEM face placement while the source remains valid");
        }
        const auto path=std::filesystem::temp_directory_path()/"zima-fem-placement-regeneration.prtz";
        regenerated.save(path,current);std::vector<kernel::BodyResult> reopened_cache;
        auto reopened=document::PartDocument::load(path,&reopened_cache);std::filesystem::remove(path);
        reopened_cache=workspace::calculate_part_with_resolved_references(engine,reopened,&reopened_cache);
        require(*reopened.body_history.find(fem.scope.id)->fem==original,"FEM placement changed after native reopen/regenerate");
    }
    std::cout<<"FEM face placement: FRONT/BACK, last/earlier source Body, repeated regeneration and native reopen passed\n";
}
