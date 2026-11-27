namespace {
void keyway_confirmation(){
    kernel::OcctKernel kernel;
    auto part=document::PartDocument::create_default();
    document::BodyHistoryGraph graph;const auto body_id=graph.create_body("Confirmation shaft");
    auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,50));
    auto shaft=document::PartDocument::create_extrusion_container(sketch.id);
    shaft.extrusion.height=shaft.extrusion.length_forward=150;sketch.owner_container_id=shaft.id;
    part.history.push_back(shaft);part.sketches.push_back(sketch);
    auto body=*graph.find(body_id);body.entries.push_back({document::PartHistoryKind::Feature,shaft.id});body.cursor=1;
    graph.update_body(body);part.set_body_history(graph);
    auto cache=workspace::calculate_part_with_resolved_references(kernel,part);
    auto owner=workspace::create_mech_keyway();owner.mech_keyway.table_id="din6885-1-maedler";
    owner.mech_keyway.axial_offset=20;owner.mech_keyway.length=60;
    auto refs=cache.back().mesh.original_references;kernel::FaceReference end;
    for(const auto& face:refs.triangle_references)if(face.surface){
        if(face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder){owner.mech_keyway.support=face;
            owner.mech_keyway.axial_direction=face.surface->axis.z>0?1:-1;}
        if(face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(face.surface->origin.z)<1e-8)end=face;
    }
    workspace::initialize_mech_keyway_placement(owner,refs,end);
    for(bool edit:{false,true}){
        workspace::Workspace live;live.add_part(part,cache);live.activate(part.document_id);
        auto* state=live.open_part(part.document_id);
        auto pending=owner;
        if(edit){pending.mech_keyway.table_id="zima-csn-parallel";
            check(workspace::commit_mech_keyway(live,kernel,part.document_id,pending),"Cannot create the valid CSN edit fixture");
            pending=*state->session.document().find_container(owner.id);pending.mech_keyway.table_id="din6885-1-maedler";}
        const auto before=state->session.document().serialized();const auto revision=state->session.revision();
        const auto* cached=state->session.calculated_boundaries().data();
        rejected([&]{static_cast<void>(workspace::commit_mech_keyway(live,kernel,part.document_id,pending));});
        workspace::ModelingConfirmation confirmation;
        unsigned calculations=0;
        workspace::CalculationExecutionScope counted([&](auto calculate){++calculations;calculate();});
        const auto submit=[&](bool failure){
            workspace::ModelingConfirmationAttempt attempt(confirmation,live,part.document_id);
            bool failed=false;try{static_cast<void>(workspace::commit_mech_keyway(live,kernel,part.document_id,pending));}
            catch(const std::exception& e){failed=true;if(!failure)std::cerr<<e.what()<<'\n';}
            check(failed==failure&&attempt.failed()==failure,"Wrong keyway first/second OK result");
        };
        submit(true);
        check(state->session.revision()==revision&&state->session.document().serialized()==before&&
            state->session.calculated_boundaries().data()==cached,"First failed keyway OK changed the live model");
        ++pending.mech_keyway.length;submit(true);
        state->session.update_calculated_boundaries(state->session.calculated_boundaries());submit(true);
        confirmation.clear();submit(true); // Cancel invalidates the retained acknowledgement.
        const auto count=calculations;submit(false);
        check(count==calculations,"Second keyway OK repeated geometry calculation");
        check(state->session.revision()==revision+1&&state->session.document().reference_errors.contains(owner.id)&&
            state->session.calculated_boundaries().back().calculation_errors.contains(owner.id),"Unresolved keyway was not committed once");
        check(state->session.document().find_container(owner.id)->placement==pending.placement,"Failed keyway lost its frame");
        const auto file=std::filesystem::path("build")/(edit?"keyway-unresolved-edit.prtz":"keyway-unresolved-new.prtz");
        state->session.document().save(file,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> saved;auto reopened=document::PartDocument::load(file,&saved);
        check(!saved.empty()&&saved.back().calculation_errors.contains(owner.id),"Keyway save/reopen lost its error");
        auto cold=workspace::calculate_part_with_resolved_references(kernel,reopened);
        check(cold.back().calculation_errors.contains(owner.id)&&reopened.reference_errors.contains(owner.id),"Cold keyway regeneration lost unresolved state");
        check(state->session.undo()&&state->session.redo(),"Keyway unresolved Undo/Redo failed");
        pending=*state->session.document().find_container(owner.id);pending.mech_keyway.table_id="zima-csn-parallel";
        confirmation.clear();submit(false);
        check(!state->session.document().reference_errors.contains(owner.id)&&state->session.calculated_boundaries().back().calculation_errors.empty(),"CSN selection did not repair the keyway");
        near(state->session.document().find_container(owner.id)->mech_keyway.width,28,"CSN diameter 100 has the wrong key width");
    }
    std::cout<<"Keyway creation/edit confirmation, changed inputs, Cancel, persistence, Undo/Redo and CSN repair passed\n";
}
}
