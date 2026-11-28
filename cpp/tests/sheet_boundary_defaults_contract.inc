// Exercise the same metadata transaction as Sheet Properties, with native
// source Fillet edits, correction identity, cold geometry and Undo/Redo.
nlohmann::json converted_correction_values(const document::PartDocument& part,
        const std::string& parent_id) {
    nlohmann::json result=nlohmann::json::object();
    const auto record=[&](const sketcher::Sketch& sketch) {
        for(const auto& dimension:sketch.dimensions)
            result[dimension.id]={dimension.value,std::signbit(dimension.value),
                dimension.solution_side,dimension.driving,dimension.locked};
    };
    for(const auto& binding:part.find_container(parent_id)->sheet_from_body.child_bindings) {
        if(binding.detached)continue;
        const auto* feature=part.find_container(binding.feature_id);
        const auto sketch=std::ranges::find(part.sketches,feature->id,&sketcher::Sketch::owner_container_id);
        check(sketch!=part.sketches.end(),"Changed sheet lost its owned Sketch");
        record(*sketch);
        if(binding.bend)record(sketcher::Sketch::from_serialized(feature->bend.auxiliary_sketches[1]));
    }
    return result;
}

std::size_t verify_native_sheet_thickness(const TopoDS_Shape& shape,double thickness) {
    check(BRepCheck_Analyzer(shape).IsValid(),"Changed sheet thickness has invalid material");
    std::size_t branches{};
    for(TopExp_Explorer solid(shape,TopAbs_SOLID);solid.More();solid.Next()) {
        std::vector<gp_Pln> planes;std::vector<gp_Cylinder> cylinders;
        for(TopExp_Explorer face(solid.Current(),TopAbs_FACE);face.More();face.Next()) {
            BRepAdaptor_Surface surface(TopoDS::Face(face.Current()));
            if(surface.GetType()==GeomAbs_Plane)planes.push_back(surface.Plane());
            if(surface.GetType()==GeomAbs_Cylinder)cylinders.push_back(surface.Cylinder());
        }
        bool found{};
        if(cylinders.empty()) {
            for(std::size_t i=0;i<planes.size();++i)for(std::size_t j=i+1;j<planes.size();++j)
                if(std::abs(planes[i].Axis().Direction().Dot(planes[j].Axis().Direction()))>1-1e-10&&
                   std::abs(planes[i].Distance(planes[j].Location())-thickness)<1e-7)found=true;
        } else {
            for(std::size_t i=0;i<cylinders.size();++i)for(std::size_t j=i+1;j<cylinders.size();++j)
                if(std::abs(cylinders[i].Axis().Direction().Dot(cylinders[j].Axis().Direction()))>1-1e-10&&
                   gp_Lin(cylinders[i].Axis()).Distance(cylinders[j].Location())<1e-7&&
                   std::abs(std::abs(cylinders[i].Radius()-cylinders[j].Radius())-thickness)<1e-7)found=true;
        }
        check(found,"Native sheet branch did not acquire the requested wall thickness");++branches;
    }
    check(branches>0,"Changed sheet has no native material branches");
    std::cout<<"native_thickness="<<thickness<<" verified_branches="<<branches<<std::endl;
    return branches;
}

void boundary_material_edit_contract(document::PartDocument part,
        std::vector<kernel::BodyResult> cache,const std::filesystem::path& output) {
    kernel::OcctKernel engine;
    const auto original_parent=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetFromBody;});
    check(original_parent!=part.history.end(),"Material edit fixture has no conversion");
    const auto original_corrections=converted_correction_values(part,original_parent->id);
    // Repair any stale disposable boundary before recording the expected
    // identities. The original user document is never written by this probe.
    cache=workspace::calculate_part_with_resolved_references(engine,part,&cache);
    check(!cache.empty()&&cache.back().calculation_errors.empty(),"Initial sheet material refresh failed");
    const auto parent=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==document::FeatureKind::SheetFromBody;});
    check(parent!=part.history.end(),"Material edit fixture has no conversion");
    const auto parent_id=parent->id,body=part.body_owner_for_object(parent_id)->scope.id;
    const auto children=parent->sheet_from_body.generated_features;
    const auto corrections=converted_correction_values(part,parent_id);
    check(corrections==original_corrections,"Initial thickness refresh changed authored corrections");
    const bool inherited=!parent->sheet_from_body.thickness_override;
    const double fixed_thickness=parent->sheet_from_body.thickness;
    const auto branch_count=verify_native_sheet_thickness(converted_body_shape(part,cache),fixed_thickness);
    workspace::Workspace live;const auto id=part.document_id;live.add_part(part,cache);
    auto& session=live.open_part(id)->session;
    std::filesystem::create_directories(output);
    const auto authored_state=[](const document::PartDocument& current) {
        auto state=current.serialized();
        // Undo retains reservations for dimensions created by later revisions
        // so an abandoned identifier is never reused. Model identities and
        // correction values are checked independently below.
        state.erase("dimension_identifiers");
        // The derived fingerprint includes those retained reservations.
        state.erase("calculation_definition_fingerprint");return state;
    };
    const auto check_state=[&](const nlohmann::json& expected,const char* message) {
        const auto actual=authored_state(session.document());
        if(actual!=expected) {
            const auto difference=nlohmann::json::diff(expected,actual);
            std::ofstream(output/"history-state-difference.json")<<difference.dump(2);
            for(const auto& item:difference)std::cerr<<"history_difference="<<item.at("path")<<std::endl;
            throw std::runtime_error(message);
        }
    };
    const auto verify=[&](double thickness) {
        const auto& current=session.document();const auto& calculated=session.calculated_boundaries();
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Sheet edit failed calculation");
        const auto& definition=current.find_container(parent_id)->sheet_from_body;
        check(definition.generated_features==children,"Material edit replaced generated child identities");
        check(converted_correction_values(current,parent_id)==corrections,"Material edit changed authored correction values or identities");
        near(definition.thickness,thickness,1e-10);
        for(const auto& binding:definition.child_bindings)if(!binding.detached) {
            check(binding.references_valid,"Material edit lost a native source reference");
            const auto* child=current.find_container(binding.feature_id);
            const auto defaults=document::sheet_metal_defaults(current);
            near(binding.bend?document::resolved_bend_parameters(*child,defaults).thickness:
                document::flat_thickness(*child,defaults),thickness,1e-10);
        }
        check(verify_native_sheet_thickness(converted_body_shape(current,calculated),thickness)==branch_count,
            "Material edit merged or split independent branches");
    };
    for(double thickness:{3.,2.,3.}) {
        const auto before=authored_state(session.document());
        const double before_thickness=session.document().find_container(parent_id)->sheet_from_body.thickness;
        std::map<std::string,document::HistoryContainer> fillets;
        for(const auto& f:session.document().history)if(f.feature_kind==document::FeatureKind::Fillet)fillets.emplace(f.id,f);
        auto settings=workspace::file_settings(live,id);settings.sheet_metal->thickness_mm=thickness;
        const auto begin=std::chrono::steady_clock::now();
        const auto change=workspace::set_file_settings(live,engine,id,settings);
        std::cout<<"sheet_default="<<thickness<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<std::endl;
        verify(inherited?thickness:fixed_thickness);
        for(const auto& [owner,definition]:fillets)
            check(*session.document().find_container(owner)==definition,"Thickness edit changed a source Fillet radius or definition");
        const auto after=authored_state(session.document());
        const auto revision=session.revision(),count=kernel::body_calculation_count.load();
        check(!workspace::set_file_settings(live,engine,id,settings).changed&&session.revision()==revision&&
            kernel::body_calculation_count.load()==count,"Unchanged Sheet Properties calculated or created Undo");
        if(change.changed) {
            check(session.undo(),"Thickness Undo is unavailable");check_state(before,"Thickness Undo failed");
            verify(before_thickness);
            check(session.redo(),"Thickness Redo is unavailable");check_state(after,"Thickness Redo failed");
            verify(inherited?thickness:fixed_thickness);
        }
    }
    const double original_factor=workspace::file_settings(live,id).sheet_metal->k_factor;
    for(double factor:{.37,original_factor}) {
        auto settings=workspace::file_settings(live,id);settings.sheet_metal->k_factor=factor;
        static_cast<void>(workspace::set_file_settings(live,engine,id,settings));
        verify(inherited?3.:fixed_thickness);
        std::cout<<"k_factor="<<factor<<" preserved_corrections=true"<<std::endl;
    }
    // Change the original solid Fillet, not a generated Bend radius.
    const auto fillet=std::ranges::find_if(session.document().history,[](const auto& f){return f.feature_kind==document::FeatureKind::Fillet;});
    check(fillet!=session.document().history.end(),"Source-radius fixture has no Fillet");
    const auto fillet_id=fillet->id;const double radius=fillet->edge_treatment.primary_size;
    for(double requested:{radius+1.,radius}) {
        auto edited=session.document();auto calculated=session.calculated_boundaries();
        const auto before=authored_state(edited);
        const double before_volume=measured_volume(engine,calculated.back().body_outputs.at(body));
        edited.find_container(fillet_id)->edge_treatment.primary_size=requested;
        const auto begin=std::chrono::steady_clock::now();
        calculated=workspace::calculate_part_with_resolved_references(engine,edited,&calculated);
        workspace::commit_part_document(live,id,std::move(edited),std::move(calculated));
        std::cout<<"source_fillet_radius="<<requested<<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<std::endl;
        verify(inherited?3.:fixed_thickness);
        check(std::abs(measured_volume(engine,session.calculated_boundaries().back().body_outputs.at(body))-before_volume)>1e-6,
            "Source Fillet radius edit did not change converted material");
        const auto after=authored_state(session.document());
        check(session.undo()&&authored_state(session.document())==before,"Source Fillet Undo failed");
        check(session.redo()&&authored_state(session.document())==after,"Source Fillet Redo failed");
    }
    const auto path=output/"material-edited.prtz";
    session.document().save(path,session.calculated_boundaries());
    auto reopened=document::PartDocument::load(path);kernel::OcctKernel cold;
    const auto fresh=workspace::calculate_part_with_resolved_references(cold,reopened);
    check(fresh.back().calculation_errors.empty()&&converted_correction_values(reopened,parent_id)==corrections,
        "Cold reopen lost material edits or corrections");
    compare_independent_branch_solids(converted_body_shape(session.document(),session.calculated_boundaries()),converted_body_shape(reopened,fresh));
    auto state=document::PartDocument::create_sketch_container();state.feature_kind=document::FeatureKind::Unbend;state.sheet_state.all=true;
    reopened.body_history.activate(body);reopened.insert_history_entry(document::PartHistoryKind::Feature,state.id);reopened.history.push_back(state);
    auto unfolded=workspace::calculate_part_with_resolved_references(cold,reopened,&fresh);
    check(unfolded.back().calculation_errors.empty(),"Changed thickness/radius failed Unbend");
    check_independent_overlap(converted_body_shape(reopened,unfolded));
    reopened.save(output/"material-unfolded.prtz",unfolded);
    auto bend_back=document::PartDocument::create_sketch_container();bend_back.feature_kind=document::FeatureKind::BendBack;bend_back.sheet_state.all=true;
    reopened.insert_history_entry(document::PartHistoryKind::Feature,bend_back.id);reopened.history.push_back(bend_back);
    const auto refolded=workspace::calculate_part_with_resolved_references(cold,reopened,&unfolded);
    check(refolded.back().calculation_errors.empty(),"Changed material failed Bend Back");
    compare_independent_branch_solids(converted_body_shape(session.document(),session.calculated_boundaries()),converted_body_shape(reopened,refolded));
    workspace::Workspace released;released.add_part(session.document(),session.calculated_boundaries());
    check(workspace::explode_sheet_from_body(released,engine,id,parent_id),"Changed material failed Explode");
    const auto& exploded=released.open_part(id)->session;
    compare_independent_branch_solids(converted_body_shape(session.document(),session.calculated_boundaries()),
        kernel::read_calculated_body_shape(exploded.calculated_boundaries().back().body_outputs.at(body)));
    std::cout<<"Thickness, source Fillet, corrections, native geometry, Undo/Redo, reopen, unfold and release passed"<<std::endl;
}
