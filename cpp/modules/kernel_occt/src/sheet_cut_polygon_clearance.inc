// Convex-prism clearance on cylinders and cones, including an oblique prism.
// At a fixed source U, projected positions are affine in axial position and
// normal depth. Clipping that rectangle by the authored prism's planes gives
// the exact full-thickness material interval. Changes in its active constraints
// occur at roots of C+A*cos(U)+B*sin(U), rather than at sampled normal depths.
PrimitiveData sheet_cut_convex_prism_domain(const TopoDS_Face& source,
        const FaceReference& reference,const std::vector<OwnedEdge>& source_edges,
        const PrimitiveData& full_projection,const PrimitiveData& selected_projection,
        double tolerance) {


    const BRepAdaptor_Surface original(source);
    gp_Ax3 frame;double radius=0,angle=0;
    if(original.GetType()==GeomAbs_Cylinder) {
        frame=original.Cylinder().Position();radius=original.Cylinder().Radius();
    } else if(original.GetType()==GeomAbs_Cone) {
        frame=original.Cone().Position();radius=original.Cone().RefRadius();angle=original.Cone().SemiAngle();
    } else return {};
    if(!(reference.sheet_thickness>0))return {};
    const double slope=std::tan(angle),h=std::hypot(1.,slope),thickness=reference.sheet_thickness;
    const double normal_sign=(source.Orientation()==TopAbs_REVERSED?-1.:1.)*(frame.Direct()?1.:-1.);
    const gp_Vec axis(frame.Direction()),radial_x(frame.XDirection()),radial_y(frame.YDirection());
    double u0=0,u1=0,v0=0,v1=0;BRepTools::UVBounds(source,u0,u1,v0,v1);
    if(!(u1>u0&&v1>v0)||u1-u0>2*std::numbers::pi+1.e-8)return {};
    const double xmin=v0/h,xmax=v1/h;
    const double minimum_radius=std::min(radius+v0*std::sin(angle),radius+v1*std::sin(angle));
    if(minimum_radius-std::max(0.,normal_sign*thickness/h)<=tolerance)return {};
    const double maximum_radius=std::max(radius+v0*std::sin(angle),radius+v1*std::sin(angle));
    const Handle(Geom_Surface) surface=BRep_Tool::Surface(source);
    const auto token=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    const std::string source_key="source:"+token(reference.owner_id)+":"+token(reference.semantic_key)+":";
    struct Constraint {
        std::array<double,3> x{},w{},constant{};
        std::string key;
        std::array<double,3> coefficients(double u) const {
            const double c=std::cos(u),s=std::sin(u);
            return {x[0]+x[1]*c+x[2]*s,w[0]+w[1]*c+w[2]*s,
                constant[0]+constant[1]*c+constant[2]*s};
        }
    };
    std::vector<Constraint> constraints;
    constraints.push_back({{1,0,0},{0,0,0},{-xmin,0,0},source_key+"axial-start"});
    constraints.push_back({{-1,0,0},{0,0,0},{xmax,0,0},source_key+"axial-end"});
    constraints.push_back({{0,0,0},{1,0,0},{0,0,0},source_key+"side-a"});
    constraints.push_back({{0,0,0},{-1,0,0},{thickness,0,0},source_key+"side-b"});

    TopTools_IndexedMapOfShape vertex_shapes;TopExp::MapShapes(full_projection.shape,TopAbs_VERTEX,vertex_shapes);
    if(vertex_shapes.Extent()<4)return {};
    std::vector<gp_Pnt> vertices;gp_XYZ centre(0,0,0);
    for(int i=1;i<=vertex_shapes.Extent();++i) {
        vertices.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertex_shapes(i))));centre+=vertices.back().XYZ();
    }
    centre/=double(vertices.size());const gp_Pnt inside(centre);
    for(TopExp_Explorer faces(full_projection.shape,TopAbs_FACE);faces.More();faces.Next()) {
        const auto face=TopoDS::Face(faces.Current());const BRepAdaptor_Surface support(face);
        if(support.GetType()!=GeomAbs_Plane)return {};
        const auto plane=support.Plane();gp_Vec normal(plane.Axis().Direction());
        if(gp_Vec(plane.Location(),inside).Dot(normal)<0)normal.Reverse();
        // A concave profile, islands, or separate solids cannot be represented
        // by this intersection of half-spaces and retain the general path.
        for(const auto& vertex:vertices)
            if(gp_Vec(plane.Location(),vertex).Dot(normal)<-std::max(tolerance,BRep_Tool::Tolerance(face)))return {};
        std::set<std::pair<std::string,std::string>> parents;
        for(const auto& parent:full_projection.faces)if(parent.shape.IsSame(face))
            parents.emplace(parent.reference.owner_id,parent.reference.semantic_key);
        if(parents.empty())return {};
        std::string key="prism-plane:from:";
        for(const auto& [owner,parent]:parents)key+=token(owner)+":"+token(parent)+":";
        const double na=normal.Dot(axis),nc=normal.Dot(radial_x),ns=normal.Dot(radial_y);
        const double origin=gp_Vec(plane.Location(),frame.Location()).Dot(normal)+.25*tolerance;
        constraints.push_back({{na,slope*nc,slope*ns},
            {normal_sign*slope*na/h,-normal_sign*nc/h,-normal_sign*ns/h},
            {origin,radius*nc,radius*ns},std::move(key)});
    }
    if(constraints.size()>20)return {};
    struct Point {double x=0,w=0;std::set<std::size_t> parents;};
    struct Interval {Point low,high;bool valid=false;};
    const auto evaluate=[&](double u) {
        std::vector<Point> polygon{{xmin,0,{0,2}},{xmax,0,{1,2}},
            {xmax,thickness,{1,3}},{xmin,thickness,{0,3}}};
        for(std::size_t index=4;index<constraints.size()&&!polygon.empty();++index) {
            const auto row=constraints[index].coefficients(u);
            const auto value=[&](const Point& point){return row[0]*point.x+row[1]*point.w+row[2];};
            std::vector<Point> clipped;
            for(std::size_t j=0;j<polygon.size();++j) {
                const auto& a=polygon[j];const auto& b=polygon[(j+1)%polygon.size()];
                const double fa=value(a),fb=value(b);const bool ain=fa>=0,bin=fb>=0;
                if(ain)clipped.push_back(a);
                if(ain!=bin) {
                    const double ratio=std::clamp(fa/(fa-fb),0.,1.);
                    Point point{a.x+ratio*(b.x-a.x),a.w+ratio*(b.w-a.w),{index}};
                    for(const auto parent:a.parents)if(b.parents.contains(parent))point.parents.insert(parent);
                    clipped.push_back(std::move(point));
                }
            }
            polygon=std::move(clipped);
        }
        Interval result;if(polygon.empty())return result;
        result.low=result.high=polygon.front();result.valid=true;
        for(const auto& point:polygon) {
            if(point.x<result.low.x)result.low=point;
            if(point.x>result.high.x)result.high=point;
        }
        return result;
    };
    std::vector<double> events{u0,u1};
    const auto add_roots=[&](double c,double a,double b) {
        const double magnitude=std::hypot(a,b);
        if(magnitude<=1.e-14*std::max(1.,std::abs(c)))return;
        const double ratio=-c/magnitude;if(std::abs(ratio)>1+1.e-12)return;
        const double phase=std::atan2(b,a),offset=std::acos(std::clamp(ratio,-1.,1.));
        for(const double root:{phase-offset,phase+offset}) {
            const int first=int(std::ceil((u0-root)/(2*std::numbers::pi)));
            const int last=int(std::floor((u1-root)/(2*std::numbers::pi)));
            for(int turn=first;turn<=last;++turn) {
                const double value=root+turn*2*std::numbers::pi;
                if(value>u0+1.e-11&&value<u1-1.e-11)events.push_back(value);
            }
        }
    };
    // Axial extrema switch ends of an edge when its W coefficient vanishes.
    for(const auto& constraint:constraints)add_roots(constraint.w[0],constraint.w[1],constraint.w[2]);
    const auto determinant=[](const auto& a,const auto& b,const auto& c) {
        return a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+
            a[2]*(b[0]*c[1]-b[1]*c[0]);
    };

    for(std::size_t i=0;i<constraints.size();++i)for(std::size_t j=i+1;j<constraints.size();++j)
        for(std::size_t k=j+1;k<constraints.size();++k) {
            const auto value=[&](double u){return determinant(constraints[i].coefficients(u),
                constraints[j].coefficients(u),constraints[k].coefficients(u));};
            const double zero=value(0),opposite=value(std::numbers::pi);
            const double c=.5*(zero+opposite),a=.5*(zero-opposite),b=value(std::numbers::pi/2)-c;
            const double scale=std::max({1.,std::abs(c),std::abs(a),std::abs(b)});
            if(std::abs(value(std::numbers::pi/4)-(c+(a+b)/std::sqrt(2.)))>1.e-9*scale)return {};
            add_roots(c,a,b);
        }
    std::sort(events.begin(),events.end());
    events.erase(std::unique(events.begin(),events.end(),[](double a,double b){return std::abs(a-b)<1.e-10;}),events.end());
    struct Span {double first=0,last=0;std::set<std::size_t> lower,upper;};
    std::vector<std::vector<Span>> components;bool previous=false;
    for(std::size_t i=1;i<events.size();++i) {
        const auto middle=evaluate(.5*(events[i-1]+events[i]));
        if(!middle.valid||middle.high.x-middle.low.x<=1.e-12){previous=false;continue;}
        if(!previous)components.emplace_back();
        auto& spans=components.back();
        if(!spans.empty()&&spans.back().lower==middle.low.parents&&spans.back().upper==middle.high.parents)
            spans.back().last=events[i];
        else spans.push_back({events[i-1],events[i],middle.low.parents,middle.high.parents});
        previous=true;
    }
    if(components.empty())return {};
    const auto volume=[](const TopoDS_Shape& shape) {
        GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);return std::abs(properties.Mass());
    };
    PrimitiveData result;std::set<std::string> component_keys;
    for(const auto& spans:components) {
        if(spans.size()>128)return {};
        std::string component_role="connected-support:";
        if(components.size()>1) {
            // Distinguish disconnected roots by the authored constraints that
            // open their angular support and by the derivative at that root.
            // Numeric angles and component enumeration never become identity.
            const auto& entry=spans.front();
            if(std::abs(entry.first-u0)<=1.e-10)component_role="source-angular-start:";
            else {
                std::set<std::size_t> active=entry.lower;active.insert(entry.upper.begin(),entry.upper.end());
                std::vector<std::size_t> ordered(active.begin(),active.end());
                std::sort(ordered.begin(),ordered.end(),[&](auto a,auto b){return constraints[a].key<constraints[b].key;});
                std::set<std::string> roles;
                for(std::size_t i=0;i<ordered.size();++i)for(std::size_t j=i+1;j<ordered.size();++j)
                    for(std::size_t k=j+1;k<ordered.size();++k) {
                        const auto value=[&](double u){return determinant(constraints[ordered[i]].coefficients(u),
                            constraints[ordered[j]].coefficients(u),constraints[ordered[k]].coefficients(u));};
                        const double zero=value(0),opposite=value(std::numbers::pi);
                        const double c=.5*(zero+opposite),a=.5*(zero-opposite),b=value(std::numbers::pi/2)-c;
                        const double scale=std::max({1.,std::abs(c),std::abs(a),std::abs(b)});
                        const double derivative=-a*std::sin(entry.first)+b*std::cos(entry.first);
                        if(std::abs(c+a*std::cos(entry.first)+b*std::sin(entry.first))>1.e-8*scale||
                                std::abs(derivative)<=1.e-10*scale)continue;
                        std::string role=derivative>0?"positive-event:from:":"negative-event:from:";
                        for(const auto index:{ordered[i],ordered[j],ordered[k]})role+=token(constraints[index].key)+":";
                        roles.insert(std::move(role));
                    }
                if(roles.empty())return {};
                component_role="angular-support-entry:";
                for(const auto& role:roles)component_role+=token(role)+":";
            }
            if(!component_keys.insert(component_role).second)return {};
        }
        struct Boundary {Handle(Geom2d_Curve) curve;std::string key;};
        const auto boundaries=[&](bool lower) {
            std::vector<Boundary> values;std::set<std::string> boundary_keys;
            // The opposite envelope may change its active planes without
            // changing this boundary. Do not create multiple child edges for
            // that unrelated change in the other side's topology.
            std::vector<Span> boundary_spans;
            for(const auto& span:spans) {
                if(!boundary_spans.empty()&&(lower?boundary_spans.back().lower:boundary_spans.back().upper)==
                        (lower?span.lower:span.upper))boundary_spans.back().last=span.last;
                else boundary_spans.push_back(span);
            }
            for(const auto& span:boundary_spans) {
                const double delta=std::min((span.last-span.first)*1.e-4,1.e-8/std::max(1.,maximum_radius));
                const auto value=[&](double u) {
                    const auto interval=evaluate(u);
                    return interval.valid?(lower?interval.low.x:interval.high.x):std::numeric_limits<double>::quiet_NaN();
                };
                const auto point=[&](double p) {
                    const double u=span.first+(span.last-span.first)*p;double x=0;
                    // Use the one-sided limit at singular event angles. The
                    // extrapolation error is second order in a sub-nanometre
                    // angular displacement, rather than selecting an isolated
                    // zero-area feasible edge at the event itself.
                    if(p==0)x=2*value(span.first+delta)-value(span.first+2*delta);
                    else if(p==1)x=2*value(span.last-delta)-value(span.last-2*delta);
                    else x=value(u);
                    return gp_Pnt2d(u,h*x);
                };
                Handle(Geom2d_BSplineCurve) accepted;
                for(int count=16;count<=512;count*=2) {
                    Handle(TColgp_HArray1OfPnt2d) points=new TColgp_HArray1OfPnt2d(1,count+1);
                    Handle(TColStd_HArray1OfReal) parameters=new TColStd_HArray1OfReal(1,count+1);
                    for(int i=0;i<=count;++i) {
                        const double p=double(i)/count;const auto sample=point(p);
                        if(!std::isfinite(sample.Y()))return std::vector<Boundary>{};
                        points->SetValue(i+1,sample);parameters->SetValue(i+1,p);
                    }
                    Geom2dAPI_Interpolate interpolate(points,parameters,false,1.e-14);interpolate.Perform();
                    if(!interpolate.IsDone())return std::vector<Boundary>{};
                    const auto curve=interpolate.Curve();bool accurate=true;
                    for(int i=0;i<count&&accurate;++i)for(int j=1;j<8;++j) {
                        const double p=(i+j/8.)/count;const auto expected=point(p),actual=curve->Value(p);
                        if(!std::isfinite(expected.Y())||surface->Value(expected.X(),expected.Y()).Distance(
                            surface->Value(actual.X(),actual.Y()))>std::max(1.e-11,tolerance*.001)){accurate=false;break;}
                    }
                    if(accurate){accepted=curve;break;}
                }
                if(accepted.IsNull())return std::vector<Boundary>{};
                std::string key="sheetcut-prism-envelope:"+source_key+component_role+
                    (lower?"minimum-axial:":"maximum-axial:");
                std::set<std::string> parents;
                for(const auto parent:lower?span.lower:span.upper)parents.insert(constraints[parent].key);
                for(const auto& parent:parents)key+=token(parent)+":";
                // A repeated disconnected boundary with the same ancestry
                // needs a richer semantic role; retain the general path until
                // that role can be established without enumerating fragments.
                if(!boundary_keys.insert(key).second)return std::vector<Boundary>{};
                values.push_back({accepted,std::move(key)});
            }
            return values;
        };
        const auto lower=boundaries(true);if(lower.empty())return {};
        const auto upper=boundaries(false);if(upper.empty())return {};
        BRepBuilderAPI_MakeWire wire;std::vector<std::pair<Handle(Geom2d_Curve),EdgeReference>> constructed;
        const auto append=[&](const Handle(Geom2d_Curve)& curve,const std::string& key,bool reverse) {
            BRepBuilderAPI_MakeEdge make(curve,surface);if(!make.IsDone())return false;
            auto edge=make.Edge();if(reverse)edge.Reverse();
            double first=0,last=0;const auto stored=BRep_Tool::CurveOnSurface(edge,surface,TopLoc_Location(),first,last);
            wire.Add(edge);if(!wire.IsDone())return false;
            constructed.push_back({stored,{reference.owner_id,key}});return true;
        };
        const auto connector=[&](const gp_Pnt2d& a,const gp_Pnt2d& b,const std::string& role,
                                 const std::string& first_parent,const std::string& second_parent) {
            if(surface->Value(a.X(),a.Y()).Distance(surface->Value(b.X(),b.Y()))<=tolerance*.01)return true;
            std::set<std::string> parents{first_parent,second_parent};
            std::string key="sheetcut-prism-envelope:"+source_key+component_role+role+":from:";
            for(const auto& parent:parents)key+=token(parent)+":";
            return append(GCE2d_MakeSegment(a,b).Value(),key,false);
        };
        for(const auto& edge:lower)if(!append(edge.curve,edge.key,false))return {};
        if(!connector(lower.back().curve->Value(1),upper.back().curve->Value(1),"angular-end",
                      lower.back().key,upper.back().key))return {};
        for(auto edge=upper.rbegin();edge!=upper.rend();++edge)if(!append(edge->curve,edge->key,true))return {};
        if(!connector(upper.front().curve->Value(0),lower.front().curve->Value(0),"angular-start",
                      upper.front().key,lower.front().key))return {};
        BRepBuilderAPI_MakeFace make(surface,wire.Wire(),true);if(!make.IsDone())return {};
        auto domain=make.Face();domain.Orientation(source.Orientation());std::vector<OwnedEdge> edges;
        TopTools_IndexedMapOfShape final_edges;TopExp::MapShapes(domain,TopAbs_EDGE,final_edges);
        for(int i=1;i<=final_edges.Extent();++i) {
            double first=0,last=0;const auto curve=BRep_Tool::CurveOnSurface(TopoDS::Edge(final_edges(i)),domain,first,last);
            bool found=false;
            for(const auto& [expected,parent]:constructed)if(curve==expected){edges.push_back({final_edges(i),parent});found=true;}
            if(!found)return {};
        }
        const double curve_tolerance=std::max(1.e-11,tolerance*.01);
        if(!BRepLib::BuildCurves3d(domain,curve_tolerance))return {};
        BRepLib::SameParameter(domain,curve_tolerance,true);if(!BRepCheck_Analyzer(domain).IsValid())return {};
        BRepAlgoAPI_Common limit;set_sheet_cut_boolean_inputs(limit,domain,source);
        limit.SetToFillHistory(true);limit.SetFuzzyValue(tolerance);limit.Build();if(!limit.IsDone())return {};

        const auto clipped_edges=propagate_topology(limit,edges,source_edges);
        for(TopExp_Explorer faces(limit.Shape(),TopAbs_FACE);faces.More();faces.Next()) {
            auto face=TopoDS::Face(faces.Current());face.Orientation(source.Orientation());
            const auto normal=sheet_cut_normal_volume(face,reference,{},tolerance);
            BRepAlgoAPI_Common reached;set_sheet_cut_boolean_inputs(reached,normal.shape,selected_projection.shape);
            reached.SetFuzzyValue(tolerance);reached.Build();if(!reached.IsDone())return {};

            bool component_reached=volume(reached.Shape())>std::max(1.e-12,tolerance*tolerance*tolerance);
            // Coincident analytic skins can make Common return an empty shape
            // even when an interior point proves that these solids overlap.
            // This only selects a connected component; the caller still proves
            // full passage containment with an independent Boolean subtraction.
            if(!component_reached) {
                for(TopExp_Explorer selected_solids(selected_projection.shape,TopAbs_SOLID);
                    selected_solids.More()&&!component_reached;selected_solids.Next()) {
                    const auto selected_solid=TopoDS::Solid(selected_solids.Current());
                    GProp_GProps properties;BRepGProp::VolumeProperties(selected_solid,properties);
                    const auto centre=properties.CentreOfMass();
                    BRepClass3d_SolidClassifier self(selected_solid,centre,tolerance);
                    if(self.State()!=TopAbs_IN)continue;
                    for(TopExp_Explorer normal_solids(normal.shape,TopAbs_SOLID);
                        normal_solids.More();normal_solids.Next()) {
                        BRepClass3d_SolidClassifier normal_classifier(
                            TopoDS::Solid(normal_solids.Current()),centre,tolerance);
                        if(normal_classifier.State()==TopAbs_IN){component_reached=true;break;}
                    }
                }
            }
            if(!component_reached)continue;
            TopTools_IndexedMapOfShape actual_edges;TopExp::MapShapes(face,TopAbs_EDGE,actual_edges);
            std::vector<OwnedEdge> owned;
            for(const auto& edge:clipped_edges)if(actual_edges.Contains(edge.shape))owned.push_back(edge);
            if(result.shape.IsNull()){result.shape=face;result.edges=std::move(owned);continue;}
            BRepAlgoAPI_Fuse unite;set_sheet_cut_boolean_inputs(unite,result.shape,face);
            unite.SetToFillHistory(true);unite.SetFuzzyValue(tolerance);unite.Build();if(!unite.IsDone())return {};
            result.edges=propagate_topology(unite,result.edges,owned);result.shape=unite.Shape();
        }
    }
    return result;
}
