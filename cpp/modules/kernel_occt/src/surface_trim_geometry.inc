// Native parents define every trim child. Splitter history locates their
// shapes; coordinates and traversal positions never define identities.
PrimitiveData make_trimmed_surface_data(const SurfaceTrimRequest& request,const std::string& owner,
    const TopoDS_Shape& input_shape,const std::vector<OwnedFace>& input_faces,
    const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices,
    const std::vector<OwnedFace>& original_faces,const std::vector<OwnedEdge>& original_edges,
    const std::vector<OwnedVertex>& original_vertices) {
    if(!std::isfinite(request.tolerance)||request.tolerance<=0||request.tools.empty()||
        !request.target.valid()||!request.target.instance_path.empty()||
        !std::isfinite(request.seed.x)||!std::isfinite(request.seed.y)||!std::isfinite(request.seed.z))
        throw std::runtime_error("Specify a surface, cutting tools and a retained region.");
    std::optional<OwnedFace> target;
    for(const auto& face:input_faces)if(same_face_identity(face.reference,request.target)) {
        if(target&&!target->shape.IsSame(face.shape))throw std::runtime_error("Surface trim target is ambiguous.");
        target=face;
    }
    if(!target||!target->reference.surface_result)throw std::runtime_error("Trim requires a calculated surface at its input boundary.");
    const auto target_face=TopoDS::Face(target->shape);
    std::vector<OwnedEdge> sources;std::vector<OwnedVertex> source_points;
    // Internal Section curves only locate cutting geometry. Persisted trim
    // children refer directly to their real input faces, never to a temporary
    // Section owner or to a runtime edge that has no native document object.
    std::map<std::string,std::set<std::string>> cutting_face_parents;
    const auto parents_of=[&](const OwnedEdge& edge) {
        const auto key=encoded_topology_reference(edge.reference);
        const auto found=cutting_face_parents.find(key);
        return found==cutting_face_parents.end()?std::set<std::string>{"edge:"+key}:found->second;
    };
    TopTools_IndexedMapOfShape target_edges,target_vertices;
    TopExp::MapShapes(target_face,TopAbs_EDGE,target_edges);TopExp::MapShapes(target_face,TopAbs_VERTEX,target_vertices);
    for(const auto& edge:input_edges)if(target_edges.Contains(edge.shape))sources.push_back(edge);
    for(const auto& point:input_vertices)if(target_vertices.Contains(point.shape))source_points.push_back(point);
    TopTools_ListOfShape cutters;
    std::set<std::pair<bool,std::string>> selected_tools;
    for(const auto& tool:request.tools) {
        if(!tool.reference.valid()||!tool.reference.instance_path.empty()||
            !selected_tools.emplace(tool.face,encoded_topology_reference(tool.reference)).second||
            (tool.face&&tool.reference.owner_id==request.target.owner_id&&tool.reference.semantic_key==request.target.semantic_key))
            throw std::runtime_error("Surface trim tool reference is invalid.");
        if(tool.face) {
            SurfaceIntersectionRequest intersection;
            intersection.faces={request.target,FaceReference{tool.reference.owner_id,tool.reference.semantic_key,{}}};
            intersection.tolerance=request.tolerance;
            auto faces=original_faces;faces.push_back(*target);
            auto edges=original_edges;edges.insert(edges.end(),sources.begin(),sources.end());
            auto points=original_vertices;points.insert(points.end(),source_points.begin(),source_points.end());
            auto cut=make_surface_intersection_data(intersection,owner+":cut",faces,edges,points);
            if(cut.edges.empty())throw std::runtime_error("Cutting tools do not divide the surface.");
            for(const auto& edge:cut.edges) {
                cutting_face_parents[encoded_topology_reference(edge.reference)]={
                    "face:"+encoded_topology_reference(request.target),"face:"+encoded_topology_reference(tool.reference)};
                cutters.Append(edge.shape);sources.push_back(edge);
            }
        }else {
            std::optional<OwnedEdge> found;
            for(const auto& edge:original_edges)if(edge.reference==tool.reference) {
                if(found&&!found->shape.IsSame(edge.shape))throw std::runtime_error("Surface trim tool reference is ambiguous.");
                found=edge;
            }
            if(!found)throw std::runtime_error("Surface trim tool is missing at its input boundary.");
            // A curve tool must already lie on the bounded target. Do not
            // silently project it or extend either input.
            BRepAlgoAPI_Common on_face(found->shape,target_face);on_face.SetNonDestructive(true);
            on_face.SetFuzzyValue(request.tolerance);on_face.Build();
            GProp_GProps whole,common;BRepGProp::LinearProperties(found->shape,whole);
            if(!on_face.IsDone())throw std::runtime_error("Surface trim calculation failed.");
            BRepGProp::LinearProperties(on_face.Shape(),common);
            if(std::abs(whole.Mass()-common.Mass())>request.tolerance)
                throw std::runtime_error("A cutting curve must lie on the bounded target surface.");
            cutters.Append(found->shape);sources.push_back(*found);
            TopTools_IndexedMapOfShape points;TopExp::MapShapes(found->shape,TopAbs_VERTEX,points);
            for(const auto& point:original_vertices)if(points.Contains(point.shape))source_points.push_back(point);
        }
    }
    BRepAlgoAPI_Splitter split;TopTools_ListOfShape arguments;arguments.Append(target_face);
    split.SetArguments(arguments);split.SetTools(cutters);split.SetNonDestructive(true);
    split.SetFuzzyValue(request.tolerance);split.Build();
    if(!split.IsDone())throw std::runtime_error("Surface trim calculation failed.");
    TopTools_IndexedMapOfShape regions;TopExp::MapShapes(split.Shape(),TopAbs_FACE,regions);
    if(regions.Extent()<2)throw std::runtime_error("Cutting tools do not divide the surface.");
    const auto region_data=[&](TopoDS_Face retained) {
    retained.Orientation(target_face.Orientation());
    if(!BRepCheck_Analyzer(retained).IsValid())throw std::runtime_error("Surface trim calculation failed.");
    PrimitiveData result;result.shape=retained;
    const auto ancestry=[](const std::set<std::string>& parents) {
        std::string key;for(const auto& parent:parents)key+=std::to_string(parent.size())+":"+parent;return key;
    };
    TopTools_IndexedMapOfShape edges,vertices;
    TopExp::MapShapes(retained,TopAbs_EDGE,edges);TopExp::MapShapes(retained,TopAbs_VERTEX,vertices);
    std::vector<std::set<std::string>> edge_parents(edges.Extent());
    for(const auto& source:sources) {
        const auto add=[&](const TopoDS_Shape& shape) {
            const int index=edges.FindIndex(shape);if(index){const auto parents=parents_of(source);edge_parents[index-1].insert(parents.begin(),parents.end());}
        };
        add(source.shape);
        for(TopTools_ListIteratorOfListOfShape it(split.Modified(source.shape));it.More();it.Next())add(it.Value());
        for(TopTools_ListIteratorOfListOfShape it(split.Generated(source.shape));it.More();it.Next())add(it.Value());
    }
    for(const auto& parents:edge_parents)if(parents.empty())throw std::runtime_error("Surface trim could not resolve boundary ancestry.");
    std::set<std::string> vertex_keys;
    for(int i=1;i<=vertices.Extent();++i) {
        const auto vertex=TopoDS::Vertex(vertices.FindKey(i));const auto point=BRep_Tool::Pnt(vertex);
        std::set<std::string> parents;
        for(const auto& source:source_points)if(BRep_Tool::Pnt(TopoDS::Vertex(source.shape)).Distance(point)<=request.tolerance)
            parents.insert("point:"+encoded_topology_reference(source.reference));
        for(const auto& source:sources) {
            BRepExtrema_DistShapeShape distance(vertex,source.shape);
            if(distance.IsDone()&&distance.Value()<=request.tolerance){const auto inherited=parents_of(source);parents.insert(inherited.begin(),inherited.end());}
        }
        if(parents.empty())throw std::runtime_error("Surface trim could not resolve endpoint ancestry.");
        const auto key="trim:vertex:from:"+ancestry(parents);
        if(!vertex_keys.insert(key).second)throw std::runtime_error("Surface trim endpoint ancestry is ambiguous.");
        result.vertices.push_back({vertex,{owner,key,{}}});
    }
    const auto endpoint=[&](const TopoDS_Vertex& vertex) {
        const auto found=std::ranges::find_if(result.vertices,[&](const auto& point){return point.shape.IsSame(vertex);});
        if(found==result.vertices.end())throw std::runtime_error("Surface trim could not resolve endpoint ancestry.");
        return encoded_topology_reference(found->reference);
    };
    std::set<std::string> edge_keys;
    for(int i=1;i<=edges.Extent();++i) {
        const auto edge=TopoDS::Edge(edges.FindKey(i));
        const auto key="trim:edge:from:"+ancestry(edge_parents[i-1])+":at:"+
            ancestry({endpoint(TopExp::FirstVertex(edge,true)),endpoint(TopExp::LastVertex(edge,true))});
        if(!edge_keys.insert(key).second)throw std::runtime_error("Surface trim boundary ancestry is ambiguous.");
        result.edges.push_back({edge,{owner,key,{}}});
    }
    const auto region_key="trim:face:from:"+encoded_topology_reference(request.target)+":bounded:"+ancestry(edge_keys);
    auto reference=target->reference;reference.owner_id=owner;reference.instance_path.clear();reference.semantic_key=region_key;
    result.faces.push_back({retained,std::move(reference)});
    return result;
    };
    std::optional<PrimitiveData> selected;
    if(!request.expected_region_key.empty()) {
        // Regeneration follows the persisted native region, even if its
        // source has moved away from the original viewer click. A vanished
        // boundary signature must never fall back to another fragment.
        for(int i=1;i<=regions.Extent();++i) {
            auto region=region_data(TopoDS::Face(regions.FindKey(i)));
            if(region.faces.front().reference.semantic_key!=request.expected_region_key)continue;
            if(selected)throw std::runtime_error("Surface trim retained region is ambiguous.");
            selected=std::move(region);
        }
        if(!selected)throw std::runtime_error("The retained trim region changed. Select the region again.");
    }else {
        const gp_Pnt authored(request.seed.x,request.seed.y,request.seed.z);
        GeomAPI_ProjectPointOnSurf projection(authored,BRep_Tool::Surface(target_face));
        if(projection.NbPoints()==0)throw std::runtime_error("Select a retained region away from its boundary.");
        double u,v;projection.LowerDistanceParameters(u,v);
        TopoDS_Face retained;
        for(int i=1;i<=regions.Extent();++i) {
            const auto face=TopoDS::Face(regions.FindKey(i));BRepClass_FaceClassifier classify(face,gp_Pnt2d(u,v),request.tolerance);
            if(classify.State()==TopAbs_ON)throw std::runtime_error("Select a retained region away from its boundary.");
            if(classify.State()==TopAbs_IN) {
                if(!retained.IsNull())throw std::runtime_error("Surface trim retained region is ambiguous.");
                retained=face;
            }
        }
        if(retained.IsNull())throw std::runtime_error("Select a retained region away from its boundary.");
        selected=region_data(retained);
    }
    auto result=std::move(*selected);const auto retained=result.shape;
    // Replace only the selected face, preserving unrelated solid operands and
    // all real unselected faces of a partially affected shell.
    BRep_Builder builder;TopoDS_Compound combined;builder.MakeCompound(combined);builder.Add(combined,retained);
    const auto retain=[&](auto&& self,const TopoDS_Shape& shape)->void {
        if(shape.ShapeType()==TopAbs_COMPOUND){for(TopoDS_Iterator it(shape);it.More();it.Next())self(self,it.Value());return;}
        bool affected=shape.IsSame(target_face);
        if(!affected)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())if(it.Current().IsSame(target_face)){affected=true;break;}
        if(!affected){builder.Add(combined,shape);return;}
        if(shape.ShapeType()==TopAbs_SOLID)throw std::runtime_error("Trim requires a calculated surface at its input boundary.");
        if(shape.ShapeType()!=TopAbs_FACE)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
            if(!it.Current().IsSame(target_face))builder.Add(combined,it.Current());
    };
    retain(retain,input_shape);result.shape=combined;
    for(const auto& face:input_faces)if(!face.shape.IsSame(target_face))result.faces.push_back(face);
    TopTools_IndexedMapOfShape retained_edges,retained_vertices;
    for(const auto& face:result.faces)if(face.reference.owner_id!=owner) {
        TopExp::MapShapes(face.shape,TopAbs_EDGE,retained_edges);TopExp::MapShapes(face.shape,TopAbs_VERTEX,retained_vertices);
    }
    for(const auto& edge:input_edges)if(retained_edges.Contains(edge.shape))result.edges.push_back(edge);
    for(const auto& vertex:input_vertices)if(retained_vertices.Contains(vertex.shape))result.vertices.push_back(vertex);
    return result;
}
