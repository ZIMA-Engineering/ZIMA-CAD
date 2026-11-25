// Optional reproduction on an isolated copy of the reported 50.prtz model.
void verify_projection_user_model(const std::filesystem::path& path) {
    std::vector<kernel::BodyResult> cache;
    auto part=document::PartDocument::load(path,&cache);
    const auto feature=std::ranges::find_if(part.history,[](const auto& item) {
        return item.feature_kind==document::FeatureKind::Feature&&
            item.feature.type==document::FeatureType::Projection&&item.feature.projection.targets.size()==2;
    });
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
        GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);return mass.Mass();
    };
    const auto initial=volume(cache.back());
    workspace::Workspace live;live.add_part(part,cache);live.activate(id);kernel::OcctKernel engine;
    const auto unchanged=live.open_part(id)->session.document().serialized();
    const auto generation=live.open_part(id)->session.data_generation();
    const auto calls=kernel::body_calculation_count.load();
    workspace::commit_profile(live,engine,id,edited,workspace::ProfileEditMode::Replace);
    require(kernel::body_calculation_count.load()==calls&&live.open_part(id)->session.data_generation()==generation,
        "User unchanged Projection calculated or created a transaction");
    edited.feature.projection.output=document::ProjectionOutput::Relief;
    edited.feature.projection.height=3.;edited.combine_mode=document::CombineMode::Subtract;
    bool rejected=false;
    try {workspace::commit_profile(live,engine,id,edited,workspace::ProfileEditMode::Replace);}
    catch(const workspace::ProfileOperationError&){rejected=true;}
    require(rejected&&kernel::body_calculation_count.load()==calls&&
        live.open_part(id)->session.data_generation()==generation&&
        live.open_part(id)->session.document().serialized()==unchanged,
        "User missing-region relief changed the document or calculated geometry");
    const double t=(ymin+ymax)/2.,radius=30.;
    const auto n=source->normal(),u=source->resolved_y_axis;
    const kernel::Vec3 center{0,118.8051986694336,0};
    const double radial=std::sqrt(radius*radius-t*t);
    for(std::size_t i=0;i<edited.feature.projection.targets.size();++i)
        edited.feature.projection.regions.push_back({edited.feature.projection.targets[i].reference,
            {center.x+n.x*radial+u.x*t,center.y+n.y*radial+u.y*t,i==0?4.:-4.},{}});
    const double angle=std::asin(ymax/radius)-std::asin(ymin/radius);
    for(bool inward:{true,false}) {
        edited.combine_mode=inward?document::CombineMode::Subtract:document::CombineMode::Add;
        workspace::commit_profile(live,engine,id,edited,workspace::ProfileEditMode::Replace);
        auto* state=live.open_part(id);const auto& result=state->session.calculated_boundaries().back();
        require(result.calculation_errors.empty(),"User relief left an unresolved feature");
        const double h=inward?-3.:3.,expected=angle*(xmax-xmin)*((radius+h)*(radius+h)-radius*radius)/2.;
        const auto actual=volume(result)-initial;
        std::cout<<"User relief inward="<<inward<<" volume delta="<<actual<<" expected="<<expected<<'\n';
        require(std::abs(actual-expected)<1e-3,"User relief disagrees with independent cylindrical layer volume");
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
    std::cout<<"User Projection: unchanged OK, immediate missing-region rejection, two adjacent regions, +/-3 mm volume, Undo/Redo and native roundtrip passed\n";
}
