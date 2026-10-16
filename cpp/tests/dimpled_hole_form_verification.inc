int prepare_dimpled_hole(const std::filesystem::path& input,const std::filesystem::path& output) {
    std::vector<kernel::BodyResult> cached;auto authored=document::PartDocument::load(input,&cached);
    auto part=document::PartDocument::from_serialized(authored.serialized(cached,{kernel::make_stable_id(),{}, {}}),&cached);
    part.name="DimpledHole";
    check(part.body_history.bodies().size()==3&&part.sketches.size()==3,"Unexpected authored dimple roles");
    const double outer=part.sketches.front().circles.front().radius;
    const auto dimension=std::ranges::find(part.sketches[1].dimensions,sketcher::DimensionKind::DistanceSymmetric,&sketcher::SketchDimension::kind);
    check(dimension!=part.sketches[1].dimensions.end(),"Missing formed hole diameter");
    auto graph=part.body_history;const auto symbol_body=document::create_origin_bound_body(graph,part.document_id,"FORM_SYMBOL");part.set_body_history(graph);
    auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Sketch;feature.name="Manufacturing symbol";
    auto symbol=sketcher::Sketch::create_default();symbol.owner_container_id=feature.id;symbol.name="Manufacturing symbol";
    static_cast<void>(symbol.add_circle(0,0,outer));static_cast<void>(symbol.add_circle(0,0,dimension->value*.5));
    for(const auto& circle:std::vector(symbol.circles))symbol.set_geometry_centerline(circle.id,true);
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);part.sketches.push_back(symbol);
    check(part.body_history.owner(feature.id)->scope.id==symbol_body,"Symbol has no ordinary Body owner");
    graph=part.body_history;graph.activate(graph.bodies().front().scope.id);part.set_body_history(graph);
    kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);
    check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Dimple native calculation failed");
    const auto definition=document::sheet_form_definition(part,calculated);check(!definition.corner()&&!definition.flat_sketch.empty()&&!definition.symbol_sketch.empty(),"Dimple role validation failed");
    part.save(output,calculated);const auto reopened=document::read_sheet_form_definition(output);
    check(reopened.part.document_id!=authored.document_id&&reopened.bodies==definition.bodies,"Dimple native namespace or roles did not persist");
    for(std::size_t i=0;i<authored.sketches.size();++i) {
        const auto& a=authored.sketches[i];const auto& b=reopened.part.sketches[i];
        check(a.points==b.points&&a.arcs==b.arcs&&a.circles==b.circles&&a.dimensions==b.dimensions&&a.constraints==b.constraints,"Library preparation changed authored Sketch geometry or constraints");
    }
    std::cout<<"Dimple library prepared with native calculation, independent namespace, four roles and unchanged authored geometry\n";return 0;
}

int prepare_bridge_lance(const std::filesystem::path& input,const std::filesystem::path& output) {
    std::vector<kernel::BodyResult> cached;const auto authored=document::PartDocument::load(input,&cached);
    auto part=document::PartDocument::from_serialized(authored.serialized(cached,{kernel::make_stable_id(),{}, {}}),&cached);part.name="BridgeLance";
    check(part.body_history.bodies().size()==2&&part.sketches.size()==2,"Unexpected bridge source roles");
    auto graph=part.body_history;auto cut=graph.bodies().front();check(cut.name=="FORM-CUT","Unexpected bridge cut role");cut.name="FORM_CUT";graph.update_body(cut);
    static_cast<void>(document::create_origin_bound_body(graph,part.document_id,"FORM_FLAT"));
    const auto symbol_body=document::create_origin_bound_body(graph,part.document_id,"FORM_SYMBOL");part.set_body_history(graph);
    auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Sketch;feature.name="Manufacturing symbol";
    auto symbol=sketcher::Sketch::create_default();symbol.owner_container_id=feature.id;symbol.name="Manufacturing symbol";
    const auto points=part.sketches.front().points;double xmin=points.front().x,xmax=xmin,ymin=points.front().y,ymax=ymin;
    for(const auto& p:points){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
    static_cast<void>(symbol.add_rectangle(xmin,ymin,xmax,ymax));for(const auto& segment:std::vector(symbol.segments))symbol.set_geometry_centerline(segment.id,true);
    part.insert_history_entry(document::PartHistoryKind::Feature,feature.id);part.history.push_back(feature);part.sketches.push_back(symbol);check(part.body_history.owner(feature.id)->scope.id==symbol_body,"Bridge symbol has no Body owner");
    graph=part.body_history;graph.activate(cut.scope.id);part.set_body_history(graph);
    auto shape=std::ranges::find(part.history,part.sketches[1].owner_container_id,&document::HistoryContainer::id);
    check(shape!=part.history.end()&&shape->feature_kind==document::FeatureKind::Feature,"Bridge form has no unified Feature");
    const auto stop=std::ranges::find_if(points,[&](const auto& p){return std::abs(p.y-ymax)<1e-8;});
    check(stop!=points.end(),"Bridge cut has no upper stop point");
    document::ExtrusionParameters::EndTarget target;target.kind=document::EndTargetKind::Point;target.reference={part.sketches.front().id,"point:"+stop->id,{}};target.label="Cut boundary point";
    shape->feature.sides[0].extrusion_extent=document::EndCondition::UpTo;shape->feature.sides[0].targets={target};
    kernel::OcctKernel kernel;const auto calculated=workspace::calculate_part_with_resolved_references(kernel,part);check(calculated.back().calculation_errors.empty(),"Bridge source calculation failed");
    const auto definition=document::sheet_form_definition(part,calculated);check(definition.flat_sketch.empty()&&!definition.symbol_sketch.empty(),"Bridge role validation failed");part.save(output,calculated);
    const auto reopened=document::read_sheet_form_definition(output);check(reopened.bodies==definition.bodies,"Bridge roles did not survive reopen");
    for(std::size_t i=0;i<authored.sketches.size();++i){const auto& a=authored.sketches[i];const auto& b=reopened.part.sketches[i];check(a.points==b.points&&a.arcs==b.arcs&&a.segments==b.segments&&a.dimensions==b.dimensions&&a.constraints==b.constraints,"Bridge preparation changed authored geometry");}
    std::cout<<"Bridge library prepared with four roles, construction symbol and unchanged authored geometry\n";return 0;
}

int verify_library_form(const std::filesystem::path& file,bool dimple) {
    const auto source=document::read_sheet_form_definition(file);const auto immutable=source.part.serialized(*source.calculated);
    const auto flat=std::ranges::find(source.part.sketches,source.flat_sketch,&sketcher::Sketch::id);check(!dimple||flat!=source.part.sketches.end(),"Missing flat precut");
    const auto symbol=std::ranges::find(source.part.sketches,source.symbol_sketch,&sketcher::Sketch::id);
    check(symbol!=source.part.sketches.end(),"Missing manufacturing symbol");
    if(dimple)check(symbol->circles.size()==2&&std::ranges::all_of(symbol->circles,[](const auto& c){return c.construction&&c.centerline;}),"Dimple circles must retain construction roles");
    else check(symbol->segments.size()==4&&std::ranges::all_of(symbol->segments,[](const auto& s){return s.construction&&s.centerline;}),"Bridge symbol must retain construction roles");
    if(!dimple) {
        auto edited=source.part;const auto owner=edited.sketches[1].owner_container_id;
        const auto* feature=edited.find_container(owner);check(feature&&feature->feature.sides[0].targets.front().kind==document::EndTargetKind::Point,"Bridge must use its preceding cut Sketch point");
        const auto reference=feature->feature.sides[0].targets.front().reference;
        const auto dimension=std::ranges::find_if(edited.sketches.front().dimensions,[](const auto& d){return std::abs(d.value-20.)<1e-7;});check(dimension!=edited.sketches.front().dimensions.end(),"Bridge width dimension missing");const auto dimension_id=dimension->id;
        for(double width:{24.,18.,22.,20.}) {
            check(edited.sketches.front().set_dimension_value(dimension_id,width),"Bridge width edit rejected");
            kernel::OcctKernel fresh;const auto calculated=workspace::calculate_part_with_resolved_references(fresh,edited);check(calculated.back().calculation_errors.empty(),"Bridge point limit failed after width edit");
            const auto refs=edited.sketch_placement_reference_geometry(owner);const auto p=std::ranges::find_if(refs.points,[&](const auto& p){return p.reference.owner_id==reference.owner_id&&p.reference.semantic_key==reference.semantic_key;});check(p!=refs.points.end(),"Bridge source point identity lost");
            check(std::abs(std::abs(p->position.y)-width*.5)<1e-6,"Bridge symmetric source point did not move with its dimension");
            const auto& mesh=calculated.back().mesh;double low=1e100,high=-1e100;for(const auto& v:mesh.vertices){low=std::min(low,v.y);high=std::max(high,v.y);}check(std::abs(low+width*.5)<1e-6&&std::abs(high-width*.5)<1e-6,"Bridge Up to did not follow edited source width");
        }
        std::cout<<"Bridge point stop: repeated cut-width dimension edits and exact extrusion bounds passed"<<std::endl;
    }
    const auto request=document::sheet_form_request(source,{}, {},{0,1,0},{1,0,0},1.);
    check(request.symbol_edges.size()==(dimple?2u:4u)&&std::ranges::all_of(request.symbol_edges,[](const auto& e){return e.construction&&e.dash_dot;}),"Form symbol lost its construction strokes");
    const double radius=dimple?flat->circles.front().radius:0.;
    kernel::OcctKernel kernel;
    for(double thickness:{.8,1.,1.2})for(bool bottom:{false,true}) {
        auto part=document::PartDocument::create_default();static_cast<void>(document::create_origin_bound_body(part.body_history,part.document_id,"Sheet"));
        auto stock=document::PartDocument::create_sketch_container();stock.feature_kind=document::FeatureKind::Flat;stock.flat.direction=document::ExtrusionDirection::Reverse;stock.flat.thickness_override=true;stock.flat.thickness=thickness;
        auto outline=sketcher::Sketch::create_default();outline.owner_container_id=stock.id;static_cast<void>(outline.add_rectangle(-50,-50,50,50));stock.flat.sketch_id=outline.id;
        part.insert_history_entry(document::PartHistoryKind::Feature,stock.id);part.history.push_back(stock);part.sketches.push_back(outline);
        const auto baseline=workspace::calculate_part_with_resolved_references(kernel,part);
        const auto& refs=baseline.back().mesh.original_references;
        const auto face=std::ranges::find_if(refs.triangle_references,[&](const auto& f){return f.surface&&f.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&f.sheet_thickness>0&&std::abs(f.surface->origin.z-(bottom?-thickness:0))<1e-7&&(f.sheet_role==kernel::SheetFaceRole::SideA||f.sheet_role==kernel::SheetFaceRole::SideB);});
        check(face!=refs.triangle_references.end(),"Dimple sheet support missing");
        workspace::Workspace live;live.add_part(part,baseline);live.activate(part.document_id);
        auto feature=document::create_sheet_form();feature.sheet_form=document::copy_sheet_form_definition(source);feature.sheet_form.support=*face;
        const auto geometry=part.construction_reference_geometry_for(stock.id,refs);
        feature.placement=document::sheet_form_attachment(*face,part.body_history.bodies().front().origin().id,geometry,{},{});
        check(workspace::commit_sheet_form(live,kernel,part.document_id,feature),"Dimple insertion did not commit");auto* state=live.open_part(part.document_id);valid(state->session.calculated_boundaries().back());
        const auto spatial=document::serialize_body_result(state->session.calculated_boundaries().back());
        check(state->session.undo()&&state->session.redo(),"Dimple insertion Undo/Redo failed");check(document::serialize_body_result(state->session.calculated_boundaries().back())==spatial,"Dimple Redo changed native geometry");
        auto flatten=document::PartDocument::create_sketch_container();flatten.feature_kind=document::FeatureKind::Unbend;flatten.sheet_state.all=false;flatten.sheet_state.owners={feature.id};
        check(workspace::commit_sheet_state(live,kernel,part.document_id,flatten),"Dimple Unbend did not commit");valid(state->session.calculated_boundaries().back());
        auto measured=state->session.calculated_boundaries().back();kernel.ensure_body_properties(measured);near(measured.volume,(10000.-std::numbers::pi*radius*radius)*thickness,1e-5);
        auto restore=document::PartDocument::create_sketch_container();restore.feature_kind=document::FeatureKind::BendBack;restore.sheet_state.all=false;restore.sheet_state.owners={feature.id};
        check(workspace::commit_sheet_state(live,kernel,part.document_id,restore),"Dimple Bend Back did not commit");valid(state->session.calculated_boundaries().back());
        const auto path=std::filesystem::absolute("build/form-diagnostic")/(source.part.name+"-"+std::to_string(thickness)+"-"+std::to_string(bottom)+".prtz");
        state->session.document().save(path,state->session.calculated_boundaries());std::vector<kernel::BodyResult> reopened;const auto saved=document::PartDocument::load(path,&reopened);valid(reopened.back());check(saved.find_container(feature.id)->sheet_form.bodies==state->session.document().find_container(feature.id)->sheet_form.bodies,"Dimple role identities changed on reopen");
        kernel::OcctKernel cold;auto cold_part=saved;const auto regenerated=workspace::calculate_part_with_resolved_references(cold,cold_part);valid(regenerated.back());near(measured_volume(cold,regenerated.back()),measured_volume(kernel,reopened.back()),1e-5);
        const auto packets=workspace::drawing_annotation_sources(&live,part.document_id,{});check(packets.size()==1&&packets.front().symbols.size()==1,"Form manufacturing symbol was lost");
        const auto rendered=symbols::instance_mesh(packets.front().symbols.front().symbol);check(rendered.edges.size()==(dimple?2u:4u)&&std::ranges::all_of(rendered.edges,[](const auto& e){return e.construction&&e.dash_dot;}),"Drawing symbol lost its construction strokes");
        std::cout<<source.part.name<<" thickness="<<thickness<<" bottom="<<bottom<<" insertion, precut volume, Unbend/Bend Back, Undo/Redo and reopen passed"<<std::endl;
    }
    check(source.part.serialized(*source.calculated)==immutable,"Dimple insertion mutated the library");return 0;
}
