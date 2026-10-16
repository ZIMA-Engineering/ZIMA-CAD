void verify_supported_angle_matrix() {
    std::ifstream input("cpp/tests/fixtures/sketch/hex-bolt-head-angle.json");
    const auto fixture=sketcher::Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));
    unsigned scenarios=0;
    for(unsigned rotation=0;rotation<4;++rotation)for(bool mirror:{false,true})
    for(bool reverse:{false,true})for(bool native:{false,true})for(bool locked:{false,true}) {
        auto sketch=fixture;
        const auto transform=[&](double& x,double& y){if(mirror)x=-x;for(unsigned i=0;i<rotation;++i){const auto old=x;x=-y;y=old;}};
        for(auto& point:sketch.points)transform(point.x,point.y);
        for(auto& ref:sketch.external_references)for(auto& point:ref.cached_points)transform(point[0],point[1]);
        for(auto& c:sketch.constraints)if(rotation%2) {
            if(c.geometry_id=="sketch_axis:x")c.geometry_id="sketch_axis:y";
            else if(c.geometry_id=="sketch_axis:y")c.geometry_id="sketch_axis:x";
        }
        if(native) {
            for(const auto& ref:sketch.external_references) {
                auto first=sketch.points.front(),second=first;first.id=ref.id+":a";second.id=ref.id+":b";
                first.x=ref.cached_points[0][0];first.y=ref.cached_points[0][1];first.fixed=true;first.construction=true;
                second.x=ref.cached_points[1][0];second.y=ref.cached_points[1][1];second.fixed=true;second.construction=true;
                // Native point-on-segment supports are finite. Enclose the
                // edited profile instead of copying the infinite face's handles.
                const double cx=(first.x+second.x)*.5,cy=(first.y+second.y)*.5;
                const double dx=second.x-first.x,dy=second.y-first.y,length=std::hypot(dx,dy);
                first.x=cx-100*dx/length;first.y=cy-100*dy/length;
                second.x=cx+100*dx/length;second.y=cy+100*dy/length;
                auto line=sketch.segments.front();line.id=ref.id;line.first_point_id=first.id;line.second_point_id=second.id;line.construction=true;
                sketch.points.push_back(first);sketch.points.push_back(second);sketch.segments.push_back(line);
            }
            sketch.external_references.clear();
        }
        auto& dimension=sketch.dimensions.front();dimension.locked=locked;
        if(reverse)std::swap(dimension.geometry_id,dimension.second_geometry_id);
        if(mirror!=reverse)dimension.value=-dimension.value;
        const auto references=Json::parse(sketch.serialized()).at("external_references");
        const auto measured=[&] {
            const auto& d=sketch.dimensions.front();
            const auto a=std::ranges::find(sketch.segments,d.geometry_id,&sketcher::SketchSegment::id);
            const auto b=std::ranges::find(sketch.segments,d.second_geometry_id,&sketcher::SketchSegment::id);
            const auto p=sketch.find_point(a->first_point_id),q=sketch.find_point(a->second_point_id);
            const auto r=sketch.find_point(b->first_point_id),s=sketch.find_point(b->second_point_id);
            const double ax=q->x-p->x,ay=q->y-p->y,bx=s->x-r->x,by=s->y-r->y;
            return std::acos(std::clamp((ax*bx+ay*by)/(std::hypot(ax,ay)*std::hypot(bx,by)),-1.,1.))*180./std::acos(-1.);
        };
        for(double magnitude:{30.,35.,45.,25.}) {
            const auto before=sketch.points;const auto sign=std::copysign(1.,sketch.dimensions.front().value);
            if(!sketch.set_dimension_value(sketch.dimensions.front().id,sign*magnitude)) {
                throw std::runtime_error("Supported angle failed rotation="+std::to_string(rotation)+" mirror="+std::to_string(mirror)+" reverse="+std::to_string(reverse)+" native="+std::to_string(native)+" locked="+std::to_string(locked)+" value="+std::to_string(sign*magnitude));
            }
            require(std::abs(measured()-magnitude)<1e-6,"Supported angle violates its geometric equation");
            require(sketch.points!=before,"Supported angle accepted without movement");
            require(Json::parse(sketch.serialized()).at("external_references")==references,"Supported angle changed read-only references");
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());
            require(std::abs(measured()-magnitude)<1e-6,"Supported angle changed on native round trip");
        }
        auto reference=sketch;auto reference_dimension=reference.dimensions.front();reference_dimension.driving=false;reference_dimension.locked=false;reference.apply_dimension(reference_dimension);const auto reference_points=reference.points;
        require(reference.set_dimension_value(reference.dimensions.front().id,99.)&&reference.points==reference_points,"Reference angle changed geometry");
        auto constrained=sketch;constrained.dimensions.front().locked=true;
        const auto drag_id=fixture.segments[1].second_point_id;
        double step_x=.25,step_y=0.;transform(step_x,step_y);
        for(unsigned drag=0;drag<3;++drag) {
            const auto before=constrained.points;const auto* p=constrained.find_point(drag_id);
            static_cast<void>(constrained.move_point(drag_id,p->x+step_x,p->y+step_y));
            require(constrained.points==before,"Fully constrained angle escaped during a point drag");
        }
        auto free=constrained;
        std::erase_if(free.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Perpendicular;});
        for(unsigned drag=0;drag<3;++drag) {
            const auto before=free.points;const auto* p=free.find_point(drag_id);
            require(free.move_point(drag_id,p->x+step_x,p->y+step_y)&&free.points!=before,"Underconstrained supported profile did not move");
            require(Json::parse(free.serialized()).at("external_references")==references,"Point drag changed read-only references");
        }
        auto fixed=sketch;for(auto& p:fixed.points)p.fixed=true;const auto unchanged=fixed.serialized();
        require(!fixed.set_dimension_value(fixed.dimensions.front().id,60.)&&fixed.serialized()==unchanged,"Impossible angle changed fixed geometry");
        double shift_x=0.,shift_y=-.2;transform(shift_x,shift_y);
        const auto support_id=fixture.external_references.front().id;
        if(native)for(const auto& suffix:{":a",":b"}) {
            auto* p=sketch.find_point(support_id+suffix);p->x+=shift_x;p->y+=shift_y;
        } else {
            auto& support=sketch.external_references.front();for(auto& p:support.cached_points){p[0]+=shift_x;p[1]+=shift_y;}
        }
        const auto updated_references=Json::parse(sketch.serialized()).at("external_references");
        const auto before_refresh=sketch.points;
        const auto refreshed=sketch.solve();
        require(refreshed.status!=sketcher::SolveStatus::Conflicting&&refreshed.status!=sketcher::SolveStatus::Invalid,"Updated support lost its angular profile");
        require(sketch.points!=before_refresh&&std::abs(measured()-25.)<1e-6,"Updated support did not move while retaining its angle");
        require(Json::parse(sketch.serialized()).at("external_references")==updated_references,"Support solving changed read-only reference data");
        ++scenarios;
    }
    std::cout<<"Supported native/external angular profiles: "<<scenarios<<" successive-edit matrices passed\n";
}
