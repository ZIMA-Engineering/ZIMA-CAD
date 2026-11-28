// A finite authored RIP lies on the bottom skin of a filleted box. Its sector
// ends at y = radius, so radius 9 removes the y = 8 RIP from that skin while
// radius 7 preserves both regions. Verify the actual native trimmed face too.
void sheet_source_edit_contract() {
    QTemporaryDir temporary("build/sheet-source-edit-XXXXXX");
    check(temporary.isValid(),"Source edit temporary directory is unavailable");
    const std::filesystem::path output(temporary.path().toStdString());
    kernel::OcctKernel engine;
    auto part=document::PartDocument::create_default();
    document::set_sheet_metal_defaults(part,{2.,.5});
    const auto body=part.body_history.create_body("RIP sector");
    auto profile=sketcher::Sketch::create_default();
    static_cast<void>(profile.add_rectangle(0,0,100,80));
    auto source=document::PartDocument::create_feature_container(profile.id);
    profile.owner_container_id=source.id;source.feature.sides[0].length=40;
    part.insert_history_entry(document::PartHistoryKind::Feature,source.id);
    part.history.push_back(source);part.sketches.push_back(profile);
    auto cache=workspace::calculate_part_with_resolved_references(engine,part);
    std::vector<kernel::EdgeReference> edges;
    for(const auto& edge:cache.back().body_outputs.at(body)->mesh.edges)
        if(edge.points.size()>=2&&std::ranges::all_of(edge.points,[](auto p){return std::abs(p.z)<1e-7;}))edges.push_back(edge.reference);
    check(edges.size()==4,"Sector fixture requires four bottom edges");
    auto fillet=document::PartDocument::create_fillet_container(edges);
    fillet.edge_treatment.routes.clear();fillet.edge_treatment.route_end_faces.clear();
    for(const auto& edge:edges){fillet.edge_treatment.routes.push_back({edge});fillet.edge_treatment.route_end_faces.emplace_back();}
    fillet.edge_treatment.primary_size=5;
    part.insert_history_entry(document::PartHistoryKind::Feature,fillet.id);part.history.push_back(fillet);
    auto rip_feature=document::PartDocument::create_sketch_container();rip_feature.feature_kind=document::FeatureKind::Sketch;
    auto rip=sketcher::Sketch::create_default();rip.owner_container_id=rip_feature.id;
    static_cast<void>(rip.add_segment(-5,8,105,8));
    part.insert_history_entry(document::PartHistoryKind::Feature,rip_feature.id);
    part.history.push_back(rip_feature);part.sketches.push_back(rip);
    cache=workspace::calculate_part_with_resolved_references(engine,part,&cache);
    const auto& faces=cache.back().body_outputs.at(body)->mesh.original_references.triangle_references;
    const auto seed=std::ranges::find_if(faces,[](const auto& face){return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
        std::abs(face.surface->axis.z)>.999999&&std::abs(face.surface->origin.z)<1e-7;});
    check(seed!=faces.end(),"Sector fixture has no bottom plane");
    auto converted=workspace::prepare_sheet_from_body(part,cache,*seed,2,engine,true,{}, {rip.id});
    check(converted.calculated.back().calculation_errors.empty(),"Initial sector conversion failed");
    part=std::move(converted.document);cache=std::move(converted.calculated);
    const auto parent_id=converted.container_id;
    const auto bend=std::ranges::find_if(part.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::Bend;});
    check(bend!=part.history.end(),"Sector fixture requires an authored Bend");
    document::set_bend_profile_extensions(*bend,-.2,-.1);
    cache=workspace::calculate_part_with_resolved_references(engine,part,&cache);
    const auto children=part.find_container(parent_id)->sheet_from_body.generated_features;
    const auto corrections=converted_correction_values(part,parent_id);
    const auto authored=[](const document::PartDocument& current) {
        auto state=current.serialized();state.erase("dimension_identifiers");state.erase("calculation_definition_fingerprint");return state;
    };
    const auto frames=[&](const document::PartDocument& current) {
        nlohmann::json result=nlohmann::json::object();
        for(const auto& id:children) {
            const auto& p=current.find_container(id)->placement;
            result[id]={p.x,p.y,p.z,p.rotation_x,p.rotation_y,p.rotation_z};
            for(const auto& sketch:current.sketches)if(sketch.owner_container_id==id)
                result[sketch.id]={sketch.resolved_origin.x,sketch.resolved_origin.y,sketch.resolved_origin.z,
                    sketch.resolved_x_axis.x,sketch.resolved_x_axis.y,sketch.resolved_x_axis.z,
                    sketch.resolved_y_axis.x,sketch.resolved_y_axis.y,sketch.resolved_y_axis.z};
        }
        return result;
    };
    workspace::Workspace live;const auto id=part.document_id;live.add_part(part,cache);
    auto& state=*live.open_part(id);
    struct Edit {const char* kind;double value;bool valid;};
    const std::vector<Edit> edits{{"radius",7,true},{"radius",9,false},{"radius",5,true},
        {"length",30,true},{"length",50,true},{"length",40,true},
        {"x",5,true},{"x",0,true},{"radius",7,true},{"radius",5,true}};
    for(const auto& edit:edits) {
        const auto before=authored(state.session.document());
        const auto before_frames=frames(state.session.document());
        const auto before_shape=converted_body_shape(state.session.document(),state.session.calculated_boundaries());
        GProp_GProps before_properties;BRepGProp::VolumeProperties(before_shape,before_properties);
        const double previous_x=state.session.document().find_container(source.id)->placement.x;
        const auto count=kernel::body_calculation_count.load();
        const auto start=std::chrono::steady_clock::now();
        if(std::string_view(edit.kind)=="radius") {
            auto draft=state.session.document();draft.find_container(fillet.id)->edge_treatment.primary_size=edit.value;
            workspace::commit_part_parameter_edit(state,engine,std::move(draft),fillet.id,true);
        } else if(std::string_view(edit.kind)=="length") {
            auto draft=*state.session.document().find_container(source.id);draft.feature.sides[0].length=edit.value;
            workspace::commit_profile(live,engine,id,draft,workspace::ProfileEditMode::Replace);
        } else check(workspace::set_placement_values(live,engine,id,source.id,{{"x",edit.value}}),"Source placement did not change");
        std::cout<<"sheet_source_edit="<<edit.kind<<" value="<<edit.value
            <<" seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
            <<" calculations="<<kernel::body_calculation_count.load()-count<<std::endl;
        if(std::string_view(edit.kind)!="radius"||!edit.valid)
            check(kernel::body_calculation_count.load()-count<=6,
                "Stable source edit recalculated obsolete sheet branches");
        const auto& current=state.session.document();const auto& calculated=state.session.calculated_boundaries();
        check(current.find_container(parent_id)->sheet_from_body.generated_features==children,"Source edit replaced native children");
        check(converted_correction_values(current,parent_id)==corrections,"Source edit changed authored corrections");
        const auto invalid=std::ranges::count_if(current.find_container(parent_id)->sheet_from_body.child_bindings,
            [](const auto& b){return !b.detached&&!b.references_valid;});
        check(edit.valid?invalid==0:invalid==static_cast<std::ptrdiff_t>(children.size()),"RIP sector classification is incorrect");
        check(calculated.back().calculation_errors.contains(parent_id)!=edit.valid,"RIP sector diagnostic is incorrect");
        const auto* input=workspace::calculated_operation_input(current,calculated,parent_id);
        check(input,"Source edit has no exact preconversion boundary");
        const auto source_shape=kernel::read_calculated_body_shape(*input);
        const double x=current.find_container(source.id)->placement.x;
        bool on_bottom{};
        for(TopExp_Explorer f(source_shape,TopAbs_FACE);f.More();f.Next()) {
            const auto face=TopoDS::Face(f.Current());BRepAdaptor_Surface surface(face);
            if(surface.GetType()!=GeomAbs_Plane||std::abs(surface.Plane().Axis().Direction().Z())<.999999||
                    std::abs(surface.Plane().Location().Z())>1e-7)continue;
            const auto status=BRepClass_FaceClassifier(face,gp_Pnt(50+x,8,0),1e-7).State();
            on_bottom|=status==TopAbs_IN||status==TopAbs_ON;
        }
        check(on_bottom==edit.valid,"Exact source trim disagrees with the analytic RIP sector");
        const auto shape=converted_body_shape(current,calculated);
        verify_native_sheet_thickness(shape,2);
        if(edit.valid) {
            check(calculated.back().calculation_errors.empty(),"Valid source edit has material errors");
            GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
            if(std::string_view(edit.kind)=="x") {
                near(properties.Mass(),before_properties.Mass(),1e-5);
                near(properties.CentreOfMass().X()-before_properties.CentreOfMass().X(),edit.value-previous_x,1e-7);
                near(properties.CentreOfMass().Y(),before_properties.CentreOfMass().Y(),1e-7);
                near(properties.CentreOfMass().Z(),before_properties.CentreOfMass().Z(),1e-7);
            } else check(std::abs(properties.Mass()-before_properties.Mass())>1e-5,"Source dimension did not change actual sheet material");
        } else {
            check(frames(current)==before_frames,"Lost RIP changed a last valid child frame");
            compare_independent_branch_solids(before_shape,shape);
        }
        const auto path=output/"edited.prtz";current.save(path,calculated);
        const auto load_count=kernel::body_calculation_count.load();
        std::vector<kernel::BodyResult> reopened_cache;auto reopened=document::PartDocument::load(path,&reopened_cache);
        check(kernel::body_calculation_count.load()==load_count,"Native reopen recalculated source geometry");
        check(frames(reopened)==frames(current)&&converted_correction_values(reopened,parent_id)==corrections,"Native reopen lost frames or corrections");
        kernel::OcctKernel fresh;
        auto cold=workspace::calculate_part_with_resolved_references(fresh,reopened);
        check(cold.back().calculation_errors.contains(parent_id)!=edit.valid,"Cold source edit changed RIP validity");
        compare_independent_branch_solids(shape,converted_body_shape(reopened,cold));
        const auto after=authored(current);
        check(state.session.undo()&&authored(state.session.document())==before,"Source edit Undo failed");
        check(state.session.redo()&&authored(state.session.document())==after,"Source edit Redo failed");
    }
    auto flat=state.session.document();const auto folded=state.session.calculated_boundaries();
    auto unbend=document::PartDocument::create_sketch_container();unbend.feature_kind=document::FeatureKind::Unbend;unbend.sheet_state.all=true;
    flat.insert_history_entry(document::PartHistoryKind::Feature,unbend.id);flat.history.push_back(unbend);
    const auto unfolded=workspace::calculate_part_with_resolved_references(engine,flat,&folded);
    check(unfolded.back().calculation_errors.empty(),"Source edits failed Unbend");
    check_independent_overlap(converted_body_shape(flat,unfolded));
    auto back=document::PartDocument::create_sketch_container();back.feature_kind=document::FeatureKind::BendBack;back.sheet_state.all=true;
    flat.insert_history_entry(document::PartHistoryKind::Feature,back.id);flat.history.push_back(back);
    const auto refolded=workspace::calculate_part_with_resolved_references(engine,flat,&unfolded);
    check(refolded.back().calculation_errors.empty(),"Source edits failed Bend Back");
    compare_independent_branch_solids(converted_body_shape(state.session.document(),folded),converted_body_shape(flat,refolded));
    const auto expected=converted_body_shape(state.session.document(),folded);
    check(workspace::explode_sheet_from_body(live,engine,id,parent_id),"Source edits failed Explode");
    compare_independent_branch_solids(expected,kernel::read_calculated_body_shape(state.session.calculated_boundaries().back().body_outputs.at(body)));
    std::cout<<"Source radius, length, placement, RIP sector loss/repair, exact trim, persistence, Undo/Redo and sheet states passed"<<std::endl;
}
