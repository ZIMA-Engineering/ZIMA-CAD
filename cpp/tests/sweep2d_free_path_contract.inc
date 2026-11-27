// Free planar paths retain native endpoint identity independently of the Origin.
static std::string sweep2d_geometry_fingerprint(const document::HistoryContainer& c) {
    const std::vector<kernel::HistoryOperation> operations{{c.id,document::PartDocument::sweep2d_request(c)}};
    return kernel::history_fingerprint(operations,operations.size());
}
static void verify_free_planar_paths(kernel::OcctKernel& engine) {
    using namespace document::helical_geometry;
    for(const unsigned count:{1U,2U})for(const bool reverse:{false,true})
    for(const bool origin_at_end:{false,true})for(const auto plane:{sketcher::SketchPlane::XY,
            sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ}) {
        auto c=document::PartDocument::create_sweep2d_container();
        auto guide=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);guide.plane=plane;
        const double x=origin_at_end?-12.*count:7.,y=origin_at_end?-16.*count:11.;
        for(unsigned k=0;k<count;++k) {
            const unsigned i=reverse?count-1-k:k;
            const P a{x+12*i,y+16*i},b{x+12*(i+1),y+16*(i+1)};
            if(reverse)static_cast<void>(guide.add_segment(b[0],b[1],a[0],a[1]));
            else static_cast<void>(guide.add_segment(a[0],a[1],b[0],b[1]));
        }
        c.sweep2d.path_sketch=guide.serialized();document::PartDocument::reframe_sweep2d_sketches(c);
        auto route=document::PartDocument::sweep2d_route(c);
        require(route.segments.size()==count&&route.stations.size()==2*count,"Offset straight path lost stations");
        require(route.stations.front().point_id==guide.segments.front().first_point_id,
            "A new path did not start at its first authored endpoint");
        const auto start=route.stations.front().point_id,end=route.stations.back().point_id;
        profile_at(c,route.stations.front(),2);
        auto native=document::PartDocument::create_default();native.history={c};
        document::DocumentSession session(native);
        for(const P shift:{P{3,-5},P{-13,9},P{10,-4}}) {
            guide=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
            for(auto& point:guide.points){point.x+=shift[0];point.y+=shift[1];}
            c.sweep2d.path_sketch=guide.serialized();document::PartDocument::reframe_sweep2d_sketches(c);
            route=document::PartDocument::sweep2d_route(c);
            require(route.stations.front().point_id==start&&route.stations.back().point_id==end,
                "Translating path changed the profile direction or endpoint identity");
            const auto current=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
            for(const auto& station:route.stations) {
                const auto* point=current.find_point(station.point_id);require(point,"Station lost native point");
                require(norm(sub(station.origin,current.world_point(point->x,point->y)))<1e-8,
                    "Station is not on its exact authored endpoint");
            }
            native.history={c};const auto calculated=engine.evaluate_history(native.kernel_operations());
            require(calculated.back().calculation_errors.empty(),"Offset straight Sweep failed");
            require(std::abs(calculated.back().volume-count*80*std::numbers::pi)<1e-6,
                "Offset straight Sweep volume is incorrect");
            const auto before=session.document().history;
            session.commit(native,calculated);session.undo();
            require(session.document().history==before,"Offset path Undo changed its definition");session.redo();
            require(session.document().history==native.history,"Offset path Redo lost profile identity");
        }
        const auto forward=c;
        const auto original_request=sweep2d_geometry_fingerprint(c);
        for(unsigned flip=0;flip<3;++flip) {
            document::PartDocument::reverse_sweep2d_path(c);
            require(sweep2d_geometry_fingerprint(c)==original_request,
                "Reversal changed the exact calculation request or reference identities");
            route=document::PartDocument::sweep2d_route(c);
            require(route.stations.front().point_id==(c.sweep2d.path_reversed?end:start),"Reverse selected wrong endpoint");
            require(c.sweep2d.profiles.front().id==forward.sweep2d.profiles.front().id&&
                c.sweep2d.profiles.front().point_id==forward.sweep2d.profiles.front().point_id,
                "Reverse moved or replaced the authored profile");
            native.history={c};const auto calculated=engine.evaluate_history(native.kernel_operations());
            require(calculated.back().calculation_errors.empty()&&
                std::abs(calculated.back().volume-count*80*std::numbers::pi)<1e-6,
                "Reverse lost the inherited endpoint profile or changed volume");
            session.commit(native,calculated);session.undo();session.redo();
            require(session.document().history==native.history,"Reverse Undo/Redo lost path direction");
        }
        const auto file=std::filesystem::temp_directory_path()/("zima-offset-sweep-"+c.id+".prtz");
        native.save(file);const auto reopened=document::PartDocument::load(file);std::filesystem::remove(file);
        const auto restored=document::PartDocument::sweep2d_route(reopened.history.front());
        require(reopened.history.front().sweep2d.path_reversed&&
            restored.stations.front().point_id==end&&restored.stations.back().point_id==start,
            "Offset path save/reopen changed its direction");
        require(std::abs(engine.evaluate_history(reopened.kernel_operations()).back().volume-count*80*std::numbers::pi)<1e-6,
            "Offset path cold regeneration changed volume");
        // The helical guide's separate Origin restriction remains in force.
        bool rejected=false;try{static_cast<void>(guide_curves(guide,P{0,0}));}catch(...){rejected=true;}
        if(!origin_at_end)require(rejected,"Planar path fix relaxed the helical Origin contract");
    }
    for(const bool curved:{false,true})for(const bool open:{false,true})
    for(const auto type:{document::ProfileResultType::Solid,document::ProfileResultType::Thin,document::ProfileResultType::Surface})
    for(const auto mode:{document::ThinMode::OneSide,document::ThinMode::OtherSide,document::ThinMode::Symmetric}) {
        if((open&&type==document::ProfileResultType::Solid)||(type!=document::ProfileResultType::Thin&&mode!=document::ThinMode::OneSide))continue;
        auto c=fixture(curved);c.sweep2d.result_type=type;c.sweep2d.thickness=.2;
        c.sweep2d.thin_mode=mode;
        auto profile=sketcher::Sketch::from_serialized(c.sweep2d.profiles.front().sketch_serialized);
        profile.circles.clear();profile.points.clear();
        if(open)static_cast<void>(profile.add_segment(1,2,3,5));
        else static_cast<void>(profile.add_rectangle(1,2,3,5));
        c.sweep2d.profiles.front().sketch_serialized=profile.serialized();document::PartDocument::reframe_sweep2d_sketches(c);
        const auto calculate=[&]{return engine.evaluate_history({{c.id,document::PartDocument::sweep2d_request(c)}}).back();};
        const auto original=calculate();
        const auto original_request=sweep2d_geometry_fingerprint(c);
        const auto bounds=[](const kernel::BodyResult& body) {
            TopoDS_Shape shape;BRep_Builder builder;std::istringstream serialized(body.kernel_shape);
            BRepTools::Read(shape,serialized,builder);require(!shape.IsNull(),"Missing exact asymmetric Sweep result");
            Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);
            std::array<double,6> b;box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
            return b;
        };
        const auto old=bounds(original);
        for(unsigned flip=0;flip<4;++flip) {
            const auto prior=sketcher::Sketch::from_serialized(c.sweep2d.profiles.front().sketch_serialized);
            document::PartDocument::reverse_sweep2d_path(c);
            require(sweep2d_geometry_fingerprint(c)==original_request,
                "Reversal changed asymmetric section topology or transport");
            const auto current=sketcher::Sketch::from_serialized(c.sweep2d.profiles.front().sketch_serialized);
            for(const auto& p:prior.points)require(norm(sub(prior.world_point(p.x,p.y),current.world_point(p.x,p.y)))<1e-7,
                "Reverse mirrored an asymmetric profile");
            const auto body=calculate();require(body.calculation_errors.empty(),"Reversed asymmetric sweep failed");
            for(const auto& endpoint:original.mesh.points) {
                if(!endpoint.reference.semantic_key.starts_with("sweep:path-point:"))continue;
                const auto found=std::ranges::find(body.mesh.points,endpoint.reference,&kernel::ViewerPoint::reference);
                require(found!=body.mesh.points.end()&&norm(sub(found->position,endpoint.position))<1e-8,
                    "Reversal exchanged an attachment endpoint");
            }
            require(std::abs(body.volume-original.volume)<1e-5&&std::abs(body.surface_area-original.surface_area)<1e-5,
                "Reversed asymmetric sweep changed physical properties");
            const auto now=bounds(body);for(unsigned j=0;j<6;++j) {
                if(std::abs(now[j]-old[j])>=1e-6)std::cerr<<"Asymmetric bounds: curved="<<curved<<" mode="<<static_cast<int>(type)
                    <<" flip="<<flip<<" coordinate="<<j<<" before="<<old[j]<<" after="<<now[j]<<'\n';
                require(std::abs(now[j]-old[j])<1e-6,"Reversed asymmetric sweep changed its material side");
            }
        }
    }
    for(const bool reverse_before_authoring:{false,true}) {
        auto c=fixture();
        auto path=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
        static_cast<void>(path.add_segment(0,20,0,40));c.sweep2d.path_sketch=path.serialized();
        c.sweep2d.profiles.clear();
        if(reverse_before_authoring)document::PartDocument::reverse_sweep2d_path(c);
        const auto route=document::PartDocument::sweep2d_route(c);
        profile_at(c,route.stations[0],2);profile_at(c,route.stations[1],3);profile_at(c,route.stations[3],4);
        const auto calculate=[&]{return engine.evaluate_history({{c.id,document::PartDocument::sweep2d_request(c)}}).back();};
        const auto before=calculate();const auto definitions=c.sweep2d.profiles.size();
        const auto original_request=sweep2d_geometry_fingerprint(c);
        for(unsigned flip=0;flip<4;++flip) {
            document::PartDocument::reverse_sweep2d_path(c);const auto after=calculate();
            require(sweep2d_geometry_fingerprint(c)==original_request,
                "Reversal exchanged varying-section cap, rim or rail identities");
            require(after.calculation_errors.empty()&&std::abs(before.volume-after.volume)<1e-6&&
                std::abs(before.surface_area-after.surface_area)<1e-6,"Reversal changed inherited varying sections");
            require(c.sweep2d.profiles.size()==definitions,"Reversal inserted an authored profile");
        }
    }
    for(const bool branched:{false,true}) {
        auto c=document::PartDocument::create_sweep2d_container();
        auto path=sketcher::Sketch::from_serialized(c.sweep2d.path_sketch);
        static_cast<void>(path.add_segment(7,11,19,27));
        static_cast<void>(path.add_segment(branched?19:30,27,42,43));
        if(branched)static_cast<void>(path.add_segment(19,27,19,40));
        c.sweep2d.path_sketch=path.serialized();const auto unchanged=c;
        bool rejected=false;try{static_cast<void>(document::PartDocument::sweep2d_route(c));}catch(...){rejected=true;}
        require(rejected&&c==unchanged,"Invalid free path was accepted or mutated");
    }
    std::cout<<"Free planar paths: 24 line/chain/orientation/plane cases, repeated translation/reversal, exact stations/volume, asymmetric solid/thin/surface profiles, Undo/Redo, save/reopen passed\n";
}
