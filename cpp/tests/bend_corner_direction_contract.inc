void verify_corner_direction() {
    using namespace kernel::sheet_material;
    unsigned cases{};
    for(const bool rotated:{false,true})for(const bool reversed:{false,true})
    for(const double angle:{45.,90.,120.,135.})for(const double gap:{0.,.05}) {
        auto part=document::PartDocument::create_default();
        auto feature=document::PartDocument::create_sketch_container();
        feature.feature_kind=document::FeatureKind::Bend;
        auto start=sketcher::Sketch::create_default();start.owner_container_id=feature.id;
        static_cast<void>(start.add_segment(reversed?20.:-20.,0,reversed?-20.:20.,0));
        feature.bend.sketch_id=start.id;feature.bend.radius=5;
        feature.bend.thickness=1;feature.bend.thickness_override=true;
        feature.bend.angle_degrees=angle;feature.bend.corner={true,true};feature.bend.corner_gap=gap;
        if(rotated) {
            feature.placement.x=75;feature.placement.y=-31;feature.placement.z=22;
            feature.placement.absolute_rotation_x=31;feature.placement.absolute_rotation_y=22;
            feature.placement.absolute_rotation_z=53;
        }
        part.history={feature};part.sketches={start};part.resolve_constructions();
        auto& current=part.history.front();const auto& sketch=part.sketches.front();
        document::prepare_bend_sketches(current,sketch,{});
        const auto& line=sketch.segments.front();
        const auto* a=sketch.find_point(line.first_point_id);const auto* b=sketch.find_point(line.second_point_id);
        const auto first=sketch.world_point(a->x,a->y),last=sketch.world_point(b->x,b->y);
        const auto along=unit(sub(last,first)),normal=sketch.resolved_normal,inward=cross(normal,along);
        // Each corner is the intersection with a plane. For a narrowing end
        // that plane follows the lateral displacement of the cylinder; for a
        // widening end it follows the initial tangent displacement. Check the
        // independent plane equation, rather than restating the interpolation.
        for(const auto ends:{std::array{-5.,-5.},std::array{5.,5.},std::array{-3.,4.},std::array{-5.,-5.}})
        for(const auto coefficients:{std::array{1.,1.},std::array{0.,0.},std::array{.25,.75},std::array{.5,.5},std::array{.75,.25}})
        for(const auto direction:{std::optional<bool>{},std::optional<bool>{false},std::optional<bool>{true}}) {
            current.bend.corner=coefficients;
            current.bend.corner_reversed={direction,direction};
            document::set_bend_profile_extensions(current,ends[0],ends[1]);
            const double radians=angle*std::numbers::pi/180.;
            const auto check_point=[&](kernel::Vec3 point,std::size_t end) {
                const auto delta=sub(point,first);
                const double s=end?40-dot(delta,along):dot(delta,along);
                const double h=dot(delta,inward),v=dot(delta,normal);
                const double magnitude=ends[end]-gap*.5;
                const double curved=direction.value_or(ends[end]<0)?
                    -magnitude/5*(h-1)-magnitude/5/std::tan(radians)*v:
                    -magnitude/(5*std::sin(radians))*v;
                const double fraction=std::atan2(v,6-h)/radians;
                const double equation=s-(coefficients[end]*curved-
                    (1-coefficients[end])*ends[end]*fraction);
                check(std::abs(equation)<1e-7,"Bend corner leaves its analytical meeting plane");
            };
            const auto preview=document::bend_preview(current,sketch,{});
            for(const auto& edge:preview.edges)for(const std::size_t end:{0U,1U})
                if(edge.reference.semantic_key.find(end?"inner-last":"inner-first")!=std::string::npos)
                    for(const auto point:edge.points)check_point(point,end);
            const auto group=document::bend_request(current,sketch,{});
            const auto& sweep=std::get<kernel::Sweep3DRequest>(group.children.front());
            for(const auto& section:sweep.sections) {
                const auto& polygon=std::get<kernel::ExtrusionRequest::PolygonProfile>(section.profile.outer_profile);
                check_point(polygon.vertices[3],0);check_point(polygon.vertices[2],1);
            }
            const auto saved=part.serialized();
            check(document::PartDocument::from_serialized(saved).serialized()==saved,
                "Bend corner direction failed native readback");
            ++cases;
        }
    }
    std::cout<<cases<<" Bend corner plane cases: widening/narrowing, rotations, both endpoint orders, gaps and repeated edits passed\n";
}
