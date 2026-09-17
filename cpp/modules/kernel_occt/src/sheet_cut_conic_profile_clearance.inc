// Exact normal-fibre envelope of one circular or elliptical prism on a cone.
// At each angle the profile's unit disk maps affinely to axial position and normal
// depth. Its axial extrema inside the thickness strip are either disk tangents
// or intersections with one of the two skins. No depth sampling is necessary.
PrimitiveData sheet_cut_conical_profile_domain(const TopoDS_Face& source,
        const FaceReference& reference,const std::vector<OwnedEdge>& source_edges,
        const PrimitiveData& full_projection,const PrimitiveData& selected_projection,
        double tolerance) {
    const BRepAdaptor_Surface original(source);
    if(original.GetType()!=GeomAbs_Cone||!(reference.sheet_thickness>0))return {};
    const auto cone=original.Cone();const auto frame=cone.Position();
    const gp_Vec axis(frame.Direction());
    std::optional<gp_Ax2> cutter;double major=0,minor=0;
    std::set<std::pair<std::string,std::string>> parents;
    // Read the authored conic cap edge. A single complete closed conic, with
    // one wire per cap, excludes islands and mixed or piecewise profiles.
    for(TopExp_Explorer faces(full_projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());const BRepAdaptor_Surface support(face);
        if(support.GetType()!=GeomAbs_Plane)continue;
        int wire_count=0,edge_count=0;
        for(TopExp_Explorer wires(face,TopAbs_WIRE);wires.More();wires.Next())++wire_count;
        if(wire_count!=1)return {};
        for(TopExp_Explorer edges(face,TopAbs_EDGE);edges.More();edges.Next()) {
            ++edge_count;const auto edge=TopoDS::Edge(edges.Current());const BRepAdaptor_Curve curve(edge);
            gp_Ax2 position;double candidate_major=0,candidate_minor=0;
            if(curve.GetType()==GeomAbs_Circle) {
                const auto value=curve.Circle();position=value.Position();
                candidate_major=candidate_minor=value.Radius();
            } else if(curve.GetType()==GeomAbs_Ellipse) {
                const auto value=curve.Ellipse();position=value.Position();
                candidate_major=value.MajorRadius();candidate_minor=value.MinorRadius();
            } else return {};
            if(std::abs(curve.LastParameter()-curve.FirstParameter()-2*std::numbers::pi)>1.e-8||
               std::abs(axis.Dot(gp_Vec(position.Direction())))>1.e-9)return {};
            if(cutter) {
                const gp_Vec direction(cutter->Direction());
                if(std::abs(major-candidate_major)>tolerance||std::abs(minor-candidate_minor)>tolerance||
                   std::abs(std::abs(direction.Dot(gp_Vec(position.Direction())))-1)>1.e-9||
                   gp_Vec(cutter->Location(),position.Location()).Crossed(direction).Magnitude()>tolerance||
                   (major-minor>tolerance&&std::abs(std::abs(gp_Vec(cutter->XDirection()).Dot(
                       gp_Vec(position.XDirection())))-1)>1.e-9))return {};
            } else {cutter=position;major=candidate_major;minor=candidate_minor;}
            for(const auto& owned:full_projection.edges)if(owned.shape.IsSame(edge))
                parents.emplace(owned.reference.owner_id,owned.reference.semantic_key);
        }
        if(edge_count!=1)return {};
    }
    if(!cutter||parents.empty())return {};
    const gp_Vec direction(cutter->Direction());
    double cap_min=std::numeric_limits<double>::infinity(),cap_max=-cap_min;
    for(TopExp_Explorer faces(full_projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const BRepAdaptor_Surface support(TopoDS::Face(faces.Current()));
        if(support.GetType()==GeomAbs_Cylinder) {
            const auto value=support.Cylinder();
            if(std::abs(major-minor)>tolerance||std::abs(value.Radius()-major)>tolerance||
               std::abs(std::abs(direction.Dot(gp_Vec(value.Axis().Direction())))-1)>1.e-9||
               gp_Vec(cutter->Location(),value.Location()).Crossed(direction).Magnitude()>tolerance)return {};
            continue;
        }
        if(support.GetType()==GeomAbs_SurfaceOfExtrusion) {
            const auto curve=support.BasisCurve();
            if(std::abs(std::abs(direction.Dot(gp_Vec(support.Direction())))-1)>1.e-9||
               (curve->GetType()!=GeomAbs_Circle&&curve->GetType()!=GeomAbs_Ellipse))return {};
            continue;
        }
        if(support.GetType()!=GeomAbs_Plane)return {};
        const auto plane=support.Plane();
        if(std::abs(std::abs(direction.Dot(gp_Vec(plane.Axis().Direction())))-1)>1.e-9)return {};
        const double value=gp_Vec(cutter->Location(),plane.Location()).Dot(direction);
        cap_min=std::min(cap_min,value);cap_max=std::max(cap_max,value);
    }
    if(!std::isfinite(cap_min)||!(cap_max-cap_min>tolerance))return {};
    const double slope=std::tan(cone.SemiAngle()),h=std::hypot(1.,slope),h2=h*h;
    const double normal_sign=(source.Orientation()==TopAbs_REVERSED?-1.:1.)*(frame.Direct()?1.:-1.);
    const double thickness=reference.sheet_thickness,base_radius=cone.RefRadius();
    if(std::abs(slope)<1.e-8)return {};
    gp_Vec transverse=axis.Crossed(direction);transverse.Normalize();
    const gp_Vec center(frame.Location(),cutter->Location());
    double cy=center.Dot(transverse);if(cy<0){transverse.Reverse();cy=-cy;}
    const double cx=center.Dot(axis);
    const gp_Vec profile_a=gp_Vec(cutter->XDirection())*(major+.25*tolerance);
    const gp_Vec profile_b=gp_Vec(cutter->YDirection())*(minor+.25*tolerance);
    const double ax=profile_a.Dot(axis),ay=profile_a.Dot(transverse);
    const double bx=profile_b.Dot(axis),by=profile_b.Dot(transverse);
    if(cy-std::hypot(ay,by)<=tolerance)return {};
    const double origin_angle=std::atan2(transverse.Dot(gp_Vec(frame.YDirection())),
                                         transverse.Dot(gp_Vec(frame.XDirection())));
    const Handle(Geom_Surface) surface=BRep_Tool::Surface(source);
    double u0=0,u1=0,v0=0,v1=0;BRepTools::UVBounds(source,u0,u1,v0,v1);
    const double source_radius0=base_radius+v0*std::sin(cone.SemiAngle());
    const double source_radius1=base_radius+v1*std::sin(cone.SemiAngle());
    if(std::min(source_radius0,source_radius1)-thickness/h<=tolerance)return {};
    const double bound_radius=std::max(source_radius0,source_radius1)+std::max(0.,-normal_sign*thickness/h);
    constexpr double pi=std::numbers::pi;
    struct Interval { double low=0,high=0;std::string low_role,high_role;bool valid=false; };
    const auto extrema=[&](double angle,bool endpoint=false) {
        const double c=std::cos(angle);
        const double lx=(ax+slope*ay/c)/h2,ly=(bx+slope*by/c)/h2;
        const double mx=normal_sign*(slope*ax-ay/c)/h,my=normal_sign*(slope*bx-by/c)/h;
        const double xcenter=(cx+slope*(cy/c-base_radius))/h2;
        const double wcenter=normal_sign*(slope*cx-cy/c+base_radius)/h;
        const double lnorm=std::hypot(lx,ly),mnorm=std::hypot(mx,my);
        Interval interval;interval.low=std::numeric_limits<double>::infinity();interval.high=-interval.low;
        const auto add=[&](double x,const std::string& role) {
            if(x<interval.low){interval.low=x;interval.low_role=role;}
            if(x>interval.high){interval.high=x;interval.high_role=role;}
            interval.valid=true;
        };
        // At an angular support endpoint the strip touches the circle at one
        // point. Evaluate that point directly, avoiding cancellation in sqrt.
        if(endpoint) {
            const double half_width=mnorm;
            if(std::abs(wcenter+half_width)<=std::abs(wcenter-half_width-thickness))
                add(xcenter+(lx*mx+ly*my)/mnorm,"normal-start");
            else add(xcenter-(lx*mx+ly*my)/mnorm,"normal-end");
            return interval;
        }
        for(const double sign:{-1.,1.}) {
            const double w=wcenter+sign*(lx*mx+ly*my)/lnorm;
            if(w>=0&&w<=thickness)add(xcenter+sign*lnorm,"interior-tangent");
        }
        for(const double depth:{0.,thickness}) {
            const double displacement=(depth-wcenter)/mnorm;
            if(std::abs(displacement)>1)continue;
            const double along=displacement*(lx*mx+ly*my)/mnorm;
            const double across=std::sqrt(std::max(0.,1-displacement*displacement))*(-lx*my+ly*mx)/mnorm;
            const std::string role=depth==0?"normal-start":"normal-end";
            add(xcenter+along-across,role);add(xcenter+along+across,role);
        }
        return interval;
    };
    // The two depth-strip boundaries tangent to the disk give quadratics in
    // cos(angle). Partition their roots and retain the feasible interval.
    std::vector<double> roots{0.,1.};
    for(const double depth:{0.,thickness}) {
        const double b=normal_sign*h*depth-slope*cx-base_radius;
        const double qa=b*b-slope*slope*(ax*ax+bx*bx);
        const double qb=2*(b*cy+slope*(ax*ay+bx*by)),qc=cy*cy-ay*ay-by*by;
        const double discriminant=qb*qb-4*qa*qc;
        if(discriminant<0)continue;
        if(std::abs(qa)<=1.e-14*std::max({1.,std::abs(qb),std::abs(qc)})) {
            if(std::abs(qb)>1.e-14) {const double root=-qc/qb;if(root>0&&root<1)roots.push_back(root);}
        } else {
            const double q=-.5*(qb+std::copysign(std::sqrt(discriminant),qb));
            for(const double root:{q/qa,q!=0?qc/q:-qb/(2*qa)})if(root>0&&root<1)roots.push_back(root);
        }
    }
    std::sort(roots.begin(),roots.end());
    roots.erase(std::unique(roots.begin(),roots.end(),[](double a,double b){return std::abs(a-b)<1.e-12;}),roots.end());
    std::vector<std::pair<double,double>> supports;
    for(std::size_t i=1;i<roots.size();++i) {
        const double middle=.5*(roots[i-1]+roots[i]);
        if(!extrema(std::acos(middle)).valid)continue;
        if(!supports.empty()&&std::abs(supports.back().second-roots[i-1])<1.e-12)supports.back().second=roots[i];
        else supports.push_back({roots[i-1],roots[i]});
    }
    if(supports.size()!=1||supports.front().first<=1.e-8||supports.front().second>=1-1.e-8)return {};
    const double angle_start=std::acos(supports.front().second),angle_end=std::acos(supports.front().first);
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    std::string ancestry="sheetcut-cone-conic-envelope:source:"+token(reference.owner_id)+":"+
        token(reference.semantic_key)+":from:";
    for(const auto& [owner,key]:parents)ancestry+=token(owner)+":"+token(key)+":";
    // An optimum changes smoothly between a skin and a disk tangent where
    // its unconstrained normal depth equals a strip boundary. Split there so
    // interpolation does not have to approximate a jump in second derivative.
    const auto split_angles=[&](double sign) {
        std::vector<double> values{angle_start,angle_end};
        const auto depth_at=[&](double angle) {
            const double c=std::cos(angle);
            const double lx=(ax+slope*ay/c)/h2,ly=(bx+slope*by/c)/h2;
            const double mx=normal_sign*(slope*ax-ay/c)/h,my=normal_sign*(slope*bx-by/c)/h;
            return normal_sign*(slope*cx-cy/c+base_radius)/h+
                sign*(lx*mx+ly*my)/std::hypot(lx,ly);
        };
        for(const double depth:{0.,thickness})for(int i=0;i<128;++i) {
            double a=angle_start+(angle_end-angle_start)*i/128.;
            double b=angle_start+(angle_end-angle_start)*(i+1)/128.;
            double fa=depth_at(a)-depth,fb=depth_at(b)-depth;
            if(fa*fb>0)continue;
            for(int j=0;j<48;++j) {
                const double middle=.5*(a+b),fm=depth_at(middle)-depth;
                if((fa<=0)==(fm<=0)){a=middle;fa=fm;}else b=middle;
            }
            const double root=.5*(a+b);
            if(root>angle_start+1.e-10&&root<angle_end-1.e-10)values.push_back(root);
        }
        std::sort(values.begin(),values.end());
        values.erase(std::unique(values.begin(),values.end(),[](double a,double b){return std::abs(a-b)<1.e-10;}),values.end());
        return values;
    };
    const auto lower_angles=split_angles(-1),upper_angles=split_angles(1);
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps value;BRepGProp::VolumeProperties(shape,value);return std::abs(value.Mass());
    };
    PrimitiveData result;
    for(const int branch:{-1,1}) {
        const double shift=2*pi*std::round((.5*(u0+u1)-origin_angle-branch*.5*(angle_start+angle_end))/(2*pi));
        const double min_u=origin_angle+shift+(branch>0?angle_start:-angle_end);
        const double max_u=origin_angle+shift+(branch>0?angle_end:-angle_start);
        if(max_u<u0||min_u>u1)continue;
        const double side=gp_Vec(frame.XDirection()).Dot(direction)*std::cos(origin_angle+branch*pi/2)+
                          gp_Vec(frame.YDirection()).Dot(direction)*std::sin(origin_angle+branch*pi/2);
        const double axis_location=gp_Vec(cutter->Location(),frame.Location()).Dot(direction);
        const double radial_extent=bound_radius*std::sin(angle_end);
        const double passage_min=axis_location+std::min(0.,side*radial_extent);
        const double passage_max=axis_location+std::max(0.,side*radial_extent);
        if(passage_max<cap_min-tolerance||passage_min>cap_max+tolerance)continue;
        if(passage_min<cap_min-tolerance||passage_max>cap_max+tolerance)return {};
        struct Boundary {TopoDS_Edge edge;std::string role;};
        const auto boundary=[&](bool lower,const std::vector<double>& angles) {
            std::vector<Boundary> output;
            for(std::size_t part=1;part<angles.size();++part) {
                const double a=angles[part-1],b=angles[part];
                const bool first=part==1,last=part+1==angles.size();
                const auto point=[&](double parameter) {
                    double fraction=parameter;
                    if(first&&last)fraction=.5*(1-std::cos(pi*parameter));
                    else if(first)fraction=parameter*parameter;
                    else if(last)fraction=1-(1-parameter)*(1-parameter);
                    const double angle=a+(b-a)*fraction;
                    const auto interval=extrema(angle,(first&&parameter==0)||(last&&parameter==1));
                    if(!interval.valid)return gp_Pnt2d(std::numeric_limits<double>::quiet_NaN(),0);
                    return gp_Pnt2d(origin_angle+shift+branch*angle,h*(lower?interval.low:interval.high));
                };
                Handle(Geom2d_BSplineCurve) accepted;
                for(int intervals=16;intervals<=1024;intervals*=2) {
                    Handle(NCollection_HArray1<gp_Pnt2d>) points=new NCollection_HArray1<gp_Pnt2d>(1,intervals+1);
                    Handle(NCollection_HArray1<double>) parameters=new NCollection_HArray1<double>(1,intervals+1);
                    bool valid=true;
                    for(int i=0;i<=intervals;++i) {
                        const double parameter=double(i)/intervals;const auto value=point(parameter);
                        if(!std::isfinite(value.X())||!std::isfinite(value.Y())){valid=false;break;}
                        points->SetValue(i+1,value);parameters->SetValue(i+1,parameter);
                    }
                    if(!valid)return std::vector<Boundary>{};
                    Geom2dAPI_Interpolate interpolate(points,parameters,false,1.e-14);interpolate.Perform();
                    if(!interpolate.IsDone())return std::vector<Boundary>{};
                    const auto curve=interpolate.Curve();bool accurate=true;
                    for(int i=0;i<intervals&&accurate;++i)for(int j=1;j<8;++j) {
                        const double parameter=(i+j/8.)/intervals;
                        const auto expected=point(parameter),actual=curve->Value(parameter);
                        if(!std::isfinite(expected.X())||!std::isfinite(expected.Y())||
                           surface->Value(expected.X(),expected.Y()).Distance(surface->Value(actual.X(),actual.Y()))>
                           std::max(1.e-12,tolerance*.001)){accurate=false;break;}
                    }
                    if(accurate){accepted=curve;break;}
                }
                if(accepted.IsNull())return std::vector<Boundary>{};
                BRepBuilderAPI_MakeEdge make(accepted,surface);if(!make.IsDone())return std::vector<Boundary>{};
                const auto interval=extrema(.5*(a+b));
                output.push_back({make.Edge(),std::string(lower?"minimum-axial:":"maximum-axial:")+
                    (lower?interval.low_role:interval.high_role)});
            }
            return output;
        };
        const auto lower=boundary(true,lower_angles),upper=boundary(false,upper_angles);
        if(lower.empty()||upper.empty())return {};
        BRepBuilderAPI_MakeWire wire;std::vector<OwnedEdge> generated_edges;
        const auto append=[&](const std::vector<Boundary>& edges,bool reverse) {
            for(std::size_t i=0;i<edges.size();++i) {
                const auto& item=edges[reverse?edges.size()-1-i:i];auto edge=item.edge;if(reverse)edge.Reverse();
                wire.Add(edge);if(!wire.IsDone())return false;
                generated_edges.push_back({wire.Edge(),{reference.owner_id,ancestry+
                    (branch>0?"positive-angle:":"negative-angle:")+item.role}});
            }
            return true;
        };
        if(!append(lower,branch<0)||!append(upper,branch>0))return {};
        BRepBuilderAPI_MakeFace make(surface,wire.Wire(),true);if(!make.IsDone())return {};
        auto domain=make.Face();domain.Orientation(source.Orientation());
        const double curve_tolerance=std::max(1.e-11,tolerance*.01);
        if(!BRepLib::BuildCurves3d(domain,curve_tolerance))return {};
        BRepLib::SameParameter(domain,curve_tolerance,true);if(!BRepCheck_Analyzer(domain).IsValid())return {};
        BRepAlgoAPI_Common limit;set_sheet_cut_boolean_inputs(limit,domain,source);
        limit.SetToFillHistory(true);limit.SetFuzzyValue(tolerance);limit.Build();if(!limit.IsDone())return {};
        const auto clipped_edges=propagate_topology(limit,generated_edges,source_edges);
        for(TopExp_Explorer faces(limit.Shape(),TopAbs_FACE);faces.More();faces.Next()) {
            auto face=TopoDS::Face(faces.Current());face.Orientation(source.Orientation());
            const auto normal=sheet_cut_normal_volume(face,reference,{},tolerance);
            BRepAlgoAPI_Common reached;set_sheet_cut_boolean_inputs(reached,normal.shape,selected_projection.shape);
            reached.SetFuzzyValue(tolerance);reached.Build();if(!reached.IsDone())return {};
            if(volume(reached.Shape())<=std::max(1.e-12,tolerance*tolerance*tolerance))continue;
            TopTools_IndexedMapOfShape actual_edges;TopExp::MapShapes(face,TopAbs_EDGE,actual_edges);
            std::vector<OwnedEdge> face_edges;
            for(const auto& edge:clipped_edges)if(actual_edges.Contains(edge.shape))face_edges.push_back(edge);
            if(result.shape.IsNull()){result.shape=face;result.edges=std::move(face_edges);continue;}
            BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,result.shape,face);
            unite.SetToFillHistory(true);unite.SetFuzzyValue(tolerance);unite.Build();if(!unite.IsDone())return {};
            result.edges=propagate_topology(unite,result.edges,face_edges);result.shape=unite.Shape();
        }
    }
    return result;
}
