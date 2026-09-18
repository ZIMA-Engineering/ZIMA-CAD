// Explicit-calculation implementation. No viewer or editing path calls this file.
namespace sheet_state_geometry {
using sheet_material::Transition;
gp_Pnt mapped(const Transition& transition,const gp_Pnt& p,bool continuation) {
    const auto v=transition.map({p.X(),p.Y(),p.Z()},continuation);
    return {v.x,v.y,v.z};
}

// Preserve the complete boundary, including faces introduced by ordinary
// additive/subtractive operations. Fitting only the two sheet skins would
// discard bosses, pockets and cuts authored between state changes.
class Deformation final : public BRepTools_Modification {
public:
    Deformation(Transition transition,bool continuation,double tolerance)
        : transition_(std::move(transition)),continuation_(continuation),tolerance_(tolerance/16) {}
    Standard_Boolean NewSurface(const TopoDS_Face& face,Handle(Geom_Surface)& result,
            TopLoc_Location& location,Standard_Real& tolerance,
            Standard_Boolean& reverse_wires,Standard_Boolean& reverse_face) override {
        TopLoc_Location source_location;
        const auto source=BRep_Tool::Surface(face,source_location);
        double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!std::isfinite(u0+u1+v0+v1)||u1-u0<1e-12||v1-v0<1e-12)
            throw std::runtime_error("Sheet state has a degenerate surface domain.");
        const auto evaluate=[&](double u,double v){return mapped(transition_,source->Value(u,v).Transformed(source_location.Transformation()),continuation_);};
        std::vector<gp_Pnt> probes;
        double seed_u=(u0+u1)/2,seed_v=(v0+v1)/2;
        for(int i=0;i<=12;++i)for(int j=0;j<=12;++j) {
            const double u=u0+(u1-u0)*(i+.1)/12.2,v=v0+(v1-v0)*(j+.1)/12.2;
            BRepClass_FaceClassifier classifier(face,gp_Pnt2d(u,v),1e-8);
            if(classifier.State()==TopAbs_IN){probes.push_back(evaluate(u,v));seed_u=u;seed_v=v;}
        }
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
            BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
            for(int i=0;i<=12;++i)probes.push_back(mapped(transition_,curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/12),continuation_));
        }
        const auto seed=evaluate(seed_u,seed_v);
        const gp_Vec du(seed,evaluate(seed_u+(u1-u0)*1e-5,seed_v)),dv(seed,evaluate(seed_u,seed_v+(v1-v0)*1e-5));
        const auto normal=du.Crossed(dv);
        if(normal.SquareMagnitude()>1e-30) {
            const gp_Pln plane(seed,gp_Dir(normal));
            if(std::ranges::all_of(probes,[&](const auto& p){return plane.Distance(p)<tolerance_;})) {
                result=new Geom_Plane(plane);analytic_faces_.push_back(face);
                location={};tolerance=std::max(BRep_Tool::Tolerance(face),tolerance_*2);
                reverse_wires=false;reverse_face=false;return true;
            }
        }
        const auto& target=transition_.after;
        if(!target.unfolded&&!continuation_&&target.kind==SheetMaterialDefinition::Kind::Cone) {
            const auto& f=target;const double s=std::sin(f.cone_half_angle),c=std::cos(f.cone_half_angle);
            const auto depth_at=[&](const gp_Pnt& p){return sheet_material::coordinates(f,{p.X(),p.Y(),p.Z()}).depth;};
            const double depth=depth_at(seed);
            if(std::ranges::all_of(probes,[&](const auto& p){return std::abs(depth_at(p)-depth)<tolerance_;})) {
                const auto apex=sheet_material::sub(sheet_material::sub(f.origin,
                    sheet_material::mul(sheet_material::cone_generator(f),f.radius/s)),sheet_material::mul(f.along,depth/s));
                result=new Geom_ConicalSurface(gp_Ax3(gp_Pnt(apex.x,apex.y,apex.z),gp_Dir(f.along.x,f.along.y,f.along.z),
                    gp_Dir(f.radial.x,f.radial.y,f.radial.z)),f.cone_half_angle,0);
                const auto station=sheet_material::coordinates(f,{seed.X(),seed.Y(),seed.Z()});
                const auto outward=sheet_material::basis(f,station)[2];
                reverse_wires=normal.Dot(gp_Vec(outward.x,outward.y,outward.z))<0;reverse_face=reverse_wires;
                analytic_faces_.push_back(face);location={};tolerance=std::max(BRep_Tool::Tolerance(face),tolerance_*2);return true;
            }
        }
        if(!target.unfolded&&!continuation_&&target.kind==SheetMaterialDefinition::Kind::Cylinder) {
            const auto center=sheet_material::sub(target.origin,sheet_material::mul(target.radial,target.radius));
            const gp_Pnt origin(center.x,center.y,center.z);
            const auto axis=sheet_material::cross(target.radial,target.tangent);
            const gp_Dir direction(axis.x,axis.y,axis.z);
            const auto radius_at=[&](const gp_Pnt& p){const gp_Vec v(origin,p);return v.Crossed(gp_Vec(direction)).Magnitude();};
            const double radius=radius_at(seed);
            if(radius>tolerance_&&std::ranges::all_of(probes,[&](const auto& p){return std::abs(radius_at(p)-radius)<tolerance_;})) {
                result=new Geom_CylindricalSurface(gp_Ax3(origin,direction,gp_Dir(target.radial.x,target.radial.y,target.radial.z)),radius);
                analytic_faces_.push_back(face);location={};tolerance=std::max(BRep_Tool::Tolerance(face),tolerance_*2);
                gp_Vec radial(origin,seed);radial-=gp_Vec(direction)*radial.Dot(gp_Vec(direction));
                reverse_wires=normal.Dot(radial)<0;reverse_face=reverse_wires;return true;
            }
        }
        for(int count=5;count<=129;count=2*count-1) {
            TColgp_Array2OfPnt samples(1,count,1,count);
            for(int i=1;i<=count;++i)for(int j=1;j<=count;++j)
                samples(i,j)=evaluate(u0+(u1-u0)*(i-1)/(count-1),v0+(v1-v0)*(j-1)/(count-1));
            GeomAPI_PointsToBSplineSurface fit;fit.Interpolate(samples,Approx_IsoParametric,false);
            if(!fit.IsDone())throw std::runtime_error("Sheet state surface interpolation failed.");
            auto surface=fit.Surface();double a,b,c,d;surface->Bounds(a,b,c,d);
            TColStd_Array1OfReal uknots(1,surface->NbUKnots()),vknots(1,surface->NbVKnots());
            for(int i=1;i<=uknots.Length();++i)uknots(i)=u0+(surface->UKnot(i)-a)*(u1-u0)/(b-a);
            for(int i=1;i<=vknots.Length();++i)vknots(i)=v0+(surface->VKnot(i)-c)*(v1-v0)/(d-c);
            surface->SetUKnots(uknots);surface->SetVKnots(vknots);
            double error=0;
            // Independent quarter/half stations, never the fitted grid nodes.
            for(int i=0;i<count-1;++i)for(int j=0;j<count-1;++j)
                for(const double fraction:{.25,.5,.75}) {
                    const double u=u0+(u1-u0)*(i+fraction)/(count-1);
                    const double v=v0+(v1-v0)*(j+1-fraction)/(count-1);
                    error=std::max(error,surface->Value(u,v).Distance(evaluate(u,v)));
                }
            if(error>tolerance_)continue;
            result=surface;location=TopLoc_Location{};tolerance=std::max(BRep_Tool::Tolerance(face),tolerance_*2);
            reverse_wires=false;reverse_face=false;return true;
        }
        throw std::runtime_error("Sheet state surface exceeds the configured tolerance.");
    }
    Standard_Boolean NewCurve(const TopoDS_Edge& edge,Handle(Geom_Curve)& result,
            TopLoc_Location& location,Standard_Real& tolerance) override {
        TopLoc_Location source_location;double first,last;
        const auto source=BRep_Tool::Curve(edge,source_location,first,last);
        if(source.IsNull())return false;
        const auto evaluate=[&](double p){return mapped(transition_,source->Value(p).Transformed(source_location.Transformation()),continuation_);};
        for(int count=5;count<=513;count=2*count-1) {
            Handle(TColgp_HArray1OfPnt) points=new TColgp_HArray1OfPnt(1,count);
            Handle(TColStd_HArray1OfReal) parameters=new TColStd_HArray1OfReal(1,count);
            for(int i=1;i<=count;++i){parameters->SetValue(i,first+(last-first)*(i-1)/(count-1));points->SetValue(i,evaluate(parameters->Value(i)));}
            GeomAPI_Interpolate fit(points,parameters,false,1e-9);fit.Perform();
            if(!fit.IsDone())throw std::runtime_error("Sheet state edge interpolation failed.");
            const auto curve=fit.Curve();double error=0;
            for(int i=0;i<count-1;++i)for(const double fraction:{.25,.5,.75}) {
                const double t=first+(last-first)*(i+fraction)/(count-1);
                error=std::max(error,curve->Value(t).Distance(evaluate(t)));
            }
            if(error>tolerance_)continue;
            result=curve;location=TopLoc_Location{};tolerance=std::max(BRep_Tool::Tolerance(edge),tolerance_*2);return true;
        }
        throw std::runtime_error("Sheet state edge exceeds the configured tolerance.");
    }
    Standard_Boolean NewPoint(const TopoDS_Vertex& vertex,gp_Pnt& point,Standard_Real& tolerance) override {
        point=mapped(transition_,BRep_Tool::Pnt(vertex),continuation_);
        tolerance=std::max(BRep_Tool::Tolerance(vertex),tolerance_*2);return true;
    }
    Standard_Boolean NewCurve2d(const TopoDS_Edge& edge,const TopoDS_Face& face,
            const TopoDS_Edge& new_edge,const TopoDS_Face& new_face,Handle(Geom2d_Curve)& curve,Standard_Real& tolerance) override {
        if(std::ranges::any_of(analytic_faces_,[&](const auto& value){return value.IsSame(face);})) {
            double first,last;const auto c=BRep_Tool::Curve(new_edge,first,last);
            tolerance=tolerance_*2;
            curve=GeomProjLib::Curve2d(c,first,last,BRep_Tool::Surface(new_face),tolerance);
            if(curve.IsNull()||tolerance>tolerance_*4)throw std::runtime_error("Sheet state edge projection exceeds tolerance.");
            if(!Handle(Geom_CylindricalSurface)::DownCast(BRep_Tool::Surface(new_face)).IsNull()||
                !Handle(Geom_ConicalSurface)::DownCast(BRep_Tool::Surface(new_face)).IsNull()) {
                const double u=curve->Value((first+last)/2).X();
                const double period=2*std::numbers::pi;
                const double shift=std::floor((u+1e-7)/period)*period;
                if(std::abs(shift)>1)curve->Translate(gp_Vec2d(-shift,0));
            }
            tolerance=std::max(BRep_Tool::Tolerance(edge),tolerance_*2);return true;
        }
        double first,last;curve=BRep_Tool::CurveOnSurface(edge,face,first,last);
        tolerance=std::max(BRep_Tool::Tolerance(edge),tolerance_*2);return !curve.IsNull();
    }
    Standard_Boolean NewParameter(const TopoDS_Vertex& vertex,const TopoDS_Edge& edge,
            Standard_Real& parameter,Standard_Real& tolerance) override {
        parameter=BRep_Tool::Parameter(vertex,edge);tolerance=std::max(BRep_Tool::Tolerance(vertex),tolerance_*2);return true;
    }
    GeomAbs_Shape Continuity(const TopoDS_Edge&,const TopoDS_Face&,const TopoDS_Face&,
            const TopoDS_Edge&,const TopoDS_Face&,const TopoDS_Face&) override {return GeomAbs_C0;}
private:
    Transition transition_;bool continuation_;double tolerance_;
    std::vector<TopoDS_Face> analytic_faces_;
};

template<class Modifier,class Owned>
std::vector<Owned> modified_topology(Modifier& modifier,const TopTools_IndexedMapOfShape& members,
        const std::vector<Owned>& source) {
    std::vector<Owned> result;
    for(const auto& item:source)if(members.Contains(item.shape)) {
        const auto shape=modifier.ModifiedShape(item.shape);
        if(!shape.IsNull())result.push_back({shape,item.reference});
    }
    return result;
}

PrimitiveData deform(const PrimitiveData& input,const Transition& transition,bool continuation,double tolerance) {
    TopTools_IndexedMapOfShape members;TopExp::MapShapes(input.shape,members);
    PrimitiveData output;
    const auto collect=[&](auto& modifier) {
        output.faces=modified_topology(modifier,members,input.faces);
        output.edges=modified_topology(modifier,members,input.edges);
        output.vertices=modified_topology(modifier,members,input.vertices);
    };
    if(transition.before.unfolded==transition.after.unfolded) {
        const auto x=transition.rigid_vector({1,0,0}),y=transition.rigid_vector({0,1,0}),z=transition.rigid_vector({0,0,1}),p=transition.map({});
        gp_Trsf transform;transform.SetValues(x.x,y.x,z.x,p.x,x.y,y.y,z.y,p.y,x.z,y.z,z.z,p.z);
        BRepBuilderAPI_Transform modifier(input.shape,transform,true);output.shape=modifier.Shape();collect(modifier);
    } else {
        Handle(BRepTools_Modification) modification=new Deformation(transition,continuation,tolerance);
        BRepTools_Modifier modifier(input.shape,modification);
        if(!modifier.IsDone())throw std::runtime_error("Sheet state boundary transformation failed.");
        output.shape=modifier.ModifiedShape(input.shape);collect(modifier);
        BRepLib::SameParameter(output.shape,tolerance/8,true);
        if(!BRepCheck_Analyzer(output.shape).IsValid())throw std::runtime_error("Sheet state produced an invalid boundary.");
    }
    return output;
}

PrimitiveData regularize_hem(const PrimitiveData& input,const Transition& transition,double tolerance,const std::string& owner) {
    const auto& frame=transition.before;
    if(frame.unfolded||frame.kind!=SheetMaterialDefinition::Kind::Cylinder||
        frame.radius-frame.thickness>1e-9)return input;
    // At a mathematical zero-radius hem the complete inner skin collapses
    // onto one line. Open a sub-tolerance material coordinate domain before
    // the inverse map; otherwise atan2 at that line has no unique inverse.
    const double clearance=std::min(tolerance/32,frame.thickness*1e-4);
    Bnd_Box box;BRepBndLib::Add(input.shape,box);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
    double first=INFINITY,last=-INFINITY;
    for(unsigned mask=0;mask<8;++mask) {
        const Vec3 p{mask&1?x1:x0,mask&2?y1:y0,mask&4?z1:z0};
        const double u=sheet_material::dot(sheet_material::sub(p,frame.origin),frame.along);
        first=std::min(first,u);last=std::max(last,u);
    }
    const auto center=sheet_material::add(sheet_material::sub(frame.origin,sheet_material::mul(frame.radial,frame.radius)),sheet_material::mul(frame.along,first-1));
    BRepPrimAPI_MakeCylinder tool(gp_Ax2(gp_Pnt(center.x,center.y,center.z),gp_Dir(frame.along.x,frame.along.y,frame.along.z)),clearance,last-first+2);
    std::vector<OwnedFace> tool_faces;
    for(TopExp_Explorer face(tool.Shape(),TopAbs_FACE);face.More();face.Next()) {
        BRepAdaptor_Surface surface(TopoDS::Face(face.Current()));if(surface.GetType()!=GeomAbs_Cylinder)continue;
        FaceReference reference{owner,"sheet-state:hem-inner:from:"+frame.owner_id+":"+frame.curved_source_id};
        reference.sheet_owner=frame.owner_id;reference.sheet_role=SheetFaceRole::SideB;reference.sheet_thickness=frame.thickness;
        tool_faces.push_back({face.Current(),reference});
    }
    TopoDS_Shape argument=input.shape;
    if(!TopExp_Explorer(argument,TopAbs_SOLID).More()) {
        BRep_Builder builder;TopoDS_Compound faces;builder.MakeCompound(faces);
        for(const auto& face:input.faces)builder.Add(faces,face.shape);argument=faces;
    }
    BRepAlgoAPI_Cut cut;set_boolean_inputs(cut,argument,tool.Shape());cut.SetToFillHistory(true);cut.SetFuzzyValue(1e-7);cut.Build();
    if(!cut.IsDone())throw std::runtime_error("Cannot resolve the zero-radius hem material domain.");
    PrimitiveData result;result.shape=cut.Shape();result.faces=propagate_topology(cut,input.faces,tool_faces);
    result.edges=propagate_topology(cut,input.edges,std::vector<OwnedEdge>{});
    result.vertices=propagate_topology(cut,input.vertices,std::vector<OwnedVertex>{});
    result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,owner,"hem-material-domain");
    result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,result.vertices,owner,"hem-material-domain");
    return result;
}

PrimitiveData transform(const PrimitiveData& original_input,const std::vector<Transition>& transitions,
        double tolerance,const std::string& owner,const std::string& created_region={}) {
    if(transitions.empty())return original_input;
    auto input=original_input;
    const auto applies=[&](const Transition& transition) {
        return created_region.empty()||transition.before.owner_id==created_region;
    };
    if(std::ranges::none_of(transitions,applies))return input;
    if(!created_region.empty()) {
        const auto& transition=*std::ranges::find_if(transitions,applies);
        if(transition.before.unfolded==transition.after.unfolded)
            return deform(input,transition,false,tolerance);
    }
    for(const auto& transition:transitions)if(applies(transition)&&transition.before.unfolded!=transition.after.unfolded)
        input=regularize_hem(input,transition,tolerance,owner);
    if(input.shape.IsNull())throw std::invalid_argument("Sheet state requires an input body.");
    TopTools_ListOfShape arguments,tools;arguments.Append(input.shape);
    std::vector<OwnedFace> partition_faces;
    for(const auto& transition:transitions) {
        // Authored contributions already have an explicit material owner.
        // Foreign bends must not introduce unrelated cuts into that domain.
        if(!applies(transition))continue;
        const auto& region=transition.before;
        if(region.kind==SheetMaterialDefinition::Kind::Plane)continue;
        for(const bool end:{false,true}) {
            const sheet_material::Coordinate station{0,end?region.angle*region.neutral_radius:0,0,false};
            const auto p=sheet_material::point(region,station),n=sheet_material::basis(region,station)[1];
            const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(p.x,p.y,p.z),gp_Dir(n.x,n.y,n.z))).Face();
            tools.Append(plane);
            partition_faces.push_back({plane,FaceReference{owner,"sheet-partition:"+std::string(end?"end:":"start:")+"from:"+region.owner_id}});
        }
    }
    BRepAlgoAPI_Splitter splitter;splitter.SetArguments(arguments);splitter.SetTools(tools);
    splitter.SetNonDestructive(true);splitter.SetToFillHistory(true);splitter.Build();
    if(!splitter.IsDone())throw std::runtime_error("Sheet state region partition failed.");
    auto faces=propagate_topology(splitter,input.faces,std::vector<OwnedFace>{});
    // A partition plane coincident with an existing cap does not replace
    // that cap's parent. Only genuinely new partition faces use the tool.
    for(auto& face:propagate_topology(splitter,std::vector<OwnedFace>{},partition_faces))
        if(std::ranges::none_of(faces,[&](const auto& source){return source.shape.IsSame(face.shape);}))faces.push_back(std::move(face));
    const auto edges=complete_boolean_edges(splitter.Shape(),faces,
        propagate_topology(splitter,input.edges,std::vector<OwnedEdge>{}),owner,"sheet-partition");
    const auto vertices=complete_boolean_vertices(splitter.Shape(),faces,edges,
        propagate_topology(splitter,input.vertices,std::vector<OwnedVertex>{}),owner,"sheet-partition");
    PrimitiveData result;
    for(TopExp_Explorer solid(splitter.Shape(),TopAbs_SOLID);solid.More();solid.Next()) {
        PrimitiveData piece;piece.shape=solid.Current();TopTools_IndexedMapOfShape members;TopExp::MapShapes(piece.shape,members);
        std::set<std::string> owners;
        for(const auto& face:faces)if(members.Contains(face.shape)) {
            piece.faces.push_back(face);
            if(!face.reference.sheet_owner.empty()&&(face.reference.sheet_role==SheetFaceRole::SideA||face.reference.sheet_role==SheetFaceRole::SideB))owners.insert(face.reference.sheet_owner);
        }
        for(const auto& edge:edges)if(members.Contains(edge.shape))piece.edges.push_back(edge);
        for(const auto& vertex:vertices)if(members.Contains(vertex.shape))piece.vertices.push_back(vertex);
        if(!created_region.empty())owners={created_region};
        if(owners.size()>1)throw std::runtime_error("Sheet state could not isolate adjacent material regions.");
        const auto transition=std::ranges::find_if(transitions,[&](const auto& t){return owners.contains(t.before.owner_id);});
        if(transition!=transitions.end()) {
            GProp_GProps properties;BRepGProp::VolumeProperties(piece.shape,properties);
            const auto center=properties.CentreOfMass();
            const bool continuation=sheet_material::on_continuation(transition->before,{center.X(),center.Y(),center.Z()});
            try {piece=deform(piece,*transition,continuation,tolerance);}
            catch(const std::exception& error) {
                throw std::runtime_error("Sheet region "+transition->before.owner_id+": "+error.what());
            }
        }
        if(result.shape.IsNull())result=std::move(piece);
        else {
            BRepAlgoAPI_Fuse fuse;set_boolean_inputs(fuse,result.shape,piece.shape);fuse.SetToFillHistory(true);
            fuse.SetFuzzyValue(tolerance/4);fuse.Build();
            if(!fuse.IsDone())throw std::runtime_error("Sheet state region joining failed.");
            result.faces=propagate_topology(fuse,result.faces,piece.faces);
            result.edges=propagate_topology(fuse,result.edges,piece.edges);
            result.vertices=propagate_topology(fuse,result.vertices,piece.vertices);result.shape=fuse.Shape();
        }
    }
    if(result.shape.IsNull()||!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("Sheet state did not produce a valid solid.");
    result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,owner,"sheet-join");
    result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,result.vertices,owner,"sheet-join");
    return result;
}
void identify(PrimitiveData& result,const std::string& owner) {
    // The state operation still owns the derived topology identity needed by
    // later references.  Ordinary View selection, however, must pass through
    // to the authored feature.  Preserve the first non-state owner across any
    // number of Unbend/Bend Back cycles as separate presentation metadata.
    const auto identify_face=[&](auto& item) {
        auto& reference=item.reference;
        if(reference.display_owner_id.empty()&&reference.owner_id!=owner)
            reference.display_owner_id=reference.owner_id;
        reference.semantic_key="sheet-state:from:"+reference.owner_id+":"+reference.semantic_key;
        reference.owner_id=owner;
    };
    const auto identify_edge=[&](auto& item) {
        auto& reference=item.reference;
        if(reference.display_owner_id.empty()&&reference.owner_id!=owner)
            reference.display_owner_id=reference.owner_id;
        reference.semantic_key="sheet-state:from:"+reference.owner_id+":"+reference.semantic_key;
        reference.owner_id=owner;
    };
    for(auto& item:result.faces)identify_face(item);
    for(auto& item:result.edges)identify_edge(item);
    for(auto& item:result.vertices) {
        auto& reference=item.reference;
        reference.semantic_key="sheet-state:from:"+reference.owner_id+":"+reference.semantic_key;
        reference.owner_id=owner;
    }
}
} // namespace sheet_state_geometry
