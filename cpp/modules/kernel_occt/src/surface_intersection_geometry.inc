// Explicit calculation only. Native parents define names; traversal only
// locates shapes. An ambiguous ancestry is rejected instead of renumbered.
// Two generatrices created by one axial plane cut may have identical native
// face/endpoint ancestry. The analytic cylinder/plane pair defines two signed
// roots independently of traversal, XYZ ordering or nearest-point selection.
std::optional<std::string> cylinder_plane_edge_domain(const TopoDS_Edge& edge,
        const std::array<TopoDS_Face,2>& selected,const std::vector<OwnedFace>& faces,double tolerance) {
    const BRepAdaptor_Curve curve(edge);
    if(curve.GetType()!=GeomAbs_Line)return {};
    for(const auto& face:selected) {
        const BRepAdaptor_Surface surface(face);
        if(surface.GetType()!=GeomAbs_Cylinder)continue;
        TopTools_IndexedMapOfShape domain;TopExp::MapShapes(face,TopAbs_EDGE,domain);
        if(!domain.Contains(edge))continue;
        const auto cylinder=surface.Cylinder();const auto axis=cylinder.Axis().Direction();
        if(!curve.Line().Direction().IsParallel(axis,1e-10))continue;
        std::optional<std::string> result;
        for(const auto& adjacent:faces) {
            if(adjacent.shape.IsSame(face))continue;
            TopTools_IndexedMapOfShape boundary;TopExp::MapShapes(adjacent.shape,TopAbs_EDGE,boundary);
            if(!boundary.Contains(edge))continue;
            const BRepAdaptor_Surface support(TopoDS::Face(adjacent.shape));
            if(support.GetType()!=GeomAbs_Plane)return {};
            const auto plane=support.Plane();const auto normal=plane.Axis().Direction();
            if(std::abs(axis.Dot(normal))>1e-10)return {};
            const gp_Vec radial(cylinder.Location(),curve.Line().Location());
            const auto tangent=axis.Crossed(normal);
            const double height=gp_Vec(cylinder.Location(),plane.Location()).Dot(gp_Vec(normal));
            const double square=cylinder.Radius()*cylinder.Radius()-height*height;
            const double root=radial.Dot(gp_Vec(tangent));
            if(square<=tolerance*tolerance||std::abs(root)<=tolerance||
                plane.Distance(curve.Line().Location())>tolerance||
                std::abs(std::abs(root)-std::sqrt(square))>tolerance)return {};
            const auto token=":domain:cylinder-plane:"+encoded_topology_reference(adjacent.reference)+
                (root<0?":negative":":positive");
            if(result&&*result!=token)return {};
            result=token;
        }
        return result;
    }
    return {};
}

std::optional<std::string> conic_plane_point_domain(const gp_Pnt& point,const TopoDS_Edge& edge,
        const TopoDS_Face& plane_face,double tolerance) {
    const BRepAdaptor_Curve curve(edge);const BRepAdaptor_Surface support(plane_face);
    if(support.GetType()!=GeomAbs_Plane)return {};
    gp_Ax2 frame;double a,b;
    if(curve.GetType()==GeomAbs_Circle){const auto circle=curve.Circle();frame=circle.Position();a=b=circle.Radius();}
    else if(curve.GetType()==GeomAbs_Ellipse){const auto ellipse=curve.Ellipse();frame=ellipse.Position();a=ellipse.MajorRadius();b=ellipse.MinorRadius();}
    else return {};
    const auto plane=support.Plane();if(plane.Distance(point)>tolerance)return {};
    const gp_Vec normal(plane.Axis().Direction()),delta(frame.Location(),point);
    const double x=delta.Dot(gp_Vec(frame.XDirection()))/a,y=delta.Dot(gp_Vec(frame.YDirection()))/b;
    const double alpha=a*normal.Dot(gp_Vec(frame.XDirection())),beta=b*normal.Dot(gp_Vec(frame.YDirection()));
    const double gamma=gp_Vec(frame.Location(),plane.Location()).Dot(normal);
    const double square=alpha*alpha+beta*beta-gamma*gamma,root=alpha*y-beta*x;
    const double error=tolerance*std::max(1.,std::hypot(beta/a,alpha/b));
    if(square<=error*error||std::abs(root)<=error||std::abs(std::abs(root)-std::sqrt(square))>error||
            std::abs(x*x+y*y-1)>4*tolerance/std::min(a,b))return {};
    return root<0?":domain:conic-plane:negative":":domain:conic-plane:positive";
}

PrimitiveData make_surface_intersection_data(const SurfaceIntersectionRequest& request,const std::string& owner,
    const std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& edges,const std::vector<OwnedVertex>& vertices,
    bool sketch_association=false,const std::vector<std::string>& reference_branches={}) {
    if(!std::isfinite(request.tolerance)||request.tolerance<=0||request.faces[0]==request.faces[1])
        throw std::runtime_error("Intersection requires two different original faces and a positive tolerance.");
    std::array<TopoDS_Face,2> selected;
    std::set<std::string> face_parents;
    TopTools_IndexedMapOfShape source_edges,source_vertices;
    for(unsigned side=0;side<2;++side) {
        const auto& reference=request.faces[side];
        if(!reference.valid()||!reference.instance_path.empty())throw std::runtime_error("Intersection face reference is invalid.");
        for(const auto& face:faces)if(same_face_identity(face.reference,reference)) {
            if(!selected[side].IsNull()&&!selected[side].IsSame(face.shape))throw std::runtime_error("Intersection face reference is ambiguous.");
            selected[side]=TopoDS::Face(face.shape);
        }
        if(selected[side].IsNull())throw std::runtime_error("Intersection source face is missing at its input boundary.");
        face_parents.insert(encoded_topology_reference(reference));
        for(TopExp_Explorer it(selected[side],TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());
            // A periodic face's parameter seam does not bound its domain.
            if(!BRep_Tool::IsClosed(edge,selected[side]))source_edges.Add(edge);
        }
        TopExp::MapShapes(selected[side],TopAbs_VERTEX,source_vertices);
    }
    // Intersection is symmetric. Use native parent order for the kernel's
    // parameter direction so exchanging the two input fields cannot exchange
    // persisted start/end references. Retain each source face's orientation.
    if(encoded_topology_reference(request.faces[1])<encoded_topology_reference(request.faces[0]))std::swap(selected[0],selected[1]);
    const auto ancestry=[](const std::set<std::string>& parents) {
        std::string key;for(const auto& parent:parents)key+=std::to_string(parent.size())+":"+parent;return key;
    };
    const auto parent_key=ancestry(face_parents);
    BRepAlgoAPI_Common overlap(selected[0],selected[1]);
    overlap.SetNonDestructive(true);overlap.SetFuzzyValue(request.tolerance);overlap.Build();
    if(!overlap.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
    GProp_GProps area;BRepGProp::SurfaceProperties(overlap.Shape(),area);
    if(std::abs(area.Mass())>request.tolerance*request.tolerance)
        throw std::runtime_error("Coincident surface areas do not define a unique intersection curve.");
    BRepAlgoAPI_Section section(selected[0],selected[1],false);
    section.SetNonDestructive(true);section.SetFuzzyValue(request.tolerance);
    section.Approximation(true);section.ComputePCurveOn1(true);section.ComputePCurveOn2(true);section.Build();
    if(!section.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
    PrimitiveData result;result.shape=section.Shape();
    // A full-domain planar slice of a spline extrusion is its exact VIso,
    // not a newly fitted section spline. Section still owns domain discovery,
    // vertices and ancestry. Certify the complete native parameter interval
    // and constant-height support before retaining the source pole basis.
    // Partial domains and non-isoparametric sections keep the general result.
    // Only Sketch association requests this basis; existing Surface Intersection
    // and Trim features retain their established section representation.
    Handle(ShapeBuild_ReShape) exact_sections=new ShapeBuild_ReShape;
    bool replaced=false;
    for(unsigned side=0;sketch_association&&side<2;++side) {
        const auto extrusion=Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(BRep_Tool::Surface(selected[side]));
        const BRepAdaptor_Surface other(selected[1-side]);
        if(extrusion.IsNull()||other.GetType()!=GeomAbs_Plane)continue;
        auto basis=extrusion->BasisCurve();
        while(const auto trimmed=Handle(Geom_TrimmedCurve)::DownCast(basis))basis=trimmed->BasisCurve();
        const auto spline=Handle(Geom_BSplineCurve)::DownCast(basis);
        if(spline.IsNull())continue;
        const auto plane=other.Plane();const gp_Vec normal(plane.Axis().Direction()),direction(extrusion->Direction());
        constexpr double roundoff=64*std::numeric_limits<double>::epsilon();
        if(normal.Crossed(direction).Magnitude()>roundoff)continue;
        const double height=gp_Vec(spline->Pole(1),plane.Location()).Dot(normal)/direction.Dot(normal);
        double scale=std::max(1.,std::abs(height));
        for(int i=1;i<=spline->NbPoles();++i)scale=std::max(scale,spline->Pole(i).Distance(plane.Location()));
        const double epsilon=roundoff*scale;
        bool planar=true;
        for(int i=1;i<=spline->NbPoles();++i)
            planar&=std::abs(gp_Vec(spline->Pole(i),plane.Location()).Dot(normal)-height*direction.Dot(normal))<=epsilon;
        if(!planar)continue;
        const auto iso=extrusion->VIso(height);
        const double low=iso->FirstParameter(),high=iso->LastParameter();
        const double parameter_epsilon=roundoff*std::max({1.,std::abs(low),std::abs(high)});
        for(TopExp_Explorer it(result.shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());double first,last;
            const auto pc=BRep_Tool::CurveOnSurface(edge,selected[side],first,last);
            const auto parametric=Handle(Geom2d_BSplineCurve)::DownCast(pc);
            if(parametric.IsNull())continue;
            const auto a=pc->Value(first),b=pc->Value(last);
            const bool forward=std::abs(a.X()-low)<=parameter_epsilon&&std::abs(b.X()-high)<=parameter_epsilon;
            const bool reverse=std::abs(b.X()-low)<=parameter_epsilon&&std::abs(a.X()-high)<=parameter_epsilon;
            if(!forward&&!reverse)continue;
            bool constant=true;double previous=parametric->Pole(1).X();
            for(int i=1;i<=parametric->NbPoles();++i) {
                const auto pole=parametric->Pole(i);
                constant&=std::abs(pole.Y()-height)<=epsilon&&pole.X()>=low-parameter_epsilon&&pole.X()<=high+parameter_epsilon&&
                    (forward?pole.X()>=previous-parameter_epsilon:pole.X()<=previous+parameter_epsilon);
                previous=pole.X();
            }
            if(!constant)continue;
            const auto exact=Handle(Geom_Curve)::DownCast(iso->Copy());if(reverse)exact->Reverse();
            TopoDS_Vertex start,end;TopExp::Vertices(edge,start,end);
            BRepBuilderAPI_MakeEdge replacement(exact,start,end,exact->FirstParameter(),exact->LastParameter());
            if(!replacement.IsDone())continue;
            exact_sections->Replace(edge,replacement.Edge());replaced=true;
        }
    }
    if(replaced)result.shape=exact_sections->Apply(result.shape);
    TopTools_IndexedMapOfShape result_edges,result_vertices,curve_vertices;
    TopExp::MapShapes(result.shape,TopAbs_EDGE,result_edges);TopExp::MapShapes(result.shape,TopAbs_VERTEX,result_vertices);
    for(int i=1;i<=result_edges.Extent();++i)TopExp::MapShapes(result_edges.FindKey(i),TopAbs_VERTEX,curve_vertices);
    std::map<std::string,TopTools_IndexedMapOfShape> edge_descendants;
    if(sketch_association)for(const auto& source:edges)if(source_edges.Contains(source.shape))
        edge_descendants[encoded_topology_reference(source.reference)].Add(source.shape);
    std::vector<std::set<std::string>> edge_parents(source_edges.Extent()+1);
    for(const auto& source:edges)if(const auto index=source_edges.FindIndex(source.shape)) {
        auto token=encoded_topology_reference(source.reference);
        if(sketch_association&&edge_descendants.at(token).Extent()>1)
            if(const auto domain=cylinder_plane_edge_domain(TopoDS::Edge(source.shape),selected,faces,request.tolerance))token+=*domain;
        edge_parents[index].insert(std::move(token));
    }
    const auto endpoint_parents=[&](const TopoDS_Vertex& vertex,bool disambiguate=false) {
        const auto point=BRep_Tool::Pnt(vertex);std::set<std::string> parents;
        for(const auto& source:vertices)if(source_vertices.Contains(source.shape)&&
            BRep_Tool::Pnt(TopoDS::Vertex(source.shape)).Distance(point)<=request.tolerance)
            parents.insert(encoded_topology_reference(source.reference));
        for(int index=1;index<=source_edges.Extent();++index)if(!edge_parents[index].empty()) {
            BRepExtrema_DistShapeShape distance(vertex,source_edges.FindKey(index));
            if(distance.IsDone()&&distance.Value()<=request.tolerance) {
                std::optional<std::string> domain;
                if(sketch_association&&(disambiguate||!reference_branches.empty()))
                    domain=conic_plane_point_domain(point,TopoDS::Edge(source_edges.FindKey(index)),selected[1],request.tolerance);
                for(const auto& parent:edge_parents[index]) {
                    bool replay=false;
                    if(domain&&!disambiguate)for(const auto* sign:{":domain:conic-plane:negative",":domain:conic-plane:positive"}) {
                        const auto token=parent+sign,encoded=std::to_string(token.size())+":"+token;
                        replay|=std::ranges::any_of(reference_branches,[&](const auto& key){return key.find(encoded)!=std::string::npos;});
                    }
                    parents.insert(domain&&(disambiguate||replay)?parent+*domain:parent);
                }
            }
        }
        return parents;
    };
    if(result_edges.Extent()>1) {
        // Section may split one geometric branch at a periodic support seam.
        // Join only shared vertices and the same analytic/C1 spline domain.
        // The endpoints of each completed branch retain native ancestry.
        // No source shape is modified.
        Handle(NCollection_HSequence<TopoDS_Shape>) sequence=new NCollection_HSequence<TopoDS_Shape>;
        for(int i=1;i<=result_edges.Extent();++i)sequence->Append(result_edges.FindKey(i));
        const auto wires=ShapeAnalysis_FreeBounds::ConnectEdgesToWires(sequence,request.tolerance,true);
        BRep_Builder builder;TopoDS_Compound connected;builder.MakeCompound(connected);
        for(int i=1;i<=wires->Length();++i) {
            const auto wire=TopoDS::Wire(wires->Value(i));
            ShapeUpgrade_UnifySameDomain merge(wire,true,false,true);
            merge.SetSafeInputMode(true);merge.SetLinearTolerance(request.tolerance);merge.Build();
            TopTools_IndexedMapOfShape parts;TopExp::MapShapes(merge.Shape(),TopAbs_EDGE,parts);
            if(parts.Extent()==1){builder.Add(connected,merge.Shape());continue;}
            // Fuzzy section splines can have a small derivative mismatch at
            // their support seam. Concatenate their exact bounded geometry
            // with full C0 knot multiplicity; do not fit a smoother curve.
            GeomConvert_CompCurveToBSplineCurve joined;
            for(BRepTools_WireExplorer explorer(wire);explorer.More();explorer.Next()) {
                const auto edge=explorer.Current();double start,end;
                const auto basis=BRep_Tool::Curve(edge,start,end);
                Handle(Geom_TrimmedCurve) bounded=new Geom_TrimmedCurve(basis,start,end);
                if(edge.Orientation()==TopAbs_REVERSED)bounded->Reverse();
                const auto spline=GeomConvert::CurveToBSplineCurve(bounded);
                const auto previous=joined.BSplineCurve();
                const int degree=previous.IsNull()?spline->Degree():std::max(previous->Degree(),spline->Degree());
                if(!joined.Add(spline,request.tolerance,true,true,degree))throw std::runtime_error("Surface intersection calculation failed.");
            }
            TopoDS_Vertex first,last;TopExp::Vertices(wire,first,last);
            const auto joined_curve=joined.BSplineCurve();
            BRepBuilderAPI_MakeEdge joined_edge(joined_curve,first,last,joined_curve->FirstParameter(),joined_curve->LastParameter());
            if(!joined_edge.IsDone())throw std::runtime_error("Surface intersection calculation failed.");
            builder.Add(connected,joined_edge.Edge());
        }
        for(int i=1;i<=result_vertices.Extent();++i)if(!curve_vertices.Contains(result_vertices.FindKey(i)))builder.Add(connected,result_vertices.FindKey(i));
        result.shape=connected;
        result_edges.Clear();result_vertices.Clear();curve_vertices.Clear();
        TopExp::MapShapes(result.shape,TopAbs_EDGE,result_edges);TopExp::MapShapes(result.shape,TopAbs_VERTEX,result_vertices);
        for(int i=1;i<=result_edges.Extent();++i)TopExp::MapShapes(result_edges.FindKey(i),TopAbs_VERTEX,curve_vertices);
    }
    const auto closed_edge=[&](const TopoDS_Edge& edge) {
        const auto first=TopExp::FirstVertex(edge,true),last=TopExp::LastVertex(edge,true);
        return first.IsSame(last)||(BRepAdaptor_Curve(edge).IsClosed()&&
            BRep_Tool::Pnt(first).Distance(BRep_Tool::Pnt(last))<=request.tolerance);
    };
    int closed_branches=0;
    for(int i=1;i<=result_edges.Extent();++i)
        if(closed_edge(TopoDS::Edge(result_edges.FindKey(i))))++closed_branches;
    std::optional<gp_Torus> axial_torus;
    if(closed_branches>1)for(unsigned side=0;side<2;++side) {
        const BRepAdaptor_Surface curved(selected[side]),planar(selected[1-side]);
        if(curved.GetType()==GeomAbs_Torus&&planar.GetType()==GeomAbs_Plane&&
            curved.Torus().Axis().Direction().IsParallel(planar.Plane().Axis().Direction(),1e-10))
            axial_torus=curved.Torus();
    }
    const auto closed_role=[&](const TopoDS_Edge& edge) -> std::string {
        if(!axial_torus)return ":closed";
        // The analytic torus defines inner/outer radial domains before Section
        // locates either curve. This is a semantic support role, not a branch
        // number, geometric sorting or a nearest-curve match. Restrict it to
        // certified coaxial circles; other equal-parent loops stay ambiguous.
        const BRepAdaptor_Curve curve(edge);
        if(curve.GetType()!=GeomAbs_Circle)
            throw std::runtime_error("Intersection branch ancestry is ambiguous.");
        const auto circle=curve.Circle();const auto& torus=*axial_torus;
        const double height=gp_Vec(torus.Location(),circle.Location()).Dot(gp_Vec(torus.Axis().Direction()));
        const double square=torus.MinorRadius()*torus.MinorRadius()-height*height;
        const double delta=circle.Radius()-torus.MajorRadius();
        if(square<=request.tolerance*request.tolerance||
            std::abs(delta)<=request.tolerance||
            !circle.Axis().Direction().IsParallel(torus.Axis().Direction(),1e-10)||
            gp_Lin(torus.Axis()).Distance(circle.Location())>request.tolerance||
            std::abs(std::abs(delta)-std::sqrt(square))>request.tolerance)
            throw std::runtime_error("Intersection branch ancestry is ambiguous.");
        return delta<0?":closed:domain:torus-inner":":closed:domain:torus-outer";
    };
    std::map<std::string,TopoDS_Shape> named_vertices,named_edges;
    std::map<std::string,TopoDS_Shape> original_endpoints;std::set<std::string> ambiguous_endpoints;
    if(sketch_association)for(int i=1;i<=result_edges.Extent();++i) {
        const auto edge=TopoDS::Edge(result_edges.FindKey(i));if(closed_edge(edge))continue;
        for(const auto& vertex:{TopExp::FirstVertex(edge,true),TopExp::LastVertex(edge,true)}) {
            const auto key=ancestry(endpoint_parents(vertex));const auto [found,inserted]=original_endpoints.emplace(key,vertex);
            if(!inserted&&!found->second.IsSame(vertex))ambiguous_endpoints.insert(key);
        }
    }
    const auto name_vertex=[&](const TopoDS_Vertex& vertex,bool isolated) {
        const auto existing=std::ranges::find_if(result.vertices,[&](const auto& item){return item.shape.IsSame(vertex);});
        if(existing!=result.vertices.end())return existing->reference.semantic_key;
        auto parents=endpoint_parents(vertex);
        if(ambiguous_endpoints.contains(ancestry(parents)))parents=endpoint_parents(vertex,true);
        if(parents.empty()&&!isolated)throw std::runtime_error("Intersection endpoint ancestry is ambiguous.");
        const auto key="intersection:point:from:"+parent_key+(parents.empty()?":contact":":at:"+ancestry(parents));
        if(const auto found=named_vertices.find(key);found!=named_vertices.end()&&!found->second.IsSame(vertex))
            throw std::runtime_error("Intersection endpoint ancestry is ambiguous.");
        named_vertices.emplace(key,vertex);result.vertices.push_back({vertex,{owner,key,{}}});return key;
    };
    for(int i=1;i<=result_edges.Extent();++i) {
        const auto edge=TopoDS::Edge(result_edges.FindKey(i));
        const auto first=TopExp::FirstVertex(edge,true),last=TopExp::LastVertex(edge,true);
        const bool closed=closed_edge(edge);
        std::string role;
        if(closed)role=closed_role(edge);
        else role=":at:"+ancestry({name_vertex(first,false),name_vertex(last,false)});
        const auto key="intersection:curve:from:"+parent_key+role;
        if(!named_edges.emplace(key,edge).second)throw std::runtime_error("Intersection branch ancestry is ambiguous.");
        result.edges.push_back({edge,{owner,key,{}}});
    }
    for(int i=1;i<=result_vertices.Extent();++i)if(!curve_vertices.Contains(result_vertices.FindKey(i)))
        static_cast<void>(name_vertex(TopoDS::Vertex(result_vertices.FindKey(i)),true));
    return result;
}
