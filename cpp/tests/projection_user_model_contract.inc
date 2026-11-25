// Optional reproduction on an isolated copy of the reported 50.prtz model.
void verify_projection_user_model(const std::filesystem::path& path) {
    std::vector<kernel::BodyResult> cache;
    auto part=document::PartDocument::load(path,&cache);
    auto feature=std::ranges::find_if(part.history,[](const auto& item) {
        return item.feature_kind==document::FeatureKind::Feature&&
            item.feature.type==document::FeatureType::Projection&&item.feature.projection.targets.size()==2;
    });
    for(auto it=part.history.begin();it!=part.history.end();++it)
        if(it->feature_kind==document::FeatureKind::Feature&&it->feature.type==document::FeatureType::Projection&&
                it->feature.projection.output==document::ProjectionOutput::Relief&&it->feature.projection.targets.size()==2)feature=it;
    require(feature!=part.history.end(),"User reproduction needs the two-face Projection");
    auto edited=*feature;const auto owner=edited.id,id=part.document_id;
    const auto source=std::ranges::find(part.sketches,edited.feature.sketch_id,&sketcher::Sketch::id);
    require(source!=part.sketches.end(),"User Projection source is missing");
    double ymin=1e100,ymax=-1e100,xmin=1e100,xmax=-1e100;
    for(const auto& point:source->points)if(!point.construction) {
        ymin=std::min(ymin,point.y);ymax=std::max(ymax,point.y);
        xmin=std::min(xmin,point.x);xmax=std::max(xmax,point.x);
    }
    const auto volume=[](const kernel::BodyResult& result) {
        const auto shape=kernel::read_calculated_body_shape(result);
        require(BRepCheck_Analyzer(shape).IsValid(),"User Projection produced an invalid native Body");
        GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass,1e-10);return mass.Mass();
    };
    workspace::Workspace live;live.add_part(part,cache);live.activate(id);kernel::OcctKernel engine;
    const auto* input=workspace::calculated_operation_input(live.open_part(id)->session,owner);
    require(input,"User relief input boundary is missing");const auto initial=volume(*input);
    const auto unchanged=live.open_part(id)->session.document().serialized();
    const auto generation=live.open_part(id)->session.data_generation();
    const auto calls=kernel::body_calculation_count.load();
    workspace::commit_profile(live,engine,id,edited,workspace::ProfileEditMode::Replace);
    require(kernel::body_calculation_count.load()==calls&&live.open_part(id)->session.data_generation()==generation,
        "User unchanged Projection calculated or created a transaction");
    edited.feature.projection.output=document::ProjectionOutput::Relief;
    edited.feature.projection.height=3.;edited.combine_mode=document::CombineMode::Subtract;
    edited.feature.projection.regions.clear();
    double radius=30.;
    for(const auto& face:input->mesh.triangle_references)if(face.owner_id==edited.feature.projection.targets.front().reference.owner_id&&
            face.semantic_key==edited.feature.projection.targets.front().reference.semantic_key) {
        const auto s=face.surface?face.surface:face.contour_surface;
        if(s&&s->kind==kernel::SurfaceGeometry::Kind::Cylinder)radius=s->radius;
    }
    std::cout<<"User rectangle bounds="<<xmin<<','<<xmax<<','<<ymin<<','<<ymax<<" cylinder radius="<<radius<<" input volume="<<initial<<std::endl;
    const double angle=std::asin(ymax/radius)-std::asin(ymin/radius);
    for(bool directional:{false,true})for(bool inward:{true,false}) {
        edited.feature.projection.relief_direction=directional?document::ProjectionReliefDirection::Projection:document::ProjectionReliefDirection::SurfaceNormal;
        edited.combine_mode=inward?document::CombineMode::Subtract:document::CombineMode::Add;
        const auto start=std::chrono::steady_clock::now();
        workspace::commit_profile(live,engine,id,edited,workspace::ProfileEditMode::Replace);
        std::cout<<"User relief changed OK ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<std::endl;
        auto* state=live.open_part(id);
        const auto* output=workspace::calculated_operation_output(state->session,owner);
        require(output,"User relief output boundary is missing");const auto& result=*output;
        const auto* current_input=workspace::calculated_operation_input(state->session,owner);
        const auto& actual_source=*std::ranges::find(state->session.document().sketches,edited.feature.sketch_id,&sketcher::Sketch::id);
        std::cout<<"User current input volume="<<(current_input?volume(*current_input):0)<<" source origin="
            <<actual_source.resolved_origin.x<<','<<actual_source.resolved_origin.y<<','<<actual_source.resolved_origin.z
            <<" source normal="<<actual_source.normal().x<<','<<actual_source.normal().y<<','<<actual_source.normal().z<<std::endl;
        require(result.calculation_errors.empty(),"User relief left an unresolved feature");
        const double h=inward?-3.:3.,expected=directional?(xmax-xmin)*(ymax-ymin)*h:
            angle*(xmax-xmin)*((radius+h)*(radius+h)-radius*radius)/2.;
        const auto actual=volume(result)-initial;
        std::cout<<"User relief directional="<<directional<<" inward="<<inward<<" volume delta="<<actual<<" expected="<<expected<<'\n';
        require(std::abs(actual-expected)<1e-3,"User relief disagrees with independent cylindrical layer volume");
        require(!TopExp_Explorer(kernel::read_calculated_body_shape(result),TopAbs_FACE,TopAbs_SOLID).More(),
            "User relief left a loose surface inside the Sketch");
        edited=*state->session.document().find_container(owner);
        const auto saved=state->session.calculated_boundaries();
        require(state->session.undo()&&state->session.redo(),"User relief Undo/Redo failed");
        require(state->session.calculated_boundaries().back().kernel_shape==saved.back().kernel_shape,
            "User relief Undo/Redo changed native geometry");
        std::vector<kernel::BodyResult> reopened;
        const auto restored=document::PartDocument::from_serialized(state->session.document().serialized(saved),&reopened);
        require(restored.find_container(owner)->feature.projection==edited.feature.projection&&
            reopened.back().kernel_shape==saved.back().kernel_shape,
            "User relief native roundtrip changed regions or geometry");
    }
    std::cout<<"User Projection: unchanged OK, automatic Sketch area across two adjacent faces, both depth directions, +/-3 mm volume, Undo/Redo and native roundtrip passed\n";
}
