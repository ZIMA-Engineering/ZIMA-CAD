// Included by bend_command_tests.cpp before main. These authored coordinates
// isolate the first Sheet Cut from the reported model without retaining any
// user document or cached topology. The attachment edge's 1.47e-6 mm tilt is
// intentional: it exercises existing vertex tolerances during UV transfer.
void verify_sheet_cut_tilted_cone_attachment(std::filesystem::path directory) {
    kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
    options.settings=[] {command_host::Settings settings;
        settings.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};
        return settings;};
    command_host::Host host(live,kernel,directory,options);
    run(host,"new",{{"type","part"},{"name","sheet-cut-tilted-cone-attachment"}});
    const auto id=live.active_document_id();auto* state=live.open_part(id);
    auto profile=sketcher::Sketch::create_default();
    const std::array<std::pair<double,double>,6> points={
        std::pair{-99.99999999405615,-82.71272615330159},
        std::pair{100.00000000164175,-82.71272615330159},
        std::pair{98.63269130097008,17.287272375734762},
        std::pair{58.63269130097008,17.287272375734762},
        std::pair{58.6326913091286,67.28727237573477},
        std::pair{-100.00000000221468,67.2872738466984}};
    for(std::size_t i=0;i<points.size();++i) {
        const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
        static_cast<void>(profile.add_segment(a.first,a.second,b.first,b.second));
    }
    auto flat=document::PartDocument::create_sketch_container();flat.feature_kind=document::FeatureKind::Flat;
    flat.flat.sketch_id=profile.id;flat.flat.thickness=5.;flat.flat.thickness_override=true;
    flat.placement.absolute_rotation_x=flat.placement.rotation_x=-90.;profile.owner_container_id=flat.id;
    static_cast<void>(workspace::commit_flat(live,kernel,id,flat,profile));
    const auto geometry=state->session.calculated_boundaries().back().mesh.original_references;
    const auto boundary=std::ranges::find_if(geometry.edges,[](const auto& edge) {
        return kernel::sheet_edge_role(edge)==kernel::SheetEdgeRole::Boundary&&edge.points.size()>=2&&
            std::ranges::all_of(edge.points,[](const auto& point) {
                return std::abs(point.y-5.)<1e-6&&std::abs(point.z+67.287273)<2e-6;
            })&&std::abs(std::abs(edge.points.front().x-edge.points.back().x)-158.63269131134328)<1e-5;
    });
    check(boundary!=geometry.edges.end(),"Tilted-cone regression lost its Flat attachment edge");
    check(boundary->edge_treatment_endpoint_references.size()==2,
        "Tilted-cone attachment edge lost its authored endpoint references");
    const auto origin=boundary->edge_treatment_endpoint_references[
        boundary->points.front().x>boundary->points.back().x?0:1];
    auto generator=sketcher::Sketch::create_default();
    auto cone=document::PartDocument::create_revolution_container(generator.id);
    document::initialize_sheet_revolution(cone,generator,document::sheet_metal_defaults(state->session.document()));
    cone.revolution.sheet_attachment=true;cone.revolution.angle_degrees=61.764;
    cone.revolution.thin_thickness=5.;cone.revolution.direction=document::ExtrusionDirection::Forward;
    cone.placement.references=document::bend_sheet_references(*boundary,origin);
    const auto axis=std::ranges::find(generator.segments,cone.revolution.axis_segment_id,&sketcher::SketchSegment::id);
    check(axis!=generator.segments.end(),"Tilted-cone regression lost its axis");
    auto* first=generator.find_point(axis->first_point_id);auto* last=generator.find_point(axis->second_point_id);
    first->x=2.7052529105731082e-9;first->y=40.000000005010236;
    last->x=-43.25460296502805;last->y=47.626953548054054;
    workspace::commit_profile(live,kernel,id,cone,workspace::ProfileEditMode::Create,generator);
    const double before=state->session.calculated_boundaries().back().volume;
    auto rectangle=sketcher::Sketch::create_default();
    static_cast<void>(rectangle.add_rectangle(-27.881166458129883,-17.369756301024097,
        27.881166458129883,86.27262540302922));
    auto cut=document::PartDocument::create_extrusion_container(rectangle.id);
    cut.combine_mode=document::CombineMode::Subtract;cut.extrusion.sheet_cut=true;
    cut.extrusion.sheet_cut_clearance=true;cut.extrusion.direction=document::ExtrusionDirection::Reverse;
    cut.extrusion.extent_mode=document::ProfileExtentMode::OneSide;
    cut.extrusion.end_condition_forward=document::EndCondition::Length;cut.extrusion.length_forward=16.023;
    cut.placement.absolute_rotation_x=cut.placement.rotation_x=-90.;cut.extrusion.profile_plane_offset=13.;
    workspace::commit_profile(live,kernel,id,cut,workspace::ProfileEditMode::Create,rectangle);
    const auto& result=state->session.calculated_boundaries().back();
    check(result.calculation_errors.empty()&&result.volume>0&&result.volume<before,
        "Clearance cut failed across a slightly tilted Flat/cone attachment");
    std::set<std::string> owners;
    for(const auto& region:result.sheet_cuts)if(region.cut_owner==cut.id)owners.insert(region.source.owner_id);
    check(owners==std::set<std::string>{flat.id,cone.id},
        "Tilted-cone clearance cut did not preserve both material-region owners");
    for(const auto& face:result.mesh.triangle_references)check(face.valid(),
        "Tilted-cone clearance cut generated anonymous topology");
    kernel::BoxRequest prism(55.762332916259766,2000.,103.64238170405332);
    prism.translation={-27.881166458129883,-1000.,-86.27262540302922};
    const auto pin=kernel.make_box(prism);
    near(kernel.subtract_bodies(result,pin,{},{},1e-7).volume,result.volume);
    const double cut_volume=result.volume;
    run(host,"regenerate");near(state->session.calculated_boundaries().back().volume,cut_volume);
}
