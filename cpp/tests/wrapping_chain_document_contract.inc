static void wrapping_chain_document_contract() {
    using namespace kernel::wrap_detail;
    workspace::Workspace live;kernel::OcctKernel engine;
    auto part=document::PartDocument::create_default();const auto id=part.document_id;
    document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Chained Wrap"));part.set_body_history(std::move(graph));
    live.add_part(part);live.activate(id);
    auto outline=sketcher::Sketch::create_default();outline.plane_auto=false;
    static_cast<void>(outline.add_arc(0,0,10,0,0,10));
    static_cast<void>(outline.add_arc(0,5,0,10,-5,5));
    for(const auto points:{std::array<double,4>{-5,5,-5,-10},{-5,-10,10,-10},{10,-10,10,0}})
        static_cast<void>(outline.add_segment(points[0],points[1],points[2],points[3]));
    auto base=document::PartDocument::create_feature_container(outline.id);base.feature.sides[0].length=30;
    workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,outline);
    auto* state=live.open_part(id);std::array<kernel::FaceReference,3> targets;
    for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references) {
        if(!face.surface)continue;const auto& s=*face.surface;
        if(s.kind==kernel::SurfaceGeometry::Kind::Cylinder)targets[std::abs(s.radius-10)<1e-6?0:1]=face;
        if(s.kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(s.origin.x+5)<1e-6&&std::abs(s.axis.x)>.99)targets[2]=face;
    }
    require(std::ranges::all_of(targets,[](const auto& face){return face.valid();}),"Native Sketch chain target missing");
    auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
    const double upper=5*std::numbers::pi+8;
    static_cast<void>(sketch.add_rectangle(-3,-4,upper,4));
    sketcher::TemplateImage image;image.id="chain-artwork";image.name="Chain artwork";image.format="svg";
    image.x=-3;image.y=-4;image.width=upper+3;image.height=8;image.pixel_width=100;image.pixel_height=30;
    image.data_base64="PHN2Zy8+";image.template_coordinates=false;sketch.reference_images.push_back(image);
    auto feature=document::PartDocument::create_feature_container(sketch.id);feature.feature.type=document::FeatureType::Wrapping;
    auto& wrap=feature.feature.wrapping;wrap.output=document::WrappingOutput::Curves;wrap.target.emplace();wrap.target->reference=targets[0];
    for(int index:{0,1,2,2}) {auto target=*wrap.target;target.reference=targets[index];wrap.continuations.push_back(target);}
    feature.placement.x=feature.placement.y=10/std::sqrt(2.);feature.placement.z=15;
    feature.placement.rotation_x=90;feature.placement.rotation_z=135;
    feature.placement.absolute_rotation_x=90;feature.placement.absolute_rotation_z=135;
    workspace::commit_profile(live,engine,id,feature,workspace::ProfileEditMode::Create,sketch);valid(state->session.calculated_boundaries());
    feature=*state->session.document().find_container(feature.id);
    const auto* input=workspace::calculated_operation_input(state->session,feature.id);require(input,"Chained Wrap input missing");
    const auto calls=kernel::body_calculation_count.load();
    const auto preview=state->session.document().wrapping_preview_mesh(feature,input->mesh,&state->session.document(),true);
    if(preview.edges.empty()||preview.images.size()!=1||preview.images.front().surface_vertices.empty())
        std::cerr<<"Chained preview edges="<<preview.edges.size()<<" images="<<preview.images.size()<<" input triangles="<<input->mesh.triangles.size()<<'\n';
    require(!preview.edges.empty()&&preview.images.size()==1&&!preview.images.front().surface_vertices.empty(),"Chained Wrap preview is incomplete");
    auto compact=input->mesh;
    for(auto& face:compact.triangle_references){face.surface.reset();face.contour_surface.reset();}
    const auto reopened_preview=state->session.document().wrapping_preview_mesh(feature,compact,&state->session.document(),true);
    require(reopened_preview.edges.size()==preview.edges.size()&&reopened_preview.images.size()==preview.images.size(),"Compact display tags changed Wrap preview");
    for(std::size_t i=0;i<preview.edges.size();++i)
        require(reopened_preview.edges[i].points==preview.edges[i].points,"Compact display tags moved Wrap preview");
    require(reopened_preview.images.front().surface_vertices==preview.images.front().surface_vertices,"Compact display tags changed Wrap image bounds");
    require(kernel::body_calculation_count.load()==calls,"Chained preview calculated geometry");
    const auto identities=[&](const kernel::BodyResult& result){std::set<std::string> keys;
        for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id==feature.id)keys.insert(edge.reference.semantic_key);return keys;};
    const auto keys=identities(state->session.calculated_boundaries().back());
    const auto file=std::filesystem::temp_directory_path()/"zima-chained-wrap.prtz";
    state->session.document().save(file,state->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cached;auto restored=document::PartDocument::load(file,&cached);valid(cached);
    require(restored.find_container(feature.id)->feature.wrapping==feature.feature.wrapping,"Chained Wrap lost authored target order or redundant rows");
    kernel::OcctKernel cold;const auto fresh=workspace::calculate_part_with_resolved_references(cold,restored);valid(fresh);
    require(identities(fresh.back())==keys,"Cold chained Wrap changed source ancestry");
    require(fresh.back().mesh.images.size()==1&&fresh.back().mesh.images.front().data_base64==image.data_base64,"Cold chained Wrap lost artwork");
    require(std::abs(volume_of(fresh.back())-volume_of(cached.back()))<1e-7,"Cold chained Wrap changed material");
    require(state->session.undo()&&state->session.redo(),"Chained Wrap Undo/Redo unavailable");
    require(state->session.document().find_container(feature.id)->feature.wrapping==feature.feature.wrapping,"Chained Wrap Undo/Redo lost target order");
    std::filesystem::remove(file);
    std::cout<<"PASS native Sketch arc-arc-line Wrap, redundant rows, preview/artwork, identities, Undo/Redo and cold native reopen\n";
}
