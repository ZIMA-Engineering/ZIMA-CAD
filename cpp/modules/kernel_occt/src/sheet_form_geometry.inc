// Explicit calculation of copied native FORM histories. No UI or picker calls
// this implementation, and no OCCT enumeration defines a persistent identity.
namespace sheet_form_geometry {
struct Operands {PrimitiveData cut,flat,formed;bool rounded_corner{};std::vector<ViewerAxis> axes;};
struct Instance {SheetMaterialDefinition material;Operands operands;};

PrimitiveData compound(const std::vector<PrimitiveData>& inputs) {
    PrimitiveData result;TopoDS_Compound shape;BRep_Builder builder;builder.MakeCompound(shape);
    bool populated=false;
    for(const auto& input:inputs)if(!input.shape.IsNull()) {
        populated=true;
        builder.Add(shape,input.shape);
        result.faces.insert(result.faces.end(),input.faces.begin(),input.faces.end());
        result.edges.insert(result.edges.end(),input.edges.begin(),input.edges.end());
        result.vertices.insert(result.vertices.end(),input.vertices.begin(),input.vertices.end());
    }
    if(populated)result.shape=shape;
    return result;
}

bool disjoint(const std::vector<PrimitiveData>& inputs,double tolerance) {
    // Keep sequential cut/fuse semantics when footprints overlap or touch.
    // Exact surface bounds (including shape tolerances) conservatively gate
    // batching; neither tessellation nor a weaker modeling precision is used.
    std::vector<Bnd_Box> boxes;boxes.reserve(inputs.size());
    for(const auto& input:inputs) {
        Bnd_Box box;
        if(!input.shape.IsNull())BRepBndLib::AddOptimal(input.shape,box,false,true);
        if(!box.IsVoid())box.Enlarge(std::max(1e-7,tolerance));
        for(const auto& previous:boxes)
            if(!box.IsVoid()&&!previous.IsVoid()&&!box.IsOut(previous))return false;
        boxes.push_back(box);
    }
    return true;
}

std::string_view axis_region(const AxisReference& reference) {
    constexpr std::string_view prefix="centerline:from:centroid:copy:region:";
    if(!reference.semantic_key.starts_with(prefix))return reference.owner_id;
    const auto text=std::string_view(reference.semantic_key).substr(prefix.size());
    const auto colon=text.find(':');std::size_t length{};
    if(colon==std::string_view::npos)return {};
    const auto parsed=std::from_chars(text.data(),text.data()+colon,length);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+colon||length>text.size()-colon-1)return {};
    return text.substr(colon+1,length);
}

std::vector<ViewerAxis> moved_axes(std::vector<ViewerAxis> axes,const gp_Trsf& transform) {
    for(auto& axis:axes) {
        gp_Pnt point(axis.point.x,axis.point.y,axis.point.z);point.Transform(transform);
        gp_Vec direction(axis.direction.x,axis.direction.y,axis.direction.z);direction.Transform(transform);
        axis.point={point.X(),point.Y(),point.Z()};axis.direction={direction.X(),direction.Y(),direction.Z()};
    }
    return axes;
}

std::vector<ViewerAxis> profile_axes(const std::vector<ViewerAxis>& source) {
    using namespace sheet_material;
    constexpr std::string_view prefix="centerline:from:centroid:";
    std::vector<ViewerAxis> result;
    for(auto axis:source) {
        if(!axis.reference.semantic_key.starts_with(prefix))continue;
        const auto parent=feature_side_parent(std::string_view(axis.reference.semantic_key).substr(prefix.size()));
        if(parent)axis.reference.semantic_key=std::string(prefix)+"profile:"+
            std::to_string(parent->feature_id.size())+":"+parent->feature_id+":"+
            std::to_string(parent->source_id.size())+":"+parent->source_id;
        const auto joined=std::ranges::find_if(result,[&](const auto& item) {
            return item.reference==axis.reference&&std::abs(dot(item.direction,axis.direction))>1.-1e-10&&
                dot(cross(sub(item.point,axis.point),item.direction),cross(sub(item.point,axis.point),item.direction))<1e-14;
        });
        if(joined==result.end())result.push_back(std::move(axis));
        else {
            // One Form axis describes the whole source-profile path, including
            // both authored extrusion sides. Their cap/face side identities
            // remain separate; the new axis has the Sketch as its parent.
            const auto shift=dot(sub(axis.point,joined->point),joined->direction);
            const auto low=std::min(1.-joined->display_length*.5,shift+1.-axis.display_length*.5);
            const auto high=std::max(joined->display_length*.5-1.,shift+axis.display_length*.5-1.);
            joined->point=add(joined->point,mul(joined->direction,(low+high)*.5));
            joined->display_length=high-low+2.;
        }
    }
    return result;
}

PrimitiveData repair_parameterization(const PrimitiveData& source,double tolerance) {
    if(source.shape.IsNull()||BRepCheck_Analyzer(source.shape,true,false,true).IsValid())return source;
    // Boolean copies of rational blend patches can retain stale SameParameter
    // flags. Repair a private copy, never the cached source or supporting Body.
    BRepBuilderAPI_Copy copied(source.shape,true,false);
    if(!copied.IsDone())throw std::runtime_error("FORM could not create a valid sheet.");
    BRepCheck_Analyzer diagnosis(copied.Shape(),true,false,true);
    BRep_Builder builder;ShapeFix_Edge projector;
    for(TopExp_Explorer it(copied.Shape(),TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());const auto checked=diagnosis.Result(edge);
        if(checked.IsNull())continue;
        for(checked->InitContextIterator();checked->MoreShapeInContext();checked->NextShapeInContext()) {
            if(checked->ContextualShape().ShapeType()!=TopAbs_FACE||
                !std::ranges::any_of(checked->StatusOnShape(),[](auto status) {
                    return status==BRepCheck_InvalidCurveOnSurface||status==BRepCheck_InvalidSameParameterFlag;
                }))continue;
            const auto face=TopoDS::Face(checked->ContextualShape());const auto seam=BRep_Tool::IsClosed(edge,face);
            builder.UpdateEdge(edge,Handle(Geom2d_Curve){},face,BRep_Tool::Tolerance(edge));
            static_cast<void>(projector.FixAddPCurve(edge,face,seam,1e-7));
        }
    }
    BRepLib::SameParameter(copied.Shape(),1e-7,true);
    if(!BRepCheck_Analyzer(copied.Shape(),true,false,true).IsValid()) {
        if(std::getenv("ZIMA_CPP_FORM_PROFILE"))std::fprintf(stderr,"FORM copy parameterization repair failed validation\n");
        throw std::runtime_error("FORM could not create a valid sheet.");
    }
    PrimitiveData result;result.shape=copied.Shape();
    for(const auto& face:source.faces)result.faces.push_back({copied.ModifiedShape(face.shape),face.reference});
    for(const auto& edge:source.edges) {
        const auto changed=copied.ModifiedShape(edge.shape);
        if(BRep_Tool::Tolerance(TopoDS::Edge(changed))>
            std::max(BRep_Tool::Tolerance(TopoDS::Edge(edge.shape)),tolerance)*1.01+1e-12)
        {
            if(std::getenv("ZIMA_CPP_FORM_PROFILE"))std::fprintf(stderr,"FORM copy parameterization tolerance %.9g exceeds %.9g\n",BRep_Tool::Tolerance(TopoDS::Edge(changed)),std::max(BRep_Tool::Tolerance(TopoDS::Edge(edge.shape)),tolerance));
            throw std::runtime_error("FORM could not create a valid sheet.");
        }
        result.edges.push_back({changed,edge.reference});
    }
    for(const auto& vertex:source.vertices) {
        const auto changed=copied.ModifiedShape(vertex.shape);
        if(BRep_Tool::Tolerance(TopoDS::Vertex(changed))>
            std::max(BRep_Tool::Tolerance(TopoDS::Vertex(vertex.shape)),tolerance)*1.01+1e-12)
            throw std::runtime_error("FORM could not create a valid sheet.");
        result.vertices.push_back({changed,vertex.reference});
    }
    return result;
}

PrimitiveData copy(const PrimitiveData& source,const gp_Trsf& transform,const HistoryOperation& operation,
        const std::string& region,unsigned index) {
    if(source.shape.IsNull())return {};
    // Keep rigid direct copies located on their exact source geometry. Baking
    // a translation into rational blend surfaces changes their numerical
    // parameterization before the subsequent sheet Boolean. Reflections are
    // still copied by OCCT because they cannot be represented by a location.
    BRepBuilderAPI_Transform moved(source.shape,transform,false);moved.Build();
    if(!moved.IsDone())throw std::runtime_error("Feature copy transformation failed.");
    PrimitiveData result;result.shape=moved.Shape();
    const auto& request=*operation.feature_copy;
    const bool pattern=request.combination==BodyCombination::Pattern;
    const auto reference=[&](auto ref) {
        ref.semantic_key=(pattern?"pattern:"+pattern_copy_id(validated_pattern(request.pattern),index)+":" : std::string{})+
            mirror_source_key(ref.owner_id,ref.semantic_key);
        ref.owner_id=operation.owner_id;ref.instance_path.clear();return ref;
    };
    for(const auto& face:source.faces) {
        auto ref=reference(face.reference);ref.surface.reset();ref.display_owner_id.clear();ref.sheet_owner=region;
        result.faces.push_back({moved.ModifiedShape(face.shape),std::move(ref)});
    }
    for(const auto& edge:source.edges)result.edges.push_back({moved.ModifiedShape(edge.shape),reference(edge.reference)});
    for(const auto& vertex:source.vertices)result.vertices.push_back({moved.ModifiedShape(vertex.shape),reference(vertex.reference)});
    return result;
}

PrimitiveData read_surface(const BodyResult& snapshot,bool solid=false) {
    if(snapshot.kernel_shape.empty()||snapshot.kernel_faces.empty()||(solid?snapshot.volume<=0.:snapshot.volume!=0.)||
        !snapshot.calculation_errors.empty())throw std::runtime_error("FORM definition calculation failed.");
    PrimitiveData result;
    BRep_Builder builder;BRepTools_ShapeSet archive(builder);
    std::istringstream stream(snapshot.kernel_shape);archive.Read(stream);archive.Read(result.shape,stream);
    if(result.shape.IsNull())throw std::runtime_error("FORM definition calculation failed.");
    const auto restore=[&](const auto& bindings,TopAbs_ShapeEnum kind,auto& owned) {
        TopTools_IndexedMapOfShape members;TopExp::MapShapes(result.shape,kind,members);
        for(const auto& item:bindings) {
            char orientation{};int object{},location{};std::istringstream fields(item.locator);
            if(!(fields>>orientation>>object>>location)||std::string_view("+-ie").find(orientation)==std::string_view::npos||
                object<1||object>archive.NbShapes()||location<0||!item.reference.valid()||!item.reference.instance_path.empty())
                throw std::runtime_error("FORM definition calculation failed.");
            fields>>std::ws;if(!fields.eof())throw std::runtime_error("FORM definition calculation failed.");
            TopoDS_Shape shape;std::istringstream address(item.locator);archive.Read(shape,address);
            if(shape.IsNull()||shape.ShapeType()!=kind||!members.Contains(shape))
                throw std::runtime_error("FORM definition calculation failed.");
            owned.push_back({shape,item.reference});
        }
    };
    restore(snapshot.kernel_faces,TopAbs_FACE,result.faces);restore(snapshot.kernel_edges,TopAbs_EDGE,result.edges);
    restore(snapshot.kernel_vertices,TopAbs_VERTEX,result.vertices);
    require_complete_unique_shell_topology(result.shape,TopAbs_FACE,result.faces,"faces");
    require_complete_unique_shell_topology(result.shape,TopAbs_EDGE,result.edges,"edges");
    require_complete_unique_shell_topology(result.shape,TopAbs_VERTEX,result.vertices,"vertices");
    return result;
}

gp_Ax3 frame(Vec3 origin,Vec3 normal,Vec3 x) {
    const auto n=sheet_material::unit(normal),u=sheet_material::unit(x);
    if(std::abs(sheet_material::dot(n,u))>1e-10)
        throw std::invalid_argument("Invalid FORM placement frame.");
    return {gp_Pnt(origin.x,origin.y,origin.z),gp_Dir(n.x,n.y,n.z),gp_Dir(u.x,u.y,u.z)};
}

// Length-prefixed native parents can be recovered without an OCCT lookup.
template<class Reference>
std::string parent_key(std::string_view role,const std::string& definition,const Reference& parent) {
    if(role=="shape"&&parent.semantic_key.starts_with("thicken:")) {
        const auto from=parent.semantic_key.find(":from:");
        if(from!=std::string::npos)return "form:shape:"+parent.semantic_key.substr(8,from-8)+
            ":from:"+std::to_string(definition.size())+":"+definition+":"+parent.semantic_key.substr(from+6);
    }
    return "form:"+std::string(role)+":from:"+std::to_string(definition.size())+":"+definition+
        ":"+std::to_string(parent.owner_id.size())+":"+parent.owner_id+":"+parent.semantic_key;
}

PrimitiveData move(const PrimitiveData& source,const gp_Trsf& transform) {
    if(source.shape.IsNull())return {};
    // A rigid, orientation-preserving placement uses a location and shares
    // immutable source geometry/triangulation. Subsequent Booleans explicitly
    // use non-destructive inputs; offsets still own their private copy.
    BRepBuilderAPI_Transform moved(source.shape,transform,false);moved.Build();
    if(!moved.IsDone())throw std::runtime_error("FORM placement transformation failed.");
    PrimitiveData result;result.shape=moved.Shape();
    for(const auto& face:source.faces)result.faces.push_back({moved.ModifiedShape(face.shape),face.reference});
    for(const auto& edge:source.edges)result.edges.push_back({moved.ModifiedShape(edge.shape),edge.reference});
    for(const auto& vertex:source.vertices)result.vertices.push_back({moved.ModifiedShape(vertex.shape),vertex.reference});
    return result;
}

std::string preparation_key(const SheetFormRequest& form,double tolerance) {
    auto request=form;
    request.definition_id.clear();request.support={};request.position={};
    request.normal={0,0,1};request.x_direction={1,0,0};
    HistoryOperation operation;operation.primitive=std::move(request);operation.boolean_tolerance=tolerance;
    return history_fingerprint({operation},1);
}

PrimitiveData place(const PrimitiveData& source,const gp_Trsf& transform,
        const SheetFormRequest& request,const std::string& owner,std::string_view role) {
    if(source.shape.IsNull())return {};
    BRepBuilderAPI_Transform moved(source.shape,transform,false);moved.Build();
    if(!moved.IsDone())throw std::runtime_error("FORM placement transformation failed.");
    PrimitiveData result;result.shape=moved.Shape();
    const auto reference=[&](auto parent) {
        if(!parent.valid())throw std::runtime_error("FORM requires native topology ancestry.");
        parent.semantic_key=parent_key(role,request.definition_id,parent);
        parent.owner_id=owner;parent.instance_path.clear();return parent;
    };
    for(const auto& face:source.faces) {
        auto ref=reference(face.reference);ref.surface.reset();ref.measured_area.reset();
        ref.surface_result=false;ref.display_owner_id.clear();
        ref.sheet_owner=owner;ref.sheet_thickness=request.thickness;
        ref.sheet_role=SheetFaceRole::ThicknessFace;
        if(role=="shape") {
            if(!request.solid_opening_faces.empty())ref.sheet_role=face.reference.sheet_role;
            if(face.reference.semantic_key.starts_with("thicken:cap:start:from:"))ref.sheet_role=SheetFaceRole::SideA;
            if(face.reference.semantic_key.starts_with("thicken:cap:end:from:"))ref.sheet_role=SheetFaceRole::SideB;
        }
        result.faces.push_back({moved.ModifiedShape(face.shape),std::move(ref)});
    }
    for(const auto& edge:source.edges)result.edges.push_back({moved.ModifiedShape(edge.shape),reference(edge.reference)});
    for(const auto& vertex:source.vertices)result.vertices.push_back({moved.ModifiedShape(vertex.shape),reference(vertex.reference)});
    return result;
}

bool corner_definition(const SheetFormRequest& request) {
    return !request.solid_opening_faces.empty()||(request.definition&&std::ranges::count_if(*request.definition,[&](const auto& operation) {
        return operation.body.id==request.cut_body&&!operation.suppressed;
    })==2);
}

bool on_bend_corner(const SheetFormRequest& request,const TopoDS_Face& support,
        const std::vector<OwnedFace>& faces,const std::vector<HistoryOperation>& history,std::size_t index) {
    using namespace sheet_material;
    const auto regions=regions_before(history,index);
    for(const auto& bend:regions.regions) {
        if(bend.kind!=SheetMaterialDefinition::Kind::Cylinder||bend.curved_source_id.empty()||bend.unfolded||
           std::abs(bend.angle-std::numbers::pi/2)>1e-10||std::abs(bend.thickness-request.thickness)>1e-10)continue;
        const auto displacement=sub(request.position,bend.origin);const auto station=dot(displacement,bend.along);
        const auto radial=dot(displacement,bend.radial),tangent=dot(displacement,bend.tangent);
        const bool first_side=std::abs(dot(request.normal,bend.radial))>1.-1e-10;
        if(!first_side&&std::abs(dot(request.normal,bend.tangent))<1.-1e-10)continue;
        const bool inner=dot(request.normal,first_side?bend.radial:bend.tangent)<0.;
        if((!inner&&!request.solid_cut_snapshot)||
           std::abs(radial-(inner?-bend.thickness:0.))>1e-7||
           std::abs(tangent-(inner?bend.radius-bend.thickness:bend.radius))>1e-7||
           std::abs(dot(request.x_direction,bend.along))<1.-1e-10)continue;
        const auto first_contact=add(add(bend.origin,mul(bend.along,station)),mul(bend.radial,radial));
        const auto second_contact=add(add(bend.origin,mul(bend.along,station)),
            add(mul(bend.radial,-bend.radius),mul(bend.tangent,tangent)));
        const auto contact=first_side?first_contact:second_contact;
        // The selected planar face must actually meet this Bend tangency.
        // Matching an infinite plane alone does not establish adjacency.
        BRepClass_FaceClassifier adjacent(support,gp_Pnt(contact.x,contact.y,contact.z),1e-7);
        if(adjacent.State()!=TopAbs_IN&&adjacent.State()!=TopAbs_ON)continue;
        bool has_other_side=false;
        const auto other_normal=cross(request.normal,request.x_direction);
        const auto other_contact=first_side?second_contact:first_contact;
        for(const auto& face:faces) {
            if(face.reference.sheet_owner.empty()||std::abs(face.reference.sheet_thickness-request.thickness)>1e-10||
               (face.reference.sheet_role!=SheetFaceRole::SideA&&face.reference.sheet_role!=SheetFaceRole::SideB))continue;
            const auto actual=TopoDS::Face(face.shape);BRepAdaptor_Surface surface(actual);
            if(surface.GetType()!=GeomAbs_Plane)continue;
            const auto plane=surface.Plane();auto outward=plane.Axis().Direction();
            if(!plane.Position().Direct())outward.Reverse();if(actual.Orientation()==TopAbs_REVERSED)outward.Reverse();
            if(plane.Distance(gp_Pnt(request.position.x,request.position.y,request.position.z))>1e-7||
               outward.Dot(gp_Dir(other_normal.x,other_normal.y,other_normal.z))<1.-1e-10)continue;
            BRepClass_FaceClassifier joining(actual,gp_Pnt(other_contact.x,other_contact.y,other_contact.z),1e-7);
            if(joining.State()==TopAbs_IN||joining.State()==TopAbs_ON){has_other_side=true;break;}
        }
        if(!has_other_side)continue;
        for(const auto& face:faces)if(face.reference.sheet_owner==bend.owner_id) {
            BRepAdaptor_Surface cylinder(TopoDS::Face(face.shape));
            if(cylinder.GetType()!=GeomAbs_Cylinder||std::abs(cylinder.Cylinder().Radius()-bend.radius)>1e-7)continue;
            double low=std::numeric_limits<double>::infinity(),high=-low;
            for(TopExp_Explorer vertex(face.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
                const auto coordinate=dot(sub({p.X(),p.Y(),p.Z()},bend.origin),bend.along);
                low=std::min(low,coordinate);high=std::max(high,coordinate);
            }
            if(station>=low-1e-7&&station<=high+1e-7)return true;
        }
    }
    return false;
}

void validate_support(const SheetFormRequest& request,const TopoDS_Shape& input,
        const std::vector<OwnedFace>& faces,const std::vector<HistoryOperation>& history,std::size_t index) {
    if(!request.definition||request.definition->empty()||request.definition_id.empty()||
        request.shape_body.empty()||request.cut_body.empty()||!request.surface.valid()||!request.support.valid()||
        !request.support.instance_path.empty()||input.IsNull()||!std::isfinite(request.thickness)||request.thickness<=0)
        throw std::invalid_argument("Invalid FORM definition.");
    const gp_Pnt position(request.position.x,request.position.y,request.position.z);
    const gp_Dir normal(request.normal.x,request.normal.y,request.normal.z);
    bool has_planar_support=false;
    // One native identity may own several visible trimmed fragments. Locate
    // the fragment containing the insertion point, never its first ancestor.
    for(TopExp_Explorer face(input,TopAbs_FACE);face.More();face.Next()) {
        const auto actual=TopoDS::Face(face.Current());
        const auto support=std::ranges::find_if(faces,[&](const auto& owned){
            return owned.shape.IsSame(actual)&&same_face_identity(owned.reference,request.support);
        });
        if(support==faces.end()||support->reference.sheet_owner.empty()||
            (support->reference.sheet_role!=SheetFaceRole::SideA&&support->reference.sheet_role!=SheetFaceRole::SideB)||
            std::abs(support->reference.sheet_thickness-request.thickness)>1e-10)continue;
        BRepAdaptor_Surface surface(actual);if(surface.GetType()!=GeomAbs_Plane)continue;
        has_planar_support=true;
        const auto plane=surface.Plane();auto outward=plane.Axis().Direction();
        if(!plane.Position().Direct())outward.Reverse();
        if(actual.Orientation()==TopAbs_REVERSED)outward.Reverse();
        if(plane.Distance(position)>1e-7||outward.Dot(normal)<1.-1e-10)continue;
        BRepClass_FaceClassifier classifier(actual,position,1e-7);
        if(corner_definition(request)) {
            if(on_bend_corner(request,actual,faces,history,index))return;
        }else if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)return;
    }
    throw std::invalid_argument(has_planar_support?
        "FORM position and direction must follow the selected sheet side.":"Select a planar outer sheet face for FORM.");
}

PrimitiveData combine(const PrimitiveData& input,const PrimitiveData& tool,bool subtract,
        const HistoryOperation& operation,const std::vector<HistoryOperation>& history) {
    if(tool.shape.IsNull())return input;
    if(input.shape.IsNull())throw std::invalid_argument("FORM requires an input sheet.");
    const auto apply=[&](auto& algorithm) {
        set_boolean_inputs(algorithm,input.shape,tool.shape);algorithm.SetToFillHistory(true);
        algorithm.SetNonDestructive(true);
        algorithm.SetFuzzyValue(std::max(1e-7,operation.boolean_tolerance));algorithm.Build();
        if(std::getenv("ZIMA_CPP_FORM_DUMP")) {
            BRepTools::Write(input.shape,"build/form-diagnostic/form-input.brep");
            BRepTools::Write(tool.shape,"build/form-diagnostic/form-tool.brep");
            if(algorithm.IsDone())BRepTools::Write(algorithm.Shape(),"build/form-diagnostic/form-result.brep");
        }
        if(!algorithm.IsDone()||algorithm.Shape().IsNull()||!BRepCheck_Analyzer(algorithm.Shape()).IsValid())
            throw std::runtime_error("FORM could not create a valid sheet.");
        PrimitiveData result;result.shape=algorithm.Shape();
        result.faces=propagate_topology(algorithm,input.faces,tool.faces);
        result.edges=propagate_topology(algorithm,input.edges,tool.edges);
        result.vertices=propagate_topology(algorithm,input.vertices,tool.vertices);
        result.faces=complete_boolean_faces(result.shape,result.faces,result.edges,result.vertices,operation.owner_id,"form",history);
        result.edges=complete_boolean_edges(result.shape,result.faces,result.edges,operation.owner_id,"form");
        result.vertices=complete_boolean_vertices(result.shape,result.faces,result.edges,result.vertices,operation.owner_id,"form");
        return result;
    };
    if(subtract){BRepAlgoAPI_Cut algorithm;return apply(algorithm);}
    BRepAlgoAPI_Fuse algorithm;return apply(algorithm);
}

// Remove only the material outside the real outer Bend cylinder in its
// quarter-domain. The source Shell may close at the two virtual sharp planes;
// those closing walls must not protrude through the destination's rounded side.
PrimitiveData trim_corner_shell(const PrimitiveData& input,const PrimitiveData& formed,
        const SheetFormRequest& request,const HistoryOperation& operation,
        const std::vector<HistoryOperation>& history,std::size_t index) {
    using namespace sheet_material;
    const auto regions=regions_before(history,index);
    const auto bend=std::ranges::find_if(regions.regions,[&](const auto& region) {
        if(region.kind!=SheetMaterialDefinition::Kind::Cylinder||region.curved_source_id.empty()||region.unfolded||
            std::abs(region.angle-std::numbers::pi/2)>1e-10||std::abs(region.thickness-request.thickness)>1e-10)return false;
        const auto displacement=sub(request.position,region.origin);
        const auto r=dot(displacement,region.radial),t=dot(displacement,region.tangent);
        return (std::abs(r)<1e-7&&std::abs(t-region.radius)<1e-7)||
            (std::abs(r+region.thickness)<1e-7&&std::abs(t-region.radius+region.thickness)<1e-7);
    });
    if(bend==regions.regions.end())throw std::invalid_argument("FORM position and direction must follow the selected sheet side.");
    std::map<std::string,FaceReference> parents;
    for(const auto& face:input.faces)if(face.reference.sheet_owner==bend->owner_id) {
        BRepAdaptor_Surface surface(TopoDS::Face(face.shape));
        if(surface.GetType()==GeomAbs_Cylinder&&std::abs(surface.Cylinder().Radius()-bend->radius)<1e-7)
            parents.emplace(encoded_topology_reference(face.reference),face.reference);
    }
    if(parents.empty())throw std::runtime_error("FORM definition calculation failed.");
    const auto parent="form:corner-radius-trim:from:"+encoded_topology_reference(parents.begin()->second);
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(TopExp_Explorer vertex(formed.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));
        const auto x=dot(sub({p.X(),p.Y(),p.Z()},bend->origin),bend->along);
        low=std::min(low,x);high=std::max(high,x);
    }
    if(!std::isfinite(low)||!std::isfinite(high)||high-low<1e-7)
        throw std::runtime_error("FORM definition calculation failed.");
    const auto center=sub(bend->origin,mul(bend->radial,bend->radius));
    const auto point=[&](double r,double t,double x){return add(center,add(mul(bend->radial,r),add(mul(bend->tangent,t),mul(bend->along,x))));};
    ExtrusionRequest box;box.outer_profile=ExtrusionRequest::PolygonProfile{{point(0,0,low),point(bend->radius,0,low),point(bend->radius,bend->radius,low),point(0,bend->radius,low)}};
    box.direction=mul(bend->along,high-low);box.profile_region_id=parent+":quarter-region";box.outer_boundary_id=parent+":quarter-boundary";
    for(const auto* role:{"radial-low-tangent-low","radial-high-tangent-low","radial-high-tangent-high","radial-low-tangent-high"})box.outer_vertex_source_ids.push_back(parent+":point:"+role);
    for(const auto* role:{"tangent-low","radial-high","tangent-high","radial-low"})box.outer_edge_source_ids.push_back(parent+":edge:"+role);
    const auto quarter=make_extrusion_data(box,operation.owner_id,{},2'000'000.,2'000'000.,{},1e-7);
    RevolutionRequest round;round.outer_profile=ExtrusionRequest::PolygonProfile{{point(0,0,low),point(bend->radius,0,low),point(bend->radius,0,high),point(0,0,high)}};
    round.axis_point=center;round.axis_direction=bend->along;round.profile_normal=cross(bend->radial,bend->along);
    round.profile_region_id=parent+":cylinder-region";round.outer_boundary_id=parent+":cylinder-boundary";
    for(const auto* role:{"axis-low","outer-low","outer-high","axis-high"})round.outer_vertex_source_ids.push_back(parent+":point:"+role);
    for(const auto* role:{"axial-low","outer-radius","axial-high","axis"})round.outer_edge_source_ids.push_back(parent+":edge:"+role);
    const auto cylinder=make_revolution_data(round,operation.owner_id,{},1e-7);
    const auto exterior=combine(quarter,cylinder,true,operation,history);
    return combine(formed,exterior,true,operation,history);
}

// Continue the authored planar side walls through the destination Bend.
// The bounded slabs restore only actual input material in the Bend domain;
// their planes and thickness are those of the already calculated FORM skin.
PrimitiveData corner_joining_material(const PrimitiveData& input,const PrimitiveData& formed,
        const SheetFormRequest& request,const HistoryOperation& operation,
        const std::vector<HistoryOperation>& history,std::size_t index) {
    using namespace sheet_material;
    if(!corner_definition(request))return formed;
    const auto regions=regions_before(history,index);
    const auto bend=std::ranges::find_if(regions.regions,[&](const auto& region) {
        const auto displacement=sub(request.position,region.origin);
        return region.kind==SheetMaterialDefinition::Kind::Cylinder&&!region.curved_source_id.empty()&&!region.unfolded&&
            std::abs(region.angle-std::numbers::pi/2)<1e-10&&std::abs(region.thickness-request.thickness)<1e-10&&
            std::abs(dot(displacement,region.radial)+region.thickness)<1e-7&&
            std::abs(dot(displacement,region.tangent)-(region.radius-region.thickness))<1e-7;
    });
    if(bend==regions.regions.end())throw std::invalid_argument("FORM position and direction must follow the selected sheet side.");
    const auto prism=[&](std::vector<Vec3> points,Vec3 direction,double length,const std::string& parent) {
        ExtrusionRequest slab;slab.outer_profile=ExtrusionRequest::PolygonProfile{std::move(points)};
        slab.direction=mul(direction,length);slab.profile_region_id=parent+":region";slab.outer_boundary_id=parent+":boundary";
        for(const auto* role:{"low-u-low-v","high-u-low-v","high-u-high-v","low-u-high-v"})
            slab.outer_vertex_source_ids.push_back(parent+":point:"+role);
        for(const auto* role:{"low-v","high-u","high-v","low-u"})slab.outer_edge_source_ids.push_back(parent+":edge:"+role);
        return make_extrusion_data(slab,operation.owner_id,{},2'000'000.,2'000'000.,{},1e-7);
    };
    const auto intersect=[&](const PrimitiveData& a,const PrimitiveData& b) {
        BRepAlgoAPI_Common common;set_boolean_inputs(common,a.shape,b.shape);common.SetNonDestructive(true);
        common.SetToFillHistory(true);common.SetFuzzyValue(std::max(1e-7,operation.boolean_tolerance));common.Build();
        if(!common.IsDone()||common.Shape().IsNull()||!BRepCheck_Analyzer(common.Shape(),true,false,true).IsValid())
            throw std::runtime_error("FORM could not create a valid sheet.");
        PrimitiveData piece;piece.shape=common.Shape();
        piece.faces=propagate_topology(common,a.faces,b.faces);piece.edges=propagate_topology(common,a.edges,b.edges);
        piece.vertices=propagate_topology(common,a.vertices,b.vertices);
        piece.faces=complete_boolean_faces(piece.shape,piece.faces,piece.edges,piece.vertices,operation.owner_id,"form-corner",history);
        piece.edges=complete_boolean_edges(piece.shape,piece.faces,piece.edges,operation.owner_id,"form-corner");
        piece.vertices=complete_boolean_vertices(piece.shape,piece.faces,piece.edges,piece.vertices,operation.owner_id,"form-corner");return piece;
    };
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(TopExp_Explorer vertex(formed.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertex.Current()));const auto x=dot(sub({p.X(),p.Y(),p.Z()},bend->origin),bend->along);
        low=std::min(low,x);high=std::max(high,x);
    }
    const auto point=[&](double x,double r,double t){return add(bend->origin,add(mul(bend->along,x),add(mul(bend->radial,r),mul(bend->tangent,t))));};
    const auto domain_key=parent_key("corner-domain",request.definition_id,request.support)+":bend:"+bend->owner_id;
    const auto domain=prism({point(low,0,0),point(high,0,0),point(high,-bend->radius,0),point(low,-bend->radius,0)},
        bend->tangent,bend->radius,domain_key);
    const auto curved_input=intersect(input,domain);
    PrimitiveData result=formed;
    for(const auto& face:formed.faces) {
        if(!face.reference.semantic_key.starts_with("form:shape:cap:start:from:"))continue;
        const auto actual=TopoDS::Face(face.shape);BRepAdaptor_Surface surface(actual);if(surface.GetType()!=GeomAbs_Plane)continue;
        const auto plane=surface.Plane();auto outward=plane.Axis().Direction();
        if(!plane.Position().Direct())outward.Reverse();if(actual.Orientation()==TopAbs_REVERSED)outward.Reverse();
        const Vec3 normal{outward.X(),outward.Y(),outward.Z()};if(std::abs(dot(normal,bend->along))<.5)continue;
        const auto origin=plane.Location();const Vec3 anchor{origin.X(),origin.Y(),origin.Z()};
        const auto u=unit(sub(bend->radial,mul(normal,dot(bend->radial,normal)))),v=cross(normal,u);
        double u0=std::numeric_limits<double>::infinity(),u1=-u0,v0=u0,v1=-u0;
        for(double x:{low,high})for(double r:{-bend->radius,0.})for(double t:{0.,bend->radius}) {
            const auto delta=sub(point(x,r,t),anchor);const auto pu=dot(delta,u),pv=dot(delta,v);
            u0=std::min(u0,pu);u1=std::max(u1,pu);v0=std::min(v0,pv);v1=std::max(v1,pv);
        }
        const auto p=[&](double a,double b){return add(anchor,add(mul(u,a),mul(v,b)));};
        const auto wall=prism({p(u0,v0),p(u1,v0),p(u1,v1),p(u0,v1)},mul(normal,-1.),request.thickness,
            parent_key("corner-wall",request.definition_id,face.reference)+":bend:"+bend->owner_id);
        auto piece=intersect(curved_input,wall);if(!TopExp_Explorer(piece.shape,TopAbs_SOLID).More())continue;
        for(auto& owned:piece.faces) {
            owned.reference.semantic_key=parent_key("corner-join",request.definition_id,owned.reference);
            owned.reference.owner_id=operation.owner_id;owned.reference.sheet_owner=operation.owner_id;
            owned.reference.sheet_thickness=request.thickness;owned.reference.surface.reset();owned.reference.display_owner_id.clear();
        }
        for(auto& owned:piece.edges) {owned.reference.semantic_key=parent_key("corner-join",request.definition_id,owned.reference);owned.reference.owner_id=operation.owner_id;}
        for(auto& owned:piece.vertices) {owned.reference.semantic_key=parent_key("corner-join",request.definition_id,owned.reference);owned.reference.owner_id=operation.owner_id;}
        result=combine(result,piece,false,operation,history);
    }
    return result;
}
} // namespace sheet_form_geometry
