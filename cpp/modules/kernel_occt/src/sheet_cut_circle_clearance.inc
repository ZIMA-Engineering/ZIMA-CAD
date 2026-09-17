// A circular prism normal to a cylindrical sheet's axis has a continuous
// clearance envelope. Its two curved sides come from the extreme radii; at
// each axial circle tangent they are joined by a circumferential arc. Sampling
// intermediate radii cannot recover those tangent arcs efficiently.
// Included after sheet_cut_normal_volume, inside the kernel's anonymous namespace.
PrimitiveData sheet_cut_cylindrical_circle_domain(const TopoDS_Face& source,
        const FaceReference& reference,const std::vector<OwnedEdge>& source_edges,
        const PrimitiveData& full_projection,const PrimitiveData& selected_projection,
        double tolerance) {
    const BRepAdaptor_Surface original(source);
    if(original.GetType()!=GeomAbs_Cylinder||!(reference.sheet_thickness>0))return {};
    const auto cylinder=original.Cylinder();const auto frame=cylinder.Position();
    const gp_Vec axis(frame.Direction());
    std::optional<gp_Cylinder> cutter;
    std::set<std::pair<std::string,std::string>> parents;
    // This specialization is for one complete circular profile. Multiple
    // circles, islands and other curves retain the general domain algorithm.
    for(TopExp_Explorer faces(full_projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());const BRepAdaptor_Surface surface(face);
        if(surface.GetType()==GeomAbs_Plane)continue;
        if(surface.GetType()!=GeomAbs_Cylinder)return {};
        const auto candidate=surface.Cylinder();
        if(std::abs(axis.Dot(gp_Vec(candidate.Axis().Direction())))>1.e-9)return {};
        if(cutter) {
            const gp_Vec direction(cutter->Axis().Direction());
            const gp_Vec separation(cutter->Location(),candidate.Location());
            if(std::abs(cutter->Radius()-candidate.Radius())>tolerance||
               std::abs(std::abs(direction.Dot(gp_Vec(candidate.Axis().Direction())))-1)>1.e-9||
               separation.Crossed(direction).Magnitude()>tolerance)return {};
        } else cutter=candidate;
        for(const auto& owned:full_projection.faces)if(owned.shape.IsSame(face))
            parents.emplace(owned.reference.owner_id,owned.reference.semantic_key);
    }
    if(!cutter||parents.empty())return {};
    const gp_Vec direction(cutter->Axis().Direction());
    double cap_min=std::numeric_limits<double>::infinity(),cap_max=-cap_min;
    for(TopExp_Explorer faces(full_projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const BRepAdaptor_Surface surface(TopoDS::Face(faces.Current()));
        if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();
        if(std::abs(std::abs(direction.Dot(gp_Vec(plane.Axis().Direction())))-1)>1.e-9)return {};
        const double location=gp_Vec(cutter->Location(),plane.Location()).Dot(direction);
        cap_min=std::min(cap_min,location);cap_max=std::max(cap_max,location);
    }
    if(!std::isfinite(cap_min)||!(cap_max-cap_min>tolerance))return {};
    const double outward=(source.Orientation()==TopAbs_REVERSED?-1.:1.)*(frame.Direct()?1.:-1.);
    const double opposite=cylinder.Radius()-outward*reference.sheet_thickness;
    const double rmin=std::min(cylinder.Radius(),opposite),rmax=std::max(cylinder.Radius(),opposite);
    if(!(rmin>tolerance))return {};
    gp_Vec transverse=axis.Crossed(direction);transverse.Normalize();
    const gp_Vec center(frame.Location(),cutter->Location());
    double center_radial=center.Dot(transverse);
    if(center_radial<0) {transverse.Reverse();center_radial=-center_radial;}
    const double center_axial=center.Dot(axis);
    // A sub-tolerance outward allowance dominates the independently checked
    // curve interpolation error, without becoming visible model inflation.
    const double radius=cutter->Radius()+.25*tolerance;
    if(center_radial-radius<=tolerance||center_radial+radius>=rmin-tolerance)return {};
    const double origin_angle=std::atan2(transverse.Dot(gp_Vec(frame.YDirection())),
                                         transverse.Dot(gp_Vec(frame.XDirection())));
    const Handle(Geom_Surface) surface=BRep_Tool::Surface(source);
    double source_u0=0,source_u1=0,source_v0=0,source_v1=0;
    BRepTools::UVBounds(source,source_u0,source_u1,source_v0,source_v1);
    constexpr double pi=std::numbers::pi;
    const double theta_min=std::acos((center_radial+radius)/rmin);
    const double theta_max=std::acos((center_radial-radius)/rmax);
    const double fit_tolerance=std::max(1.e-12,tolerance*.001);
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    std::string ancestry="sheetcut-circle-envelope:source:"+token(reference.owner_id)+":"+
        token(reference.semantic_key)+":from:";
    for(const auto& [owner,key]:parents)ancestry+=token(owner)+":"+token(key)+":";
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
        return std::abs(properties.Mass());
    };
    PrimitiveData result;
    for(const int branch:{-1,1}) {
        const double middle=origin_angle+branch*.5*(theta_min+theta_max);
        const double shift=2*pi*std::round((.5*(source_u0+source_u1)-middle)/(2*pi));
        const double min_u=origin_angle+shift+(branch>0?theta_min:-theta_max);
        const double max_u=origin_angle+shift+(branch>0?theta_max:-theta_min);
        if(max_u<source_u0||min_u>source_u1||center_axial+radius<source_v0||
           center_axial-radius>source_v1)continue;
        // A directional prism cap may exclude a complete angular branch. A
        // cap crossing a branch needs the general algorithm's clipping logic.
        const double side=gp_Vec(frame.XDirection()).Dot(direction)*std::cos(origin_angle+branch*pi/2)+
                          gp_Vec(frame.YDirection()).Dot(direction)*std::sin(origin_angle+branch*pi/2);
        const double axis_location=gp_Vec(cutter->Location(),frame.Location()).Dot(direction);
        double passage_min=axis_location+side*rmin*std::sin(theta_min);
        double passage_max=axis_location+side*rmax*std::sin(theta_max);
        if(passage_min>passage_max)std::swap(passage_min,passage_max);
        if(passage_max<cap_min-tolerance||passage_min>cap_max+tolerance)continue;
        if(passage_min<cap_min-tolerance||passage_max>cap_max+tolerance)return {};

        const auto fit=[&](double skin_radius,double radial_sign)->Handle(Geom2d_BSplineCurve) {
            const auto point=[&](double parameter) {
                const double q=(center_radial+radial_sign*radius*std::sin(parameter))/skin_radius;
                return gp_Pnt2d(origin_angle+shift+branch*std::acos(q),
                                center_axial+radius*std::cos(parameter));
            };
            const auto tangent=[&](double parameter) {
                const double q=(center_radial+radial_sign*radius*std::sin(parameter))/skin_radius;
                return gp_Vec2d(-branch*radial_sign*radius*std::cos(parameter)/
                                (skin_radius*std::sqrt(1-q*q)),-radius*std::sin(parameter));
            };
            for(int intervals=16;intervals<=512;intervals*=2) {
                Handle(NCollection_HArray1<gp_Pnt2d>) points=new NCollection_HArray1<gp_Pnt2d>(1,intervals+1);
                Handle(NCollection_HArray1<double>) parameters=new NCollection_HArray1<double>(1,intervals+1);
                for(int i=0;i<=intervals;++i) {
                    const double parameter=pi*i/intervals;
                    points->SetValue(i+1,point(parameter));parameters->SetValue(i+1,parameter);
                }
                Geom2dAPI_Interpolate interpolate(points,parameters,false,1.e-14);
                interpolate.Load(tangent(0),tangent(pi),false);interpolate.Perform();
                if(!interpolate.IsDone())return {};
                const auto curve=interpolate.Curve();bool accurate=true;
                for(int i=0;i<intervals&&accurate;++i)for(int j=1;j<8;++j) {
                    const double parameter=pi*(i+j/8.)/intervals;
                    const auto expected=point(parameter),actual=curve->Value(parameter);
                    if(surface->Value(expected.X(),expected.Y()).Distance(surface->Value(actual.X(),actual.Y()))>
                       fit_tolerance) {accurate=false;break;}
                }
                if(accurate)return curve;
            }
            return {};
        };
        const auto low=branch>0?fit(rmin,1):fit(rmax,-1);
        const auto high=branch>0?fit(rmax,-1):fit(rmin,1);
        if(low.IsNull()||high.IsNull())return {};
        const std::array<Handle(Geom2d_Curve),4> curves={low,
            GCE2d_MakeSegment(low->Value(pi),high->Value(pi)).Value(),high,
            GCE2d_MakeSegment(high->Value(0),low->Value(0)).Value()};
        const std::array<std::string,4> roles={branch>0?"minimum-radius":"maximum-radius",
            "negative-axial-tangent",branch>0?"maximum-radius":"minimum-radius","positive-axial-tangent"};
        BRepBuilderAPI_MakeWire wire;std::vector<OwnedEdge> generated_edges;
        for(std::size_t i=0;i<curves.size();++i) {
            BRepBuilderAPI_MakeEdge make(curves[i],surface);
            if(!make.IsDone())return {};
            auto edge=make.Edge();if(i==2)edge.Reverse();wire.Add(edge);
            if(!wire.IsDone())return {};
            generated_edges.push_back({wire.Edge(),{reference.owner_id,ancestry+
                (branch>0?"positive-angle:":"negative-angle:")+roles[i]}});
        }
        BRepBuilderAPI_MakeFace make(surface,wire.Wire(),true);
        if(!make.IsDone())return {};
        auto domain=make.Face();domain.Orientation(source.Orientation());
        const double curve_tolerance=std::max(1.e-11,tolerance*.01);
        if(!BRepLib::BuildCurves3d(domain,curve_tolerance))return {};
        BRepLib::SameParameter(domain,curve_tolerance,true);
        if(!BRepCheck_Analyzer(domain).IsValid())return {};
        BRepAlgoAPI_Common limit;set_sheet_cut_boolean_inputs(limit,domain,source);
        limit.SetToFillHistory(true);limit.SetFuzzyValue(tolerance);limit.Build();
        if(!limit.IsDone())return {};
        auto clipped_edges=propagate_topology(limit,generated_edges,source_edges);
        for(TopExp_Explorer faces(limit.Shape(),TopAbs_FACE);faces.More();faces.Next()) {
            auto face=TopoDS::Face(faces.Current());face.Orientation(source.Orientation());
            const auto normal=sheet_cut_normal_volume(face,reference,{},tolerance);
            BRepAlgoAPI_Common reached;set_sheet_cut_boolean_inputs(reached,normal.shape,selected_projection.shape);
            reached.SetFuzzyValue(tolerance);reached.Build();
            if(!reached.IsDone())return {};
            if(volume(reached.Shape())<=std::max(1.e-12,tolerance*tolerance*tolerance))continue;
            TopTools_IndexedMapOfShape actual_edges;TopExp::MapShapes(face,TopAbs_EDGE,actual_edges);
            std::vector<OwnedEdge> face_edges;
            for(const auto& edge:clipped_edges)if(actual_edges.Contains(edge.shape))face_edges.push_back(edge);
            if(result.shape.IsNull()) {result.shape=face;result.edges=std::move(face_edges);continue;}
            BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,result.shape,face);
            unite.SetToFillHistory(true);unite.SetFuzzyValue(tolerance);unite.Build();
            if(!unite.IsDone())return {};
            result.edges=propagate_topology(unite,result.edges,face_edges);result.shape=unite.Shape();
        }
    }
    return result;
}
