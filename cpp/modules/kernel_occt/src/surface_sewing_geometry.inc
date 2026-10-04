// Explicit surface calculation. Child names are functions of native input
// parents and authored endpoint parents; OCCT history only locates descendants.
PrimitiveData make_sewn_surface_data(const SurfaceSewingRequest& request,const std::string& owner,
    const TopoDS_Shape& input_shape,const std::vector<OwnedFace>& input_faces,
    const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices) {
    if(request.faces.size()<2||!std::isfinite(request.tolerance)||request.tolerance<=0||input_shape.IsNull())
        throw std::runtime_error("Sewing requires at least two surfaces and a positive tolerance.");
    std::vector<OwnedFace> selected;std::set<std::string> identities;
    TopTools_IndexedMapOfShape selected_faces,selected_edges,selected_vertices;
    for(const auto& reference:request.faces) {
        if(!reference.valid()||!reference.instance_path.empty()||!identities.insert(encoded_topology_reference(reference)).second)
            throw std::runtime_error("Sewing surface reference is invalid.");
        std::optional<OwnedFace> match;
        for(const auto& face:input_faces)if(same_face_identity(face.reference,reference)) {
            if(match&&!match->shape.IsSame(face.shape))throw std::runtime_error("Sewing surface reference is ambiguous.");
            match=face;
        }
        if(!match||!match->reference.surface_result)throw std::runtime_error("Sewing requires calculated surfaces at its input boundary.");
        if(selected_faces.Contains(match->shape))throw std::runtime_error("Sewing requires different surfaces.");
        selected_faces.Add(match->shape);selected.push_back(*match);
        TopExp::MapShapes(match->shape,TopAbs_EDGE,selected_edges);
        TopExp::MapShapes(match->shape,TopAbs_VERTEX,selected_vertices);
    }
    BRep_Builder builder;TopoDS_Compound sources;builder.MakeCompound(sources);
    for(const auto& face:selected)builder.Add(sources,face.shape);
    BRepBuilderAPI_Copy copy(sources,true,false);
    std::vector<OwnedEdge> source_edges;std::vector<OwnedVertex> source_vertices;
    for(const auto& edge:input_edges)if(selected_edges.Contains(edge.shape))source_edges.push_back({copy.ModifiedShape(edge.shape),edge.reference});
    for(const auto& vertex:input_vertices)if(selected_vertices.Contains(vertex.shape))source_vertices.push_back({copy.ModifiedShape(vertex.shape),vertex.reference});
    BRepBuilderAPI_Sewing sewing(request.tolerance,true,true,true,false);
    for(const auto& face:selected)sewing.Add(copy.ModifiedShape(face.shape));
    sewing.Perform();const auto sewn=sewing.SewedShape();
    if(sewn.IsNull()||sewn.ShapeType()!=TopAbs_SHELL||sewing.NbContigousEdges()==0||sewing.NbMultipleEdges()!=0||
        !BRepCheck_Analyzer(sewn).IsValid())
        throw std::runtime_error("Sewing must produce one connected manifold shell.");
    TopTools_IndexedMapOfShape result_faces,result_edges,result_vertices;
    TopExp::MapShapes(sewn,TopAbs_FACE,result_faces);TopExp::MapShapes(sewn,TopAbs_EDGE,result_edges);TopExp::MapShapes(sewn,TopAbs_VERTEX,result_vertices);
    if(result_faces.Extent()!=static_cast<int>(selected.size()))throw std::runtime_error("Sewing did not preserve its source surfaces.");
    PrimitiveData result;result.shape=sewn;
    const auto ancestry=[&](const std::set<std::string>& parents) {
        auto text=encoded_topology_ancestry(parents)+":parents:";
        for(const auto& parent:parents)text+=std::to_string(parent.size())+":"+parent;
        return text;
    };
    const auto modified=[&](const TopoDS_Shape& source) {
        return sewing.IsModifiedSubShape(source)?sewing.ModifiedSubShape(source):sewing.IsModified(source)?sewing.Modified(source):source;
    };
    for(const auto& face:selected) {
        const auto shape=modified(copy.ModifiedShape(face.shape));
        if(shape.ShapeType()!=TopAbs_FACE||!result_faces.Contains(shape))throw std::runtime_error("Sewing did not preserve its source surfaces.");
        auto reference=face.reference;reference.owner_id=owner;reference.instance_path.clear();
        reference.semantic_key="sewn:face:from:"+encoded_topology_reference(face.reference);
        result.faces.push_back({shape,std::move(reference)});
    }
    // A sewing corner is the canonical set of input points meeting there.
    // Coordinates locate that set but never appear in the persistent identity.
    using Cell=std::tuple<double,double,double>;
    const auto cell=[&](const gp_Pnt& point) {return Cell{std::floor(point.X()/request.tolerance),std::floor(point.Y()/request.tolerance),std::floor(point.Z()/request.tolerance)};};
    std::map<Cell,std::vector<const OwnedVertex*>> point_cells;
    for(const auto& source:source_vertices)point_cells[cell(BRep_Tool::Pnt(TopoDS::Vertex(source.shape)))].push_back(&source);
    std::map<std::string,TopoDS_Shape> vertex_identities;
    for(int i=1;i<=result_vertices.Extent();++i) {
        const auto vertex=TopoDS::Vertex(result_vertices.FindKey(i));const auto point=BRep_Tool::Pnt(vertex);
        std::set<std::string> parents;
        const auto [x,y,z]=cell(point);
        for(int dx=-1;dx<=1;++dx)for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz) {
            const auto found=point_cells.find({x+dx,y+dy,z+dz});if(found==point_cells.end())continue;
            for(const auto* source:found->second)if(BRep_Tool::Pnt(TopoDS::Vertex(source->shape)).Distance(point)<=request.tolerance)
                parents.insert(encoded_topology_reference(source->reference));
        }
        if(parents.empty())throw std::runtime_error("Sewing could not resolve endpoint ancestry.");
        const auto key="sewn:vertex:from:"+ancestry(parents);
        if(!vertex_identities.emplace(key,vertex).second)throw std::runtime_error("Sewing endpoint ancestry is ambiguous.");
        result.vertices.push_back({vertex,{owner,key,{}}});
    }
    const auto endpoint_key=[&](const TopoDS_Vertex& vertex) {
        const auto found=std::ranges::find_if(result.vertices,[&](const auto& item){return item.shape.IsSame(vertex);});
        if(found==result.vertices.end())throw std::runtime_error("Sewing could not resolve endpoint ancestry.");
        return encoded_topology_reference(found->reference);
    };
    std::map<std::string,TopoDS_Shape> edge_identities;
    std::vector<std::set<std::string>> edge_parents(result_edges.Extent());
    TopTools_IndexedMapOfShape source_edge_shapes;std::vector<std::set<std::string>> source_edge_parents;
    for(const auto& source:source_edges) {
        const auto index=source_edge_shapes.Add(source.shape);if(index>static_cast<int>(source_edge_parents.size()))source_edge_parents.emplace_back();
        source_edge_parents[index-1].insert(encoded_topology_reference(source.reference));
        const auto descendant=modified(source.shape);
        const auto add=[&](const TopoDS_Shape& shape) {const auto index=result_edges.FindIndex(shape);if(index>0)edge_parents[index-1].insert(encoded_topology_reference(source.reference));};
        if(descendant.ShapeType()==TopAbs_EDGE)add(descendant);
        else for(TopExp_Explorer it(descendant,TopAbs_EDGE);it.More();it.Next())add(it.Current());
    }
    // Sewing may retain only the chosen boundary in ModifiedSubShape. Its
    // explicit contiguous-edge couples carry both native parents of a join.
    for(int i=1;i<=sewing.NbContigousEdges();++i) {
        const auto index=result_edges.FindIndex(sewing.ContigousEdge(i));if(index==0)continue;
        for(TopTools_ListIteratorOfListOfShape it(sewing.ContigousEdgeCouple(i));it.More();it.Next()) {
            const TopoDS_Shape boundary=sewing.IsSectionBound(TopoDS::Edge(it.Value()))?TopoDS_Shape(sewing.SectionToBoundary(TopoDS::Edge(it.Value()))):it.Value();
            const auto source_index=source_edge_shapes.FindIndex(boundary);
            if(source_index>0)edge_parents[index-1].insert(source_edge_parents[source_index-1].begin(),source_edge_parents[source_index-1].end());
        }
    }
    for(int i=1;i<=result_edges.Extent();++i) {
        const auto edge=TopoDS::Edge(result_edges.FindKey(i));const auto& parents=edge_parents[i-1];
        if(parents.empty())throw std::runtime_error("Sewing could not resolve boundary ancestry.");
        std::set<std::string> endpoints{endpoint_key(TopExp::FirstVertex(edge,true)),endpoint_key(TopExp::LastVertex(edge,true))};
        const auto key="sewn:edge:from:"+ancestry(parents)+":at:"+ancestry(endpoints);
        if(!edge_identities.emplace(key,edge).second)throw std::runtime_error("Sewing boundary ancestry is ambiguous.");
        result.edges.push_back({edge,{owner,key,{}}});
    }
    // Replace only selected surface faces. Unaffected solids and shells keep
    // their existing objects; a partially selected shell leaves its real faces.
    TopoDS_Compound combined;builder.MakeCompound(combined);builder.Add(combined,sewn);
    const auto retain=[&](auto&& self,const TopoDS_Shape& shape)->void {
        if(shape.ShapeType()==TopAbs_COMPOUND) {
            for(TopoDS_Iterator it(shape);it.More();it.Next())self(self,it.Value());return;
        }
        bool affected=false;
        if(shape.ShapeType()==TopAbs_FACE)affected=selected_faces.Contains(shape);
        else for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())if(selected_faces.Contains(it.Current())){affected=true;break;}
        if(!affected){builder.Add(combined,shape);return;}
        if(shape.ShapeType()==TopAbs_SOLID)throw std::runtime_error("Sewing requires calculated surfaces at its input boundary.");
        if(shape.ShapeType()!=TopAbs_FACE)for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
            if(!selected_faces.Contains(it.Current()))builder.Add(combined,it.Current());
    };
    retain(retain,input_shape);result.shape=combined;
    for(const auto& face:input_faces)if(!selected_faces.Contains(face.shape))result.faces.push_back(face);
    TopTools_IndexedMapOfShape retained_edges,retained_vertices;
    for(const auto& face:result.faces)if(face.reference.owner_id!=owner) {
        TopExp::MapShapes(face.shape,TopAbs_EDGE,retained_edges);TopExp::MapShapes(face.shape,TopAbs_VERTEX,retained_vertices);
    }
    for(const auto& edge:input_edges)if(retained_edges.Contains(edge.shape))result.edges.push_back(edge);
    for(const auto& vertex:input_vertices)if(retained_vertices.Contains(vertex.shape))result.vertices.push_back(vertex);
    if(!BRepCheck_Analyzer(result.shape).IsValid())throw std::runtime_error("Sewing must produce one connected manifold shell.");
    return result;
}
