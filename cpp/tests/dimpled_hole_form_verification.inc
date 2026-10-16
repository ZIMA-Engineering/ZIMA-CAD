int verify_form_dimension_edits(const std::filesystem::path& input) {
    std::vector<kernel::BodyResult> cache;auto part=document::PartDocument::load(input,&cache);
    const auto form=std::ranges::find(part.history,document::FeatureKind::SheetForm,&document::HistoryContainer::feature_kind);
    check(form!=part.history.end(),"No inserted FORM in dimension fixture");const auto owner=form->id;
    const auto initial=*form;const auto factory=*initial.sheet_form.definition;
    workspace::Workspace live;live.add_part(part,cache);live.activate(part.document_id);kernel::OcctKernel kernel;
    auto* state=live.open_part(part.document_id);const auto dimensions=document::sheet_form_internal_dimensions(initial);
    std::vector<kernel::BodyResult> peer_cache;
    auto peer_part=document::PartDocument::from_serialized(part.serialized(cache,{kernel::make_stable_id(),{}, {}}),&peer_cache);
    const auto peer_document=peer_part.document_id;
    const auto peer_form=std::ranges::find(peer_part.history,document::FeatureKind::SheetForm,&document::HistoryContainer::feature_kind);
    check(peer_form!=peer_part.history.end(),"Independent peer FORM is missing");
    const auto peer=peer_form->id;const auto peer_definition=*peer_form->sheet_form.definition;
    live.add_part(peer_part,peer_cache);live.activate(part.document_id);state=live.open_part(part.document_id);
    int edited=0,rejected=0,readonly=0;bool peer_geometry_checked=false;
    for(const auto& dimension:dimensions) {
        const auto key=dimension.reference.semantic_key;
        const auto before=state->session.document().serialized();
        if(!document::sheet_form_internal_dimension_editable(initial,key)){++readonly;continue;}
        const auto revision=state->session.revision();
        check(!workspace::set_sheet_form_dimension(live,kernel,part.document_id,owner,key,dimension.value)&&state->session.revision()==revision,"Unchanged FORM dimension created a transaction");
        const auto value=dimension.value==0?.1:dimension.value*1.02;
        auto source_copy=document::stored_sheet_form_definition(initial.sheet_form).part;
        for(auto& sketch:source_copy.sketches)for(auto& d:sketch.dimensions)if(key.ends_with("dimension:"+d.id)) {
            d.driving=false;const auto reference_before=source_copy.serialized();bool refused=false;
            try {static_cast<void>(document::edit_sheet_form_internal_dimension(source_copy,key,value));}catch(const std::exception&){refused=true;}
            check(refused&&source_copy.serialized()==reference_before,"Reference FORM dimension became a driving edit");
            d.driving=true;d.locked=true;
            auto ordinary=sketch;const bool expected=ordinary.set_dimension_value(d.id,value);bool accepted=false;
            try {accepted=document::edit_sheet_form_internal_dimension(source_copy,key,value);}catch(const std::exception&){}
            check(accepted==expected,"FORM changed ordinary locked-dimension numeric policy");
            if(accepted)check(sketch.points==ordinary.points&&sketch.circles==ordinary.circles,"Locked FORM numeric edit changed the Sketch solution");
        }
        try {
            check(workspace::set_sheet_form_dimension(live,kernel,part.document_id,owner,key,value),"FORM dimension edit did not commit");
        }catch(const std::exception& error) {
            check(state->session.document().serialized()==before,"Rejected FORM dimension changed the document");
            std::cout<<"FORM dimension rejected "<<key<<": "<<error.what()<<std::endl;++rejected;continue;
        }
        const auto changed=state->session.document().serialized();valid(state->session.calculated_boundaries().back());
        const auto* current=state->session.document().find_container(owner);
        check(current->placement==initial.placement&&current->sheet_form.bodies==initial.sheet_form.bodies,"FORM edit changed placement or role identity");
        const auto shown=document::sheet_form_internal_dimensions(*current);
        const auto found=std::ranges::find_if(shown,[&](const auto& d){return d.reference.semantic_key==key;});
        check(found!=shown.end(),"FORM dimension lost its stable identity");near(found->value,value,1e-6);
        using namespace kernel::sheet_material;
        const auto delta=sub(found->witness_second,found->witness_first);
        if(found->kind==kernel::ViewerDimensionKind::Radius||found->kind==kernel::ViewerDimensionKind::Diameter)
            near(std::sqrt(dot(delta,delta))*(found->kind==kernel::ViewerDimensionKind::Diameter?2.:1.),std::abs(value),1e-5);
        else if(found->kind==kernel::ViewerDimensionKind::Linear&&found->measurement_direction)
            near(std::abs(dot(delta,unit(*found->measurement_direction))),std::abs(value),1e-5);
        check(*live.open_part(peer_document)->session.document().find_container(peer)->sheet_form.definition==peer_definition,"Editing a FORM changed another insertion");
        if(!peer_geometry_checked) {
            auto recalculated_peer=peer_part;
            const auto peer_result=workspace::calculate_part_with_resolved_references(kernel,recalculated_peer);
            valid(peer_result.back());near(measured_volume(kernel,peer_result.back()),measured_volume(kernel,peer_cache.back()),1e-5);
            check(*recalculated_peer.find_container(peer)->sheet_form.definition==peer_definition,"Peer regeneration changed its native definition");
            peer_geometry_checked=true;
        }
        const auto second=value==0?.2:value*1.02;
        try {static_cast<void>(workspace::set_sheet_form_dimension(live,kernel,part.document_id,owner,key,second));}
        catch(const std::exception&){check(state->session.document().serialized()==changed,"Rejected successive edit changed FORM");}
        while(state->session.document().serialized()!=before)check(state->session.undo(),"FORM dimension Undo failed");
        check(state->session.redo(),"FORM dimension Redo failed");
        check(state->session.document().serialized()==changed,"FORM dimension Redo changed the result");
        const auto save=std::filesystem::absolute("build/form-diagnostic")/("editable-"+owner+".prtz");
        state->session.document().save(save,state->session.calculated_boundaries());
        std::vector<kernel::BodyResult> reopened;const auto saved=document::PartDocument::load(save,&reopened);valid(reopened.back());
        check(*saved.find_container(owner)->sheet_form.definition==*state->session.document().find_container(owner)->sheet_form.definition,"FORM dimension packet changed on reopen");
        check(state->session.undo(),"FORM dimension restore failed");++edited;
    }
    check(edited>0,"No authored FORM dimension accepted a numeric edit");
    check(*state->session.document().find_container(owner)->sheet_form.definition==factory,"FORM matrix did not restore independent definition");
    std::cout<<"FORM dimension matrix: edited="<<edited<<" rejected="<<rejected<<" read-only="<<readonly<<std::endl;return 0;
}
int prepare_outward_planar(const std::filesystem::path& input,const std::filesystem::path& output) {
    const auto source=document::read_sheet_form_definition(input);auto part=source.part;
    check(!source.corner(),"Expected a planar FORM");
    const auto request=document::sheet_form_request(source,{}, {},{0,1,0},{1,0,0},1.);
    const auto mesh=part.place_body_mesh(source.calculated->back().body_outputs.at(source.bodies[1])->mesh,source.bodies[1]);
    double low=0,high=0;
    for(const auto& p:mesh.vertices) {
        const auto height=kernel::sheet_material::dot(kernel::sheet_material::sub(p,request.source_origin),request.source_normal);
        low=std::min(low,height);high=std::max(high,height);
    }
    std::cout<<"Authored normal bounds "<<low<<" .. "<<high<<std::endl;
    check(low<-1.&&high<=1e-6,"Expected the factory skin on its reversed attachment side");
    part.form_outward=true;
    kernel::OcctKernel engine;const auto calculated=workspace::calculate_part_with_resolved_references(engine,part);
    check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Outward planar source calculation failed");
    static_cast<void>(document::sheet_form_definition(part,calculated));part.save(output,calculated);
    const auto reopened=document::read_sheet_form_definition(output);check(reopened.part.form_outward,"Outward intent lost on reopen");
    return 0;
}

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
    check(source.part.form_outward,"Factory planar FORMs must use the outward stock-side convention");
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
        // A valid connected result alone can hide a missing extrusion half:
        // the surviving half is still connected to the original stock.
        const auto& inserted=state->session.calculated_boundaries().back();
        const auto skin=document::sheet_form_request(source,{}, {},{0,1,0},{1,0,0},thickness);
        const auto inspect=document::sheet_form_internal_dimensions(feature);
        std::size_t expected_dimensions=0;
        for(const auto& sketch:source.part.sketches)expected_dimensions+=sketch.viewer_mesh().dimensions.size();
        check(!inspect.empty()&&inspect.size()==expected_dimensions,"FORM inspection omitted authored internal dimensions");
        check(std::ranges::all_of(inspect,[&](const auto& d){return d.reference.owner_id==feature.id&&document::is_sheet_form_internal_dimension(d.reference.semantic_key);}),"FORM inspection leaked inner reference ownership");
        gp_Trsf attachment;
        const auto& p=feature.placement;
        const kernel::Vec3 angles{p.rotation_x,p.rotation_y,p.rotation_z};
        const auto normal=document::construction_direction_from_local_axis("y",angles);
        const auto along=document::construction_direction_from_local_axis("x",angles);
        const auto verify_axis=[&](const document::PartDocument& model) {
            const auto mesh=model.construction_viewer_mesh();std::vector<kernel::ViewerAxis> axes;
            for(const auto& axis:mesh.axes)if(axis.reference.owner_id==feature.id&&axis.reference.semantic_key=="axis")axes.push_back(axis);
            check(axes.size()==1,"Planar FORM must expose one owned normal axis");
            check(kernel::sheet_material::dot(axes.front().direction,normal)>1.-1e-8,"Planar FORM axis is not normal to its selected stock side");
            near(axes.front().display_length,10.);near(axes.front().point.x,p.x);near(axes.front().point.y,p.y);near(axes.front().point.z,p.z);
            check(std::ranges::none_of(mesh.points,[&](const auto& point){return point.reference.owner_id==feature.id&&point.reference.semantic_key.starts_with("axis:point:");}),"Planar FORM axis has unexpected endpoint handles");
        };
        verify_axis(state->session.document());
        const auto outward=kernel::sheet_material::mul(face->surface->axis,face->surface->reversed?-1.:1.);
        check(kernel::sheet_material::dot(normal,outward)>1.-1e-8,"FORM attachment normal disagrees with the selected stock side");
        attachment.SetDisplacement(
            gp_Ax3(gp_Pnt(skin.source_origin.x,skin.source_origin.y,skin.source_origin.z),gp_Dir(skin.source_normal.x,skin.source_normal.y,skin.source_normal.z),gp_Dir(skin.source_x.x,skin.source_x.y,skin.source_x.z)),
            gp_Ax3(gp_Pnt(p.x,p.y,p.z),gp_Dir(normal.x,normal.y,normal.z),gp_Dir(along.x,along.y,along.z)));
        std::size_t index=0;
        for(const auto& sketch:source.part.sketches) {
            const auto* body=source.part.body_owner_for_object(sketch.owner_container_id);
            auto mesh=sketch.viewer_mesh();if(body)mesh=source.part.place_body_mesh(std::move(mesh),body->scope.id);
            for(const auto& dimension:mesh.dimensions) {
                const auto& actual=inspect.at(index++);near(actual.value,dimension.value);check(actual.kind==dimension.kind&&actual.driving==dimension.driving&&actual.locked==dimension.locked,"FORM inspection changed dimension state");
                const auto compare=[&](kernel::Vec3 a,kernel::Vec3 b){gp_Pnt point(a.x,a.y,a.z);point.Transform(attachment);near(b.x,point.X());near(b.y,point.Y());near(b.z,point.Z());};
                compare(dimension.witness_first,actual.witness_first);compare(dimension.witness_second,actual.witness_second);
                compare(dimension.line_first,actual.line_first);compare(dimension.line_second,actual.line_second);
            }
        }
        const auto target=kernel::read_calculated_body_shape(inserted);
        const auto sample=[&](double x,double y,double height) {
            const auto tangent=kernel::sheet_material::cross(normal,along);
            const auto point=kernel::sheet_material::add({p.x,p.y,p.z},kernel::sheet_material::add(kernel::sheet_material::mul(along,x),kernel::sheet_material::add(kernel::sheet_material::mul(tangent,y),kernel::sheet_material::mul(normal,height))));
            return BRepClass3d_SolidClassifier(target,gp_Pnt(point.x,point.y,point.z),1e-7).State();
        };
        check(sample(0,0,-thickness*.5)==TopAbs_OUT,"FORM replacement cut left the original stock under its opening");
        check(sample(40,40,-thickness*.5)==TopAbs_IN,"FORM cut removed stock outside its replacement region");
        double low=0,high=0;
        for(const auto& v:inserted.mesh.vertices) {
            const auto height=kernel::sheet_material::dot(kernel::sheet_material::sub(v,{p.x,p.y,p.z}),normal);
            low=std::min(low,height);high=std::max(high,height);
        }
        check(low<-5.&&high<=1e-6,"FORM material is on the wrong side of the selected stock face");
        if(!dimple) {
            BRepAlgoAPI_Cut raised(target,kernel::read_calculated_body_shape(baseline.back()));check(raised.IsDone(),"Cannot isolate the raised bridge");
            gp_Trsf reflection;reflection.SetMirror(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,1,0),gp_Dir(1,0,0)));
            const auto mirrored=BRepBuilderAPI_Transform(raised.Shape(),reflection,true).Shape();
            BRepAlgoAPI_Common shared(raised.Shape(),mirrored);check(shared.IsDone(),"Cannot compare bridge halves");
            GProp_GProps original,both;BRepGProp::VolumeProperties(raised.Shape(),original);BRepGProp::VolumeProperties(shared.Shape(),both);
            check(original.Mass()>1.,"Raised bridge is empty");near(both.Mass(),original.Mass(),1e-4);
        }
        const auto skin_mesh=source.part.place_body_mesh(source.calculated->back().body_outputs.at(source.bodies[1])->mesh,source.bodies[1]);
        for(const auto& vertex:skin_mesh.vertices) {
            check(kernel::sheet_material::dot(kernel::sheet_material::sub(vertex,skin.source_origin),skin.source_normal)<=1e-6,"Factory FORM skin does not point into the selected sheet face");
            auto point=vertex;
            gp_Pnt world(point.x,point.y,point.z);world.Transform(attachment);
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(world).Shape(),target);
            check(distance.IsDone()&&distance.Value()<=1e-5,"Inserted FORM moved or lost its authored starting skin");
        }
        const auto spatial=document::serialize_body_result(inserted);
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
        verify_axis(saved);verify_axis(cold_part);verify_axis(state->session.document());
        const auto rendered=symbols::instance_mesh(packets.front().symbols.front().symbol);check(rendered.edges.size()==(dimple?2u:4u)&&std::ranges::all_of(rendered.edges,[](const auto& e){return e.construction&&e.dash_dot;}),"Drawing symbol lost its construction strokes");
        const auto before_dimension=state->session.document().serialized();
        const auto placed_before=*state->session.document().find_container(feature.id);bool dimension_edited=false;
        for(const auto& dimension:inspect)if(dimension.value!=0&&document::sheet_form_internal_dimension_editable(placed_before,dimension.reference.semantic_key)) {
            try {dimension_edited=workspace::set_sheet_form_dimension(live,kernel,part.document_id,feature.id,dimension.reference.semantic_key,dimension.value*1.01);}
            catch(const std::exception&){check(state->session.document().serialized()==before_dimension,"Rejected sided FORM edit mutated the model");}
            if(!dimension_edited)continue;
            const auto* after=state->session.document().find_container(feature.id);valid(state->session.calculated_boundaries().back());
            check(after->placement==placed_before.placement&&after->sheet_form.support==placed_before.sheet_form.support,"FORM dimension changed its selected side or attachment");
            check(state->session.undo()&&state->session.document().serialized()==before_dimension,"Sided FORM dimension Undo failed");break;
        }
        check(dimension_edited,"No FORM dimension was editable for a tested thickness/side");
        std::cout<<source.part.name<<" thickness="<<thickness<<" bottom="<<bottom<<" insertion, precut volume, Unbend/Bend Back, Undo/Redo and reopen passed"<<std::endl;
    }
    check(source.part.serialized(*source.calculated)==immutable,"Dimple insertion mutated the library");return 0;
}
