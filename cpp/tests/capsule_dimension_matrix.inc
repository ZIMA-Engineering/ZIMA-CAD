void verify_capsule_dimensions() {
    std::ifstream input(std::filesystem::path(__FILE__).parent_path()/"fixtures/sketch/capsule-line-distance.json");
    const auto original=Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));
    const auto check_geometry=[&](const Sketch& s,double width,double length=100) {
        for(const auto& arc:s.arcs) {
            require(near(arc.radius,width/2),"Capsule radius equation failed");
            const auto* center=s.find_point(arc.center_point_id);
            for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
                const auto* p=s.find_point(id);
                require(near(std::hypot(p->x-center->x,p->y-center->y),arc.radius),"Capsule endpoint left circle");
                for(const auto& line:s.segments)if(line.first_point_id==id||line.second_point_id==id) {
                    const auto* q=s.find_point(line.first_point_id==id?line.second_point_id:line.first_point_id);
                    const double length=std::hypot(q->x-p->x,q->y-p->y);
                    require(near(((p->x-center->x)*(q->x-p->x)+(p->y-center->y)*(q->y-p->y))/length,0),"Capsule tangent equation failed");
                }
            }
        }
        const auto* a=s.find_point(s.arcs[0].center_point_id);
        const auto* b=s.find_point(s.arcs[1].center_point_id);
        require(near(std::hypot(b->x-a->x,b->y-a->y),length),"Capsule length changed");
        for(const auto& c:s.constraints)if(!c.suppressed&&c.kind==ConstraintKind::PointOnLine) {
            std::array<double,2> origin{0,0},direction{1,0};
            if(c.geometry_id!="sketch_axis:x") {
                const auto line=std::ranges::find(s.segments,c.geometry_id,&SketchSegment::id);
                if(line!=s.segments.end()) {
                    const auto* p=s.find_point(line->first_point_id);const auto* q=s.find_point(line->second_point_id);
                    origin={p->x,p->y};direction={q->x-p->x,q->y-p->y};
                } else {
                    const auto ref=std::ranges::find(s.external_references,c.geometry_id,&SketchExternalReference::id);
                    require(ref!=s.external_references.end(),"Capsule support identity lost");
                    const auto support=external_reference_line(*ref);require(support.has_value(),"Capsule support is no longer a line");
                    origin=support->first;direction=support->second;
                }
            }
            const auto* p=s.find_point(c.first_point_id);
            require(near(((p->x-origin[0])*direction[1]-(p->y-origin[1])*direction[0])/std::hypot(direction[0],direction[1]),0),"Capsule centre support equation failed");
        }
    };
    auto user=original;
    for(const auto [width,length]:std::vector<std::pair<double,double>>{{60,120},{30,80},{90,150},{57.946189880354524,100}}) {
        require(user.set_dimension_value(user.dimensions[0].id,width),"Original capsule width edit failed");
        check_geometry(user,width,user.dimensions[1].value);
        require(user.set_dimension_value(user.dimensions[1].id,length),"Original capsule centre-distance edit failed");
        check_geometry(user,width,length);
        require(near(user.find_point(user.arcs[0].center_point_id)->y,0)&&near(user.find_point(user.arcs[1].center_point_id)->y,0),"Original capsule left native X axis");
        user=Sketch::from_serialized(user.serialized());
    }
    for(const auto& root:original.points) {
        auto unlocked=original;
        for(double delta:{7.,-3.}) {
            const auto point=*unlocked.find_point(root.id);
            require(unlocked.move_point(root.id,point.x+delta,point.y),"Unlocked capsule drag failed");
            require(std::abs(unlocked.find_point(root.id)->x-point.x)>1,"Unlocked capsule did not move");
            check_geometry(unlocked,unlocked.dimensions[0].value,std::abs(unlocked.dimensions[1].value));
        }
    }
    std::size_t cases=0;
    for(double angle:{0.,std::acos(-1.)/2,.37,std::acos(-1.)})
    for(bool reverse:{false,true})for(bool opposite_normal:{false,true})for(bool locked:{false,true})
    for(bool anchored:{false,true})for(bool external:{false,true}) {
        auto s=original;
        const double co=std::cos(angle),si=std::sin(angle);
        for(auto& p:s.points){const double x=p.x,y=p.y;p.x=co*x-si*y;p.y=si*x+co*y;}
        for(auto& a:s.arcs){a.start_angle+=angle;a.end_angle+=angle;}
        SketchExternalReference axis;axis.id="capsule-support";axis.kind=ExternalReferenceKind::Axis;
        axis.source_document_id="read-only-source";axis.source_owner_id="origin";axis.source_semantic_key="axis:x";
        axis.cached_points={{{0,0}},{{co,si}}};axis.infinite=true;
        std::string support;
        if(external){s.external_references.push_back(axis);support=axis.id;}
        else {
            support=s.add_segment(-1000*co,-1000*si,1000*co,1000*si,1e-6,true);
            const auto& line=s.segments.back();s.find_point(line.first_point_id)->fixed=true;s.find_point(line.second_point_id)->fixed=true;
        }
        for(auto& c:s.constraints)if(c.kind==ConstraintKind::PointOnLine)c.geometry_id=support;
        s.dimensions[1].kind=DimensionKind::Distance;
        if(anchored)s.find_point(s.arcs[0].center_point_id)->fixed=true;
        if(opposite_normal)std::swap(s.segments[reverse?1:0].first_point_id,s.segments[reverse?1:0].second_point_id);
        auto d=s.create_line_pair_dimension(s.segments[reverse?1:0].id,s.segments[reverse?0:1].id,DimensionKind::DistanceLine);
        d.id=s.dimensions[0].id;d.locked=locked;s.dimensions[0]=d;
        const auto external_before=s.external_references;
        for(double width:{60.,30.,90.,57.946189880354524}) {
            if(!s.set_dimension_value(d.id,width)) {
                std::cerr<<"angle="<<angle<<" reverse="<<reverse<<" locked="<<locked<<" anchored="<<anchored<<" external="<<external<<" width="<<width<<'\n';
                throw std::runtime_error("Capsule width edit failed");
            }
            check_geometry(s,width);
            require(s.dimensions[0].solution_side==d.solution_side,"Capsule dimension changed contact side");
            require(s.external_references==external_before,"Capsule altered read-only support");
            for(std::size_t i=0;i<original.points.size();++i)require(s.points[i].id==original.points[i].id,"Capsule point identity changed");
            s=Sketch::from_serialized(s.serialized());
        }
        // Locked drivers retain equations during drag; an unanchored capsule
        // still has its free translation along the support.
        for(auto& value:s.dimensions)value.locked=true;
        for(const auto& source_point:original.points) {
            const auto point_id=source_point.id;
            const auto old=*s.find_point(point_id);
            const auto before=s.serialized();
            const bool moved=s.move_point(point_id,old.x+co*7,old.y+si*7);
            if(anchored) {
                // The existing drag API may report a successful projection back
                // onto a fully constrained solution. Check every coordinate and
                // equation, rather than interpreting that boolean as movement.
                const auto previous=Sketch::from_serialized(before);
                for(const auto& p:previous.points)require(near(s.find_point(p.id)->x,p.x)&&near(s.find_point(p.id)->y,p.y),"Fully constrained capsule escaped on drag");
                check_geometry(s,s.dimensions[0].value);
            } else {
                require(moved&&std::hypot(s.find_point(point_id)->x-old.x,s.find_point(point_id)->y-old.y)>1,"Free capsule did not move");
                check_geometry(s,s.dimensions[0].value);
                const auto next=*s.find_point(point_id);
                require(s.move_point(point_id,next.x-co*3,next.y-si*3),"Successive capsule drag failed");
                check_geometry(s,s.dimensions[0].value);
            }
            require(s.external_references==external_before,"Capsule drag altered read-only support");
        }
        auto impossible=s;for(auto& p:impossible.points)p.fixed=true;
        const auto frozen=impossible.serialized();
        require(!impossible.set_dimension_value(d.id,65)&&impossible.serialized()==frozen,"Impossible capsule edit changed document");
        const auto before_zero=s.serialized();
        require(!s.set_dimension_value(d.id,0)&&s.serialized()==before_zero,"Degenerate zero-width capsule changed document");
        ++cases;
    }
    for(auto kind:{DimensionKind::Distance,DimensionKind::DistanceY,DimensionKind::Radius,DimensionKind::Diameter}) {
        auto s=original;auto d=s.dimensions[0];
        if(kind==DimensionKind::Radius)d=s.create_arc_radius_dimension(s.arcs[0].id);
        else if(kind==DimensionKind::Diameter)d=s.create_arc_diameter_dimension(s.arcs[0].id);
        else d=s.create_point_dimension(s.arcs[0].end_point_id,s.arcs[0].start_point_id,kind);
        s.dimensions[0]=d;
        for(double width:{60.,40.,80.}) {
            require(s.set_dimension_value(d.id,kind==DimensionKind::Radius?width/2:width),"Alternate capsule driver edit failed");
            check_geometry(s,width);
        }
    }
    auto redundant=original;const auto redundant_before=redundant.serialized();bool rejected=false;
    try {redundant.apply_dimension(redundant.create_line_pair_dimension(redundant.segments[0].id,redundant.segments[1].id,DimensionKind::DistanceLine));}catch(const std::exception&){rejected=true;}
    require(rejected&&redundant.serialized()==redundant_before,"Redundant capsule driver changed document");
    auto zero_lines=Sketch::create_default();const auto first=zero_lines.add_segment(-10,0,10,0),second=zero_lines.add_segment(-10,10,10,10);
    auto zero_dimension=zero_lines.create_line_pair_dimension(first,second,DimensionKind::DistanceLine);zero_lines.apply_dimension(zero_dimension);
    require(zero_lines.set_dimension_value(zero_dimension.id,0),"Ordinary zero line distance was rejected");
    auto reference=original;auto measured=reference.dimensions[0];measured.driving=false;reference.apply_dimension(measured);
    const auto before=reference.serialized();require(reference.set_dimension_value(measured.id,999)&&reference.serialized()==before,"Reference width drove capsule");
    auto trimmed=original;require(trimmed.set_dimension_value(trimmed.dimensions[0].id,60),"Pre-trim width edit failed");
    const auto pieces=sketch_trim_topology(trimmed,false);
    const auto piece=std::ranges::find(pieces,trimmed.segments[1].id,&SketchTrimPiece::geometry_id);
    require(piece!=pieces.end(),"Resized capsule top line missing from trim topology");
    static_cast<void>(apply_sketch_trim(trimmed,{*piece}));
    require(trimmed.arcs.size()==2&&trimmed.arcs[0].id==original.arcs[0].id&&trimmed.arcs[1].id==original.arcs[1].id,"Trim replaced circular reference identities");
    const auto trim_solve=trimmed.solve();require(trim_solve.status!=SolveStatus::Invalid&&trim_solve.status!=SolveStatus::Conflicting&&trim_solve.maximum_residual<1e-7,"Resized capsule trim left conflicting equations");
    std::cout<<"Capsule matrix: "<<cases<<" orientation/order/lock/anchor/support cases, successive edits/drags, alternate drivers and reference policy passed\n";
}
