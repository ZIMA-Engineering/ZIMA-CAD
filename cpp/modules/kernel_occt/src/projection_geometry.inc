// Shared exact stage for all Projection outputs. This runs only during an
// explicit history calculation and consumes the actual bounded input faces.
PrimitiveData make_projection_data(const ProjectionRequest& request,const std::string& owner,
        const TopoDS_Shape& input,const std::vector<OwnedFace>& input_faces,
        const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices,
        const std::vector<OwnedFace>& available_faces,const std::vector<OwnedEdge>& available_edges,
        const std::vector<OwnedVertex>& available_vertices,
        const std::vector<HistoryOperation>& operations,
        std::vector<ResolvedProjectionRegion>& resolved_regions) {
    if(request.targets.empty()||request.sketch_id.empty())
        throw std::invalid_argument("Projection requires source curves, target faces and a positive tolerance.");
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    std::vector<detail::ProjectionTarget> targets;
    for(const auto& selected:request.targets) {
        const OwnedFace* found=nullptr;
        for(const auto& face:available_faces)if(same_face_identity(face.reference,selected)) {
            if(found&&!found->shape.IsSame(face.shape))throw std::runtime_error("Projection curve ancestry is ambiguous.");
            found=&face;
        }
        if(!found)throw std::runtime_error("Projection source ancestry is missing.");
        detail::ProjectionTarget target{TopoDS::Face(found->shape),encoded_topology_reference(selected)};
        for(TopExp_Explorer it(found->shape,TopAbs_EDGE);it.More();it.Next()) {
            const auto edge=TopoDS::Edge(it.Current());
            if(BRep_Tool::Degenerated(edge))continue;
            auto boundary=std::ranges::find_if(available_edges,[&](const auto& source){return source.shape.IsSame(edge);});
            if(boundary!=available_edges.end())target.boundaries.push_back({edge,encoded_topology_reference(boundary->reference)});
            else if(BRep_Tool::IsClosed(edge,target.face))target.boundaries.push_back({edge,"parameter-seam:"+target.target_key});
            else throw std::runtime_error("Projection boundary ancestry is missing.");
        }
        targets.push_back(std::move(target));
    }
    std::vector<detail::ProjectionCurve> sources;
    const auto append_profile=[&](const ExtrusionRequest& profile) {
        for(const auto& wire:make_body_profiles(profile,request.sketch_normal)) {
            if(wire.edges.size()!=wire.curve_ids.size()||(!wire.point_ids.empty()&&wire.point_ids.size()!=wire.edges.size()))
                throw std::runtime_error("Projection source ancestry is missing.");
            const auto point_key=[&](const std::string& id){return id.empty()?std::string{}:
                "sketch:"+field(request.sketch_id)+":point:"+field(id);};
            for(std::size_t i=0;i<wire.edges.size();++i) {
                detail::ProjectionCurve source{wire.edges[i],"sketch:"+field(request.sketch_id)+":curve:"+field(wire.curve_ids[i])};
                if(!wire.point_ids.empty()) {
                    source.first_point_key=point_key(wire.point_ids[i]);
                    source.last_point_key=point_key(i+1<wire.point_ids.size()?wire.point_ids[i+1]:
                        !profile.open_profile_end_id.empty()?profile.open_profile_end_id:wire.point_ids.front());
                }
                sources.push_back(std::move(source));
            }
        }
    };
    for(const auto& source:request.profiles) {
        append_profile(source);
        for(const auto& region:source.additional_profile_regions) {
            auto profile=source;profile.additional_profile_regions.clear();
            profile.outer_profile=region.outer_profile;profile.inner_profiles=region.inner_profiles;
            profile.outer_edge_source_ids=region.outer_edge_source_ids;profile.inner_edge_source_ids=region.inner_edge_source_ids;
            profile.outer_vertex_source_ids=region.outer_vertex_source_ids;profile.inner_vertex_source_ids=region.inner_vertex_source_ids;
            append_profile(profile);
        }
    }
    const gp_Pln sketch(gp_Pnt(request.sketch_origin.x,request.sketch_origin.y,request.sketch_origin.z),
        gp_Dir(request.sketch_normal.x,request.sketch_normal.y,request.sketch_normal.z));
    const gp_Dir direction(request.direction.x,request.direction.y,request.direction.z);
    auto projection=detail::project_directionally(sources,targets,sketch,direction,request.tolerance,request.both_directions);
    if(projection.ambiguous)throw std::runtime_error("Projection curve ancestry is ambiguous.");
    if(!projection.gaps.empty())throw std::runtime_error("Projection does not completely define a partition.");
    detail::name_projected_curves(projection,sources,targets,sketch,direction,request.tolerance);
    PrimitiveData curves;BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);
    for(const auto& projected:projection.curves) {
        builder.Add(compound,projected.edge);
        curves.edges.push_back({projected.edge,{owner,projected.semantic_key,{}}});
        if(!projected.first_point_key.empty())curves.vertices.push_back({TopExp::FirstVertex(projected.edge,true),{owner,projected.first_point_key,{}}});
        if(!projected.last_point_key.empty())curves.vertices.push_back({TopExp::LastVertex(projected.edge,true),{owner,projected.last_point_key,{}}});
    }
    if(request.output==ProjectionResult::Curves&&!request.display_points.empty()) {
        std::vector<std::unique_ptr<IntCurvesFace_ShapeIntersector>> rays;
        for(const auto& target:targets) {
            auto ray=std::make_unique<IntCurvesFace_ShapeIntersector>();
            ray->Load(target.face,request.tolerance);
            rays.push_back(std::move(ray));
        }
        for(std::size_t index=0;index<request.display_points.size();++index) {
            const auto& source=request.display_points[index];
            const gp_Pnt origin(source.x,source.y,source.z);
            double distance=std::numeric_limits<double>::infinity(),depth{};
            bool ambiguous=false;
            for(const auto& ray:rays) {
                ray->Perform(gp_Lin(origin,direction),request.both_directions?-Precision::Infinite():-request.tolerance,Precision::Infinite());
                if(!ray->IsDone())continue;
                for(int i=1;i<=ray->NbPnt();++i) {
                    const double candidate=ray->WParameter(i);
                    const double absolute=std::abs(candidate);
                    if(absolute<distance-request.tolerance){distance=absolute;depth=candidate;ambiguous=false;}
                    else if(std::abs(absolute-distance)<=request.tolerance&&std::abs(depth-candidate)>request.tolerance)ambiguous=true;
                }
            }
            if(ambiguous||!std::isfinite(distance))continue;
            const auto point=origin.Translated(depth*gp_Vec(direction));
            if(std::ranges::any_of(curves.vertices,[&](const auto& vertex){
                return BRep_Tool::Pnt(TopoDS::Vertex(vertex.shape)).Distance(point)<=request.tolerance;
            }))continue;
            // The key identifies a viewport marker only. It has no BRep
            // vertex, and the picker excludes this presentation role.
            ViewerPoint marker{{point.X(),point.Y(),point.Z()},
                {owner,"projection:display-point:"+std::to_string(index),{}}};
            marker.display_owner_id=owner;
            curves.profile_references.points.push_back(std::move(marker));
        }
    }
    curves.shape=compound;
    if(request.output==ProjectionResult::Curves)return curves;
    const auto partition=detail::partition_projection(request.output==ProjectionResult::Split?input:TopoDS_Shape{},targets,projection,request.tolerance);
    std::vector<OwnedFace> parents;
    if(request.output==ProjectionResult::Split)parents=input_faces;
    else for(const auto& face:available_faces)if(std::ranges::any_of(targets,[&](const auto& target){return target.face.IsSame(face.shape);}))parents.push_back(face);
    auto original_edges=available_edges;original_edges.insert(original_edges.end(),curves.edges.begin(),curves.edges.end());
    auto original_vertices=available_vertices;original_vertices.insert(original_vertices.end(),curves.vertices.begin(),curves.vertices.end());
    auto faces=propagate_topology(partition.history,parents);
    auto edges=propagate_topology(partition.history,original_edges);
    auto vertices=propagate_topology(partition.history,original_vertices);
    vertices=complete_boolean_vertices(partition.shape,faces,edges,vertices,owner,"projection");
    faces=complete_boolean_faces(partition.shape,faces,edges,vertices,owner,"projection",operations);
    edges=complete_boolean_edges(partition.shape,faces,edges,owner,"projection",{},vertices);
    if(request.output==ProjectionResult::Split) {
        PrimitiveData result;result.shape=partition.shape;result.faces=std::move(faces);
        result.edges=std::move(edges);result.vertices=std::move(vertices);return result;
    }
    if(request.regions.empty())throw std::invalid_argument("Specify a surface, cutting tools and a retained region.");
    std::vector<OwnedFace> retained;
    for(std::size_t intent_index=0;intent_index<request.regions.size();++intent_index) {
        const auto& intent=request.regions[intent_index];
        const auto target=std::ranges::find_if(targets,[&](const auto& value){return value.target_key==encoded_topology_reference(intent.target);});
        if(target==targets.end())throw std::runtime_error("Projection source ancestry is missing.");
        std::optional<OwnedFace> selected;
        for(const auto& region:partition.regions) {
            if(region.target_key!=target->target_key)continue;
            const auto named=std::ranges::find_if(faces,[&](const auto& face){return face.shape.IsSame(region.face);});
            if(named==faces.end())throw std::runtime_error("Projection boundary ancestry is missing.");
            bool match=false;
            if(!intent.expected_region_key.empty())match=named->reference.semantic_key==intent.expected_region_key;
            else {
                const gp_Pnt seed(intent.seed.x,intent.seed.y,intent.seed.z);
                BRepClass_FaceClassifier classify(region.face,seed,request.tolerance);
                if(classify.State()==TopAbs_ON)throw std::runtime_error("Select a retained region away from its boundary.");
                match=classify.State()==TopAbs_IN;
            }
            if(match) {
                if(selected)throw std::runtime_error("Surface trim retained region is ambiguous.");
                selected=*named;
            }
        }
        if(!selected)throw std::runtime_error("The retained trim region changed. Select the region again.");
        resolved_regions.push_back({owner,intent_index,selected->reference.semantic_key});
        if(std::ranges::none_of(retained,[&](const auto& face){return face.shape.IsSame(selected->shape);}))retained.push_back(*selected);
    }
    PrimitiveData result;TopoDS_Compound combined;builder.MakeCompound(combined);
    if(!input.IsNull())builder.Add(combined,input);
    result.faces=input_faces;result.edges=input_edges;result.vertices=input_vertices;
    // Copy the selected region set together. Adjacent selected regions share
    // one native boundary; separate copies would give the same reference key
    // to two different edges and break downstream reference resolution.
    TopoDS_Compound selected_shape;builder.MakeCompound(selected_shape);
    for(const auto& face:retained)builder.Add(selected_shape,face.shape);
    BRepBuilderAPI_Copy copy(selected_shape,true,false);builder.Add(combined,copy.Shape());
    for(const auto& face:retained) {
        auto reference=face.reference;reference.owner_id=owner;reference.instance_path.clear();reference.surface_result=true;
        result.faces.push_back({copy.ModifiedShape(face.shape),reference});
    }
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> topology;TopExp::MapShapes(selected_shape,topology);
    for(const auto& edge:edges)if(topology.Contains(edge.shape))result.edges.push_back({copy.ModifiedShape(edge.shape),
        {owner,"projection:surface-edge:from:"+field(encoded_topology_reference(edge.reference)),{}}});
    for(const auto& point:vertices)if(topology.Contains(point.shape))result.vertices.push_back({copy.ModifiedShape(point.shape),
        {owner,"projection:surface-point:from:"+field(encoded_topology_reference(point.reference)),{}}});
    result.shape=combined;return result;
}
