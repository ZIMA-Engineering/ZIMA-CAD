std::optional<SurfaceGeometry> analytic_surface(const TopoDS_Face& face);

class WrapCurveEvaluator final:public AdvApprox_EvaluatorFunction {
public:
    WrapCurveEvaluator(occ::handle<Geom_Curve> curve,const SurfaceWrap& map,const WrappingRequest& request)
        :curve_(std::move(curve)),map_(map),request_(request){}
    gp_Pnt value(double t)const {
        const auto p=curve_->Value(t);
        const Vec3 delta{p.X()-request_.origin.x,p.Y()-request_.origin.y,p.Z()-request_.origin.z};
        const auto q=map_.point(wrap_detail::dot(delta,request_.x_axis),wrap_detail::dot(delta,request_.y_axis));
        return {q.x,q.y,q.z};
    }
    void Evaluate(int* dimension,double[2],double* parameter,int* derivative,double* result,int* error)override {
        *error=0;if(*dimension!=3||*derivative!=0){*error=1;return;}
        const auto p=value(*parameter);result[0]=p.X();result[1]=p.Y();result[2]=p.Z();
    }
private:
    occ::handle<Geom_Curve> curve_;
    const SurfaceWrap& map_;
    const WrappingRequest& request_;
};

PrimitiveData make_wrapping_data(const WrappingRequest& request,const std::string& owner,
        const std::vector<OwnedFace>& available_faces) {
    if(request.sketch_id.empty()||!request.target.valid()||(request.profiles.empty()&&request.images.empty())||
            !std::isfinite(request.tolerance)||request.tolerance<=0||!std::isfinite(request.height)||request.height<=0)
        throw std::invalid_argument("Invalid Wrap definition.");
    TopoDS_Face target;
    for(const auto& face:available_faces)if(same_face_identity(face.reference,request.target)) {
        if(!target.IsNull()&&!target.IsSame(face.shape))throw std::runtime_error("Wrap target is ambiguous.");
        target=TopoDS::Face(face.shape);
    }
    if(target.IsNull())throw std::runtime_error("Wrap target is unavailable.");
    const auto analytic=analytic_surface(target);
    if(!analytic||(analytic->kind!=SurfaceGeometry::Kind::Cylinder&&analytic->kind!=SurfaceGeometry::Kind::Cone&&
            analytic->kind!=SurfaceGeometry::Kind::Sphere))
        throw std::runtime_error("Select a cylindrical, conical or spherical face to wrap.");
    const SurfaceWrap map(*analytic,request.origin,request.x_axis,request.y_axis,request.reverse_u,request.reverse_v);
    // BRep_Tool returns a world-space supporting surface, including Location.
    auto support=BRep_Tool::Surface(target);
    const double tolerance=request.tolerance;
    const auto anchor=map.point(0,0);
    if(analytic->kind==SurfaceGeometry::Kind::Cylinder) {
        // SurfaceWrap uses a right-handed analytic frame. A native circle
        // Extrusion can have an indirect support, and a converted analytic
        // surface need not share these UV coordinates. Use an equivalent
        // direct support for our exact p-curves; retain the real bounded face
        // for trimming and reference identity.
        support=new Geom_CylindricalSurface(gp_Ax3(
            gp_Pnt(analytic->origin.x,analytic->origin.y,analytic->origin.z),
            gp_Dir(analytic->axis.x,analytic->axis.y,analytic->axis.z),
            gp_Dir(analytic->radial.x,analytic->radial.y,analytic->radial.z)),analytic->radius);
    } else if(analytic->kind==SurfaceGeometry::Kind::Sphere) {
        // A native sphere's arbitrary UV pole must not become a singularity
        // of an otherwise regular inscription. Put the anchor on this
        // equivalent support's equator; the bounded target remains unchanged.
        const auto radial=wrap_detail::unit(wrap_detail::sub(anchor,analytic->origin));
        const auto guide=std::abs(wrap_detail::dot(radial,analytic->axis))<.9?analytic->axis:analytic->radial;
        const auto axis=wrap_detail::unit(wrap_detail::sub(guide,wrap_detail::scale(radial,wrap_detail::dot(guide,radial))));
        support=new Geom_SphericalSurface(gp_Ax3(gp_Pnt(analytic->origin.x,analytic->origin.y,analytic->origin.z),
            gp_Dir(axis.x,axis.y,axis.z),gp_Dir(radial.x,radial.y,radial.z)),analytic->radius);
    }
    ShapeAnalysis_Surface parameterization(support);
    const auto anchor_uv=parameterization.ValueOfUV(gp_Pnt(anchor.x,anchor.y,anchor.z),tolerance);
    PrimitiveData result;BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    const std::string prefix="wrap:sketch:"+field(request.sketch_id);
    std::map<std::string,TopoDS_Vertex> shared_points;
    const auto vertex=[&](const std::string& key,const gp_Pnt& point) {
        auto [it,added]=shared_points.try_emplace(key);
        if(added){it->second=BRepBuilderAPI_MakeVertex(point).Vertex();builder.UpdateVertex(it->second,tolerance);}
        else if(BRep_Tool::Pnt(it->second).Distance(point)>tolerance)throw std::runtime_error("Wrap source incidence is inconsistent.");
        return it->second;
    };
    const auto mapped_edge=[&](const TopoDS_Edge& source,const std::string& key,
            const std::string& start_key,const std::string& end_key) {
        double first,last;const auto curve=BRep_Tool::Curve(source,first,last);
        if(curve.IsNull()||!std::isfinite(first)||!std::isfinite(last)||last<=first)
            throw std::runtime_error("Wrap source curve is invalid.");
        WrapCurveEvaluator evaluator(curve,map,request);
        occ::handle<Geom2d_Curve> pcurve;
        if(analytic->kind==SurfaceGeometry::Kind::Cylinder) {
            const gp_Ax3 frame(gp_Pnt(request.origin.x,request.origin.y,request.origin.z),
                gp_Dir(request.normal.x,request.normal.y,request.normal.z),gp_Dir(request.x_axis.x,request.x_axis.y,request.x_axis.z));
            auto planar=GeomAPI::To2d(curve,gp_Pln(frame));
            auto trimmed=new Geom2d_TrimmedCurve(planar,first,last);
            auto spline=Geom2dConvert::CurveToBSplineCurve(trimmed);
            const auto anchor=map.point(0,0);const auto delta=wrap_detail::sub(anchor,analytic->origin);
            const auto tangent=wrap_detail::cross(analytic->axis,analytic->radial);
            const double angle=std::atan2(wrap_detail::dot(delta,tangent),wrap_detail::dot(delta,analytic->radial));
            const double axial=wrap_detail::dot(delta,analytic->axis);
            for(int i=1;i<=spline->NbPoles();++i) {
                const auto p=spline->Pole(i);const auto [u,v]=map.canonical_coordinates(p.X(),p.Y());
                spline->SetPole(i,gp_Pnt2d(angle+u/analytic->radius,axial+v));
            }
            pcurve=spline;first=spline->FirstParameter();last=spline->LastParameter();
        } else {
            occ::handle<NCollection_HArray1<double>> tolerances=new NCollection_HArray1<double>(1,1);
            tolerances->SetValue(1,tolerance/8);
            AdvApprox_ApproxAFunction fit(0,0,1,{}, {},tolerances,first,last,GeomAbs_C0,12,1024,evaluator);
            if(!fit.IsDone()||!fit.HasResult()||fit.MaxError(3,1)>tolerance/8) {
                throw std::runtime_error("Wrap curve could not meet the model tolerance.");
            }
            NCollection_Array1<gp_Pnt> poles(1,fit.NbPoles());fit.Poles(1,poles);
            occ::handle<Geom_Curve> mapped=new Geom_BSplineCurve(poles,fit.Knots()->Array1(),fit.Multiplicities()->Array1(),fit.Degree());
            double projection_tolerance=tolerance/8;
            pcurve=GeomProjLib::Curve2d(mapped,first,last,support,projection_tolerance);
            if(pcurve.IsNull()||projection_tolerance>tolerance/4) {
                throw std::runtime_error("Wrap curve could not meet the model tolerance.");
            }
            // Independent checks use points other than the approximation nodes,
            // over each fitted span, against the analytic source map.
            for(int span=1;span<fit.NbKnots();++span)for(int sample=0;sample<=32;++sample) {
                const double t=fit.Knots()->Value(span)+(fit.Knots()->Value(span+1)-fit.Knots()->Value(span))*sample/32.;
                const auto uv=pcurve->Value(t);const auto actual=support->Value(uv.X(),uv.Y());
                if(actual.Distance(evaluator.value(t))>tolerance/2) {
                    throw std::runtime_error("Wrap curve could not meet the model tolerance.");
                }
            }
        }
        if(support->IsUPeriodic()) {
            const double period=support->UPeriod(),mid=pcurve->Value((first+last)/2).X();
            pcurve->Translate(gp_Vec2d(period*std::round((anchor_uv.X()-mid)/period),0));
        }
        const auto first_uv=pcurve->Value(first),last_uv=pcurve->Value(last);
        const auto a=support->Value(first_uv.X(),first_uv.Y()),b=support->Value(last_uv.X(),last_uv.Y());
        const bool reversed=source.Orientation()==TopAbs_REVERSED;
        const auto va=vertex(reversed?end_key:start_key,a),vb=vertex(reversed?start_key:end_key,b);
        BRepBuilderAPI_MakeEdge edge(pcurve,support,va,vb,first,last);
        if(!edge.IsDone())throw std::runtime_error("Wrap curve construction failed.");
        auto mapped=edge.Edge();
        if(!BRepLib::BuildCurve3d(mapped,tolerance/4))throw std::runtime_error("Wrap curve construction failed.");
        if(reversed)mapped.Reverse();
        // Validate the trimmed target, including holes, at the actual mapped
        // edge. Wrapping onto the analytic support must not escape its face.
        BRepAdaptor_Curve check(mapped);
        for(int i=0;i<=64;++i) {
            const auto point=check.Value(check.FirstParameter()+(check.LastParameter()-check.FirstParameter())*i/64.);
            BRepClass_FaceClassifier classifier(target,point,tolerance);
            if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON)
                throw std::runtime_error("Wrapped geometry extends outside the selected face.");
        }
        // Point samples cannot detect a narrow hole between two samples.
        // Check the complete mapped edge against the exact bounded face.
        BRepAlgoAPI_Cut outside;
        NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(mapped);tools.Append(target);
        outside.SetArguments(arguments);outside.SetTools(tools);outside.SetNonDestructive(true);outside.SetFuzzyValue(tolerance);outside.Build();
        if(!outside.IsDone()||outside.HasErrors())throw std::runtime_error("Wrapped region is not valid.");
        GProp_GProps uncovered;BRepGProp::LinearProperties(outside.Shape(),uncovered);
        if(uncovered.Mass()>tolerance)throw std::runtime_error("Wrapped geometry extends outside the selected face.");
        result.edges.push_back({mapped,{owner,key,{}}});
        result.vertices.push_back({va,{owner,reversed?end_key:start_key,{}}});
        result.vertices.push_back({vb,{owner,reversed?start_key:end_key,{}}});
        return mapped;
    };
    std::vector<ExtrusionRequest> profiles;
    for(const auto& source:request.profiles) {
        auto main=source;main.additional_profile_regions.clear();profiles.push_back(std::move(main));
        for(const auto& region:source.additional_profile_regions) {
            auto profile=source;profile.additional_profile_regions.clear();
            profile.outer_profile=region.outer_profile;profile.inner_profiles=region.inner_profiles;
            profile.outer_edge_source_ids=region.outer_edge_source_ids;profile.inner_edge_source_ids=region.inner_edge_source_ids;
            profile.outer_vertex_source_ids=region.outer_vertex_source_ids;profile.inner_vertex_source_ids=region.inner_vertex_source_ids;
            profiles.push_back(std::move(profile));
        }
    }
    for(const auto& profile:profiles) {
        std::vector<TopoDS_Wire> wires;std::vector<std::string> sources;
        const auto source_wires=make_body_profiles(profile,request.normal);
        for(const auto& wire:source_wires) {
            if(wire.edges.size()!=wire.curve_ids.size()||(!wire.point_ids.empty()&&wire.point_ids.size()!=wire.edges.size()))
                throw std::runtime_error("Wrap source ancestry is missing.");
            BRepBuilderAPI_MakeWire mapped;
            for(std::size_t i=0;i<wire.edges.size();++i) {
                const auto key=prefix+":curve:"+field(wire.curve_ids[i]);sources.push_back(key);
                const auto a=wire.point_ids.empty()?key+":seam":prefix+":point:"+field(wire.point_ids[i]);
                const auto b=wire.point_ids.empty()?a:prefix+":point:"+field(i+1<wire.point_ids.size()?wire.point_ids[i+1]:
                    !profile.open_profile_end_id.empty()?profile.open_profile_end_id:wire.point_ids.front());
                mapped.Add(mapped_edge(wire.edges[i],key,a,b));
            }
            if(!mapped.IsDone())throw std::runtime_error("Wrap curve construction failed.");
            wires.push_back(mapped.Wire());
        }
        if(request.output==WrappingResult::Curves) {for(const auto& wire:wires)builder.Add(compound,wire);continue;}
        if(wires.empty())throw std::runtime_error("Wrap requires closed Sketch regions.");
        BRepBuilderAPI_MakeFace face(support,wires.front(),true);
        for(std::size_t i=1;i<wires.size();++i) {
            BRepBuilderAPI_MakeFace hole(support,wires[i],true);
            if(!hole.IsDone())throw std::runtime_error("Wrap requires closed Sketch regions.");
            face.Add(TopoDS::Wire(BRepTools::OuterWire(hole.Face()).Reversed()));
        }
        if(!face.IsDone()||!BRepCheck_Analyzer(face.Face()).IsValid())throw std::runtime_error("Wrapped region is not valid.");
        auto skin=face.Face();
        // MakeFace may reverse the face to bound a clockwise authored wire.
        // Material direction belongs to the selected support, independently
        // of that wire winding and the support's native UV handedness.
        const auto skin_surface=analytic_surface(skin);
        if(!skin_surface)throw std::runtime_error("Wrapped region is not valid.");
        if(skin_surface->reversed!=analytic->reversed)skin.Reverse();
        BRepAlgoAPI_Cut outside;
        NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(skin);tools.Append(target);
        outside.SetArguments(arguments);outside.SetTools(tools);outside.SetNonDestructive(true);outside.SetFuzzyValue(tolerance);outside.Build();
        if(!outside.IsDone()||outside.HasErrors())throw std::runtime_error("Wrapped region is not valid.");
        GProp_GProps uncovered,boundary_length;BRepGProp::SurfaceProperties(outside.Shape(),uncovered);
        BRepGProp::LinearProperties(skin,boundary_length);
        if(uncovered.Mass()>tolerance*std::max(tolerance,boundary_length.Mass()))
            throw std::runtime_error("Wrapped geometry extends outside the selected face.");
        std::ranges::sort(sources);std::string key=prefix+":region";for(const auto& source:sources)key+=":"+field(source);
        FaceReference reference{owner,key,{}};reference.surface_result=true;
        if(request.output==WrappingResult::Surface) {result.faces.push_back({skin,reference});builder.Add(compound,skin);}
        else {
            SurfaceThickenRequest thickness;thickness.face=reference;thickness.thickness=request.height;
            thickness.side=request.inward?SurfaceThicknessSide::Second:SurfaceThicknessSide::First;
            thickness.tolerance=tolerance;
            auto solid=make_thickened_surface_data(thickness,owner,skin,{{skin,reference}},result.edges,result.vertices).operand;
            builder.Add(compound,solid.shape);result.faces.insert(result.faces.end(),solid.faces.begin(),solid.faces.end());
            result.edges.insert(result.edges.end(),solid.edges.begin(),solid.edges.end());
            result.vertices.insert(result.vertices.end(),solid.vertices.begin(),solid.vertices.end());
        }
    }
    result.shape=compound;
    if(!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("Wrapped region is not valid.");
    return result;
}
