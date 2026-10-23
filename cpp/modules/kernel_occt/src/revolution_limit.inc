// Exact angular contacts are operation-local and run only during calculation.
struct RevolutionContacts {
    TopoDS_Face face;
    gp_Ax1 axis;
    double tolerance;
    std::optional<detail::OrderedLimitResult> ordered;
    const OrderedUpToLimit* ordered_definition{};
    std::vector<TopoDS_Face> uncorrected_patches;
    double minimum_span{};
    std::pair<double,double> angles(const gp_Pnt& point) const {
        const gp_Vec n(axis.Direction()),delta(axis.Location(),point);
        const gp_Pnt center=axis.Location().Translated(n*delta.Dot(n));
        const gp_Vec radial(center,point);
        if(radial.Magnitude()<=tolerance)
            throw std::runtime_error("Revolution profile misses target surface.");
        Handle(Geom_Circle) circle=new Geom_Circle(gp_Ax2(center,axis.Direction(),gp_Dir(radial)),radial.Magnitude());
        std::vector<double> hits;
        const auto intersect=[&](const TopoDS_Face& target) {
        GeomAPI_IntCS intersection(circle,BRep_Tool::Surface(target));
        if(!intersection.IsDone()||intersection.NbSegments()!=0)
            throw std::runtime_error("Revolution target does not define an unambiguous end.");
        for(int i=1;i<=intersection.NbPoints();++i) {
            double u{},v{},angle{};intersection.Parameters(i,u,v,angle);
            BRepClass_FaceClassifier classified(target,gp_Pnt2d(u,v),tolerance);
            if(classified.State()!=TopAbs_IN&&classified.State()!=TopAbs_ON)continue;
            angle=std::fmod(angle,2*std::numbers::pi);if(angle<0)angle+=2*std::numbers::pi;
            if(angle*radial.Magnitude()<=tolerance)
                throw std::runtime_error("Revolution target does not define an unambiguous end.");
            GeomLProp_SLProps normal(BRep_Tool::Surface(target),u,v,1,Precision::Confusion());
            const gp_Vec tangent=n.Crossed(gp_Vec(center,intersection.Point(i)));
            if(!normal.IsNormalDefined()||std::abs(tangent.Normalized().Dot(gp_Vec(normal.Normal())))<1e-8)
                throw std::runtime_error("Revolution target does not define an unambiguous end.");
            hits.push_back(angle);
        }
        };
        if(!uncorrected_patches.empty())for(const auto& patch:uncorrected_patches)intersect(patch);
        else if(ordered)for(const auto& patch:ordered->patches)intersect(patch.face);
        else intersect(face);
        std::sort(hits.begin(),hits.end());
        hits.erase(std::unique(hits.begin(),hits.end(),[](double a,double b){return std::abs(a-b)<1e-9;}),hits.end());
        if(hits.empty())throw std::runtime_error("Revolution profile misses target surface.");
        const double correction=ordered_definition?ordered_definition->path_correction*std::numbers::pi/180:0.;
        // Follow the original first contact through its signed correction. Choosing
        // a normalized hit after rotating the support can silently select another
        // turn when that contact crosses the beginning or the end of the sweep.
        const double first=hits.front()+correction;
        if(!std::isfinite(first)||first*radial.Magnitude()<=tolerance||first>=2*std::numbers::pi)
            throw std::runtime_error("Revolution target does not define an unambiguous end.");
        return {first,hits.size()>1?std::min(hits[1]+correction,2*std::numbers::pi):2*std::numbers::pi};
    }
};

RevolutionContacts revolution_contacts(const RevolutionRequest& request,
        const std::optional<TopoDS_Face>& exact,double tolerance,
        const std::vector<std::optional<TopoDS_Face>>& exact_ordered,const TopoDS_Shape& profile) {
    const gp_Ax1 axis(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),
        gp_Dir(request.axis_direction.x,request.axis_direction.y,request.axis_direction.z));
    if(request.ordered_end_limit) {
        if(request.end_limit)throw detail::OrderedLimitError(detail::OrderedLimitFailure::InvalidInput,0,0);
        auto joined=prepare_ordered_up_to(*request.ordered_end_limit,exact_ordered,profile,tolerance,axis);
        if(request.mirror_end_limit) {
            gp_Trsf mirror;mirror.SetMirror(gp_Ax2(axis.Location(),
                gp_Dir(request.profile_normal.x,request.profile_normal.y,request.profile_normal.z)));
            joined=detail::transform_ordered_up_to(joined,mirror);
        }
        std::vector<TopoDS_Face> uncorrected;
        if(request.ordered_end_limit->path_correction!=0) {
            for(const auto& patch:joined.patches)uncorrected.push_back(patch.face);
            gp_Trsf advance;advance.SetRotation(axis,request.ordered_end_limit->path_correction*std::numbers::pi/180);
            joined=detail::transform_ordered_up_to(joined,advance);
        }
        return {{},axis,std::max(Precision::Confusion(),tolerance),std::move(joined),&*request.ordered_end_limit,std::move(uncorrected)};
    }
    const auto& limit=*request.end_limit;
    TopoDS_Face face;
    if(limit.planar) {
        const gp_Pln plane(gp_Pnt(limit.origin.x,limit.origin.y,limit.origin.z),gp_Dir(limit.normal.x,limit.normal.y,limit.normal.z));
        face=BRepBuilderAPI_MakeFace(plane,-5'000'000.,5'000'000.,-5'000'000.,5'000'000.).Face();
    } else {
        if(!exact)throw std::runtime_error("Exact Revolution target surface is missing or ambiguous.");
        face=*exact;
    }
    if(request.mirror_end_limit) {
        gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),
            gp_Dir(request.profile_normal.x,request.profile_normal.y,request.profile_normal.z)));
        face=TopoDS::Face(BRepBuilderAPI_Transform(face,mirror,true).Shape());
    }
    return {face,gp_Ax1(gp_Pnt(request.axis_point.x,request.axis_point.y,request.axis_point.z),
        gp_Dir(request.axis_direction.x,request.axis_direction.y,request.axis_direction.z)),std::max(Precision::Confusion(),tolerance)};
}

double revolution_limit_span(const std::vector<TopoDS_Wire>& wires,RevolutionContacts& contact,double* actual_span) {
    double minimum=2*std::numbers::pi,maximum{},next=2*std::numbers::pi;
    for(const auto& wire:wires)for(TopExp_Explorer it(wire,TopAbs_EDGE);it.More();it.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
        for(int sample=0;sample<=32;++sample) {
            const auto [first,second]=contact.angles(curve.Value(curve.FirstParameter()+
                (curve.LastParameter()-curve.FirstParameter())*sample/32.));
            maximum=std::max(maximum,first);next=std::min(next,second);
            minimum=std::min(minimum,first);
        }
    }
    // Do not sweep into a later crossing. The overrun is removed by exact Common.
    const double margin=std::min(.02,(next-maximum)*.25);
    if(!std::isfinite(maximum)||maximum<=0||margin<=1e-7||maximum+margin>=2*std::numbers::pi)
        throw std::runtime_error("Revolution target does not define an unambiguous end.");
    if(actual_span)*actual_span=maximum*180/std::numbers::pi;
    contact.minimum_span=minimum;
    return (maximum+margin)*180/std::numbers::pi;
}

TopoDS_Shape revolution_terminal_window(const TopoDS_Shape& shape,const RevolutionRequest& request,
        const RevolutionContacts& contact,const gp_Pnt& start_point) {
    const double first=contact.minimum_span-std::min(.02,contact.minimum_span*.5);
    const double overrun=request.angle_degrees*std::numbers::pi/180;
    const double last=overrun+std::min(.02,(2*std::numbers::pi-overrun)*.5);
    const double half=(last-first)*.5;
    if(!std::isfinite(half)||half<=0||half>=std::numbers::pi*.5-1e-7)
        throw detail::OrderedLimitError(detail::OrderedLimitFailure::DisconnectedChain,0,contact.ordered->patches.size()-1);
    const gp_Vec normal(contact.axis.Direction());
    const gp_Vec start_delta(contact.axis.Location(),start_point);
    const gp_Vec radial=(start_delta-normal*start_delta.Dot(normal)).Normalized();
    const gp_Vec tangent=normal.Crossed(radial);
    Bnd_Box bounds;BRepBndLib::Add(shape,bounds);
    double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
    double low=std::numeric_limits<double>::max(),high=-low,radius=0;
    for(double x:{x0,x1})for(double y:{y0,y1})for(double z:{z0,z1}) {
        const gp_Vec delta(contact.axis.Location(),gp_Pnt(x,y,z));const double axial=delta.Dot(normal);
        low=std::min(low,axial);high=std::max(high,axial);
        radius=std::max(radius,(delta-normal*axial).Magnitude());
    }
    const double margin=std::max(1.,contact.tolerance*10);
    const gp_Pnt center=contact.axis.Location().Translated(normal*(low-margin));
    const double reach=(radius+margin)/std::cos(half);
    const auto point=[&](double angle) {return center.Translated((radial*std::cos(angle)+tangent*std::sin(angle))*reach);};
    // The actual initial profile point already includes its authored start
    // rotation, independently of the profile's normal or the axis direction.
    BRepBuilderAPI_MakePolygon polygon;polygon.Add(center);polygon.Add(point(first));polygon.Add(point(last));polygon.Close();
    const auto face=BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    return BRepPrimAPI_MakePrism(face,normal*(high-low+2*margin),true).Shape();
}

void clip_revolution_end(PrimitiveData& result,const RevolutionRequest& request,
        const RevolutionContacts& contact,const TopoDS_Shape& first,const TopoDS_Shape& last,
        const gp_Pnt& keep_point,const std::string& owner) {
    const std::string role=request.first_cap_is_start?"end":"start";
    FaceReference cap{owner,profile_cap_semantic_key(role,request.profile_region_id),{}};cap.surface_result=request.surface_result;
    if(contact.ordered) {
        const auto window=request.angle_degrees>180?
            revolution_terminal_window(result.shape,request,contact,keep_point):TopoDS_Shape{};
        auto terminal_keep=keep_point;
        if(!window.IsNull()) {
            gp_Trsf turn;turn.SetRotation(contact.axis,contact.minimum_span-std::min(.01,contact.minimum_span*.25));
            terminal_keep.Transform(turn);
        }
        clip_ordered_up_to_profile(result,*contact.ordered,*contact.ordered_definition,first,last,
            terminal_keep,cap,role,request.surface_result,contact.tolerance,window);return;
    }
    BRepPrimAPI_MakeHalfSpace half_space(contact.face,keep_point);
    BRepAlgoAPI_Common clip;set_boolean_inputs(clip,result.shape,half_space.Solid());
    clip.SetFuzzyValue(contact.tolerance);clip.SetToFillHistory(true);clip.Build();
    if(!clip.IsDone()||clip.Shape().IsNull()||!BRepCheck_Analyzer(clip.Shape(),true,false,true).IsValid())
        throw std::runtime_error("Revolution target does not define an unambiguous end.");
    TopTools_IndexedMapOfShape actual;TopExp::MapShapes(clip.Shape(),actual);
    // Every region must retain its complete start and lose its entire overrun.
    // Otherwise Common has kept a later orbit section or an incomplete end.
    for(TopExp_Explorer it(first,request.surface_result?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(!actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw std::runtime_error("Revolution target does not define an unambiguous end.");
    if(actual.Contains(last)||!clip.Modified(last).IsEmpty())
        throw std::runtime_error("Revolution profile misses target surface.");
    for(TopExp_Explorer it(last,request.surface_result?TopAbs_EDGE:TopAbs_FACE);it.More();it.Next())
        if(actual.Contains(it.Current())||!clip.Modified(it.Current()).IsEmpty())
            throw std::runtime_error("Revolution profile misses target surface.");
    locate_clipped_profile_end(clip,result,owner,role);
    result.faces=propagate_topology(clip,result.faces,std::vector<OwnedFace>{{contact.face,cap}});
    std::erase_if(result.faces,[&](const auto& item){return !actual.Contains(item.shape);});
    result.edges=propagate_topology(clip,result.edges,{});
    result.vertices=propagate_topology(clip,result.vertices,{});
    std::erase_if(result.edges,[&](const auto& item){return !actual.Contains(item.shape);});
    std::erase_if(result.vertices,[&](const auto& item){return !actual.Contains(item.shape);});
    // Define replacement rims/points from the existing profile ancestry, then
    // locate them by adjacency in the calculated result (never by enumeration).
    for(const auto& face:result.faces)if(face.reference==cap) {
        for(TopExp_Explorer rim(face.shape,TopAbs_EDGE);rim.More();rim.Next()) {
            for(const auto& side:result.faces)if(side.reference.semantic_key.starts_with("generated:")) {
                bool adjacent=false;
                for(TopExp_Explorer e(side.shape,TopAbs_EDGE);e.More();e.Next())adjacent=adjacent||e.Current().IsSame(rim.Current());
                if(adjacent)result.edges.push_back({rim.Current(),{owner,role+":"+side.reference.semantic_key.substr(10),{}}});
            }
        }
    }
    for(const auto& edge:result.edges)if(edge.reference.semantic_key.starts_with("generated:"))
        for(TopExp_Explorer vertex(edge.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
            bool on_end=false;
            for(const auto& face:result.faces)if(face.reference==cap)
                for(TopExp_Explorer v(face.shape,TopAbs_VERTEX);v.More();v.Next())on_end=on_end||v.Current().IsSame(vertex.Current());
            if(on_end)result.vertices.push_back({vertex.Current(),{owner,role+":"+edge.reference.semantic_key.substr(10),{}}});
        }
    result.shape=clip.Shape();
}
