// Explicit calculation of copied native FORM histories. No UI or picker calls
// this implementation, and no OCCT enumeration defines a persistent identity.
namespace sheet_form_geometry {
struct Operands {PrimitiveData cut,flat,formed;};
struct Instance {SheetMaterialDefinition material;Operands operands;};

PrimitiveData copy(const PrimitiveData& source,const gp_Trsf& transform,const HistoryOperation& operation,
        const std::string& region,unsigned index) {
    if(source.shape.IsNull())return {};
    BRepBuilderAPI_Transform moved(source.shape,transform,true);moved.Build();
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

PrimitiveData read_surface(const BodyResult& snapshot) {
    if(snapshot.kernel_shape.empty()||snapshot.kernel_faces.empty()||snapshot.volume!=0.||
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
            if(face.reference.semantic_key.starts_with("thicken:cap:start:from:"))ref.sheet_role=SheetFaceRole::SideA;
            if(face.reference.semantic_key.starts_with("thicken:cap:end:from:"))ref.sheet_role=SheetFaceRole::SideB;
        }
        result.faces.push_back({moved.ModifiedShape(face.shape),std::move(ref)});
    }
    for(const auto& edge:source.edges)result.edges.push_back({moved.ModifiedShape(edge.shape),reference(edge.reference)});
    for(const auto& vertex:source.vertices)result.vertices.push_back({moved.ModifiedShape(vertex.shape),reference(vertex.reference)});
    return result;
}

void validate_support(const SheetFormRequest& request,const TopoDS_Shape& input,
        const std::vector<OwnedFace>& faces) {
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
        if(classifier.State()==TopAbs_IN||classifier.State()==TopAbs_ON)return;
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
} // namespace sheet_form_geometry
