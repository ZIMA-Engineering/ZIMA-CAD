void distinguish_partition_sides(std::vector<OwnedFace>& faces,const std::vector<OwnedEdge>& edges) {
    std::map<std::string,unsigned> identities;
    for(const auto& face:faces)++identities[encoded_topology_reference(face.reference)];
    for(auto& face:faces)if(identities[encoded_topology_reference(face.reference)]>1) {
        // On a closed support the inside patch and its complement
        // can have identical boundary parents. Their opposite
        // uses of those authored edges distinguish the two regions
        // without coordinates or enumeration-based identities.
        std::set<std::string> sides;
        for(TopExp_Explorer edge(face.shape,TopAbs_EDGE);edge.More();edge.Next())
            for(const auto& parent:edges)if(parent.reference.valid()&&parent.shape.IsSame(edge.Current()))
                sides.insert(encoded_topology_reference(parent.reference)+
                    (parent.shape.Orientation()==edge.Current().Orientation()?":forward":":reverse"));
        face.reference.semantic_key+=":boundary-sides:"+compact_topology_token(encoded_topology_reference_set(sides));
    }
    std::set<std::string> distinct;
    for(const auto& face:faces)if(!distinct.insert(encoded_topology_reference(face.reference)).second)
        throw std::runtime_error("Projection curve ancestry is ambiguous.");
}

PrimitiveData directional_relief(const TopoDS_Face& face,const FaceReference& parent,
        const std::vector<OwnedEdge>& source_edges,const std::vector<OwnedVertex>& source_vertices,
        const gp_Dir& direction,const ProjectionRequest& request,const std::string& owner) {
    Handle(IntTools_Context) context=new IntTools_Context;
    gp_Pnt point;gp_Pnt2d uv;
    if(BOPTools_AlgoTools3D::PointInFace(face,point,uv,context)!=0)
        throw std::runtime_error("Projection face partition failed.");
    gp_Vec du,dv;BRepAdaptor_Surface(face).D1(uv.X(),uv.Y(),point,du,dv);
    auto normal=du.Crossed(dv);if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    const double dot=normal.Dot(gp_Vec(direction));
    if(normal.SquareMagnitude()<=Precision::SquareConfusion()||std::abs(dot)<=normal.Magnitude()*Precision::Angular())
        throw std::runtime_error("Projection does not completely define a partition.");
    const double signed_depth=request.height*(request.inward?-1.:1.)*(dot<0?-1.:1.);
    BRepPrimAPI_MakePrism prism(face,gp_Vec(direction)*signed_depth,true,false);
    prism.Build();
    if(!prism.IsDone()||prism.Shape().ShapeType()!=TopAbs_SOLID)
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    auto solid=TopoDS::Solid(prism.Shape());
    if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid).IsValid()||!BRepAlgoAPI_Check(solid,false,true).IsValid())
        throw std::runtime_error("Surface thickening could not create a valid solid.");
    BRep_Builder builder;TopoDS_Solid forward;builder.MakeSolid(forward);
    for(TopoDS_Iterator child(solid);child.More();child.Next())builder.Add(forward,child.Value());
    PrimitiveData result;result.shape=forward;
    const auto key=[](std::string_view role,const auto& reference) {
        return "projection:linear:"+std::string(role)+":from:"+encoded_topology_reference(reference);
    };
    result.faces.push_back({prism.FirstShape(),{owner,key("cap:start",parent),{}}});
    result.faces.push_back({prism.LastShape(),{owner,key("cap:end",parent),{}}});
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> members;TopExp::MapShapes(face,members);
    for(const auto& edge:source_edges)if(members.Contains(edge.shape)) {
        for(const auto& wall:prism.Generated(edge.shape))if(wall.ShapeType()==TopAbs_FACE)
            result.faces.push_back({wall,{owner,key("wall",edge.reference),{}}});
        for(bool end:{false,true}) {
            const auto rim=end?prism.LastShape(edge.shape):prism.FirstShape(edge.shape);
            if(!rim.IsNull())result.edges.push_back({rim,{owner,key(end?"rim:end":"rim:start",edge.reference),{}}});
        }
    }
    for(const auto& vertex:source_vertices)if(members.Contains(vertex.shape)) {
        for(const auto& edge:prism.Generated(vertex.shape))if(edge.ShapeType()==TopAbs_EDGE)
            result.edges.push_back({edge,{owner,key("longitudinal",vertex.reference),{}}});
        for(bool end:{false,true}) {
            const auto point=end?prism.LastShape(vertex.shape):prism.FirstShape(vertex.shape);
            if(!point.IsNull())result.vertices.push_back({point,{owner,key(end?"point:end":"point:start",vertex.reference),{}}});
        }
    }
    require_complete_unique_shell_topology(result.shape,TopAbs_FACE,result.faces,"faces");
    require_complete_unique_shell_topology(result.shape,TopAbs_EDGE,result.edges,"edges");
    require_complete_unique_shell_topology(result.shape,TopAbs_VERTEX,result.vertices,"vertices");
    return result;
}

// Shared exact stage for all Projection outputs. This runs only during an
// explicit history calculation and consumes the actual bounded input faces.
PrimitiveData make_projection_data(const ProjectionRequest& request,const std::string& owner,
        const TopoDS_Shape& input,const std::vector<OwnedFace>& input_faces,
        const std::vector<OwnedEdge>& input_edges,const std::vector<OwnedVertex>& input_vertices,
        const std::vector<OwnedFace>& available_faces,const std::vector<OwnedEdge>& available_edges,
        const std::vector<OwnedVertex>& available_vertices,
        const std::vector<HistoryOperation>& operations,
        std::vector<ResolvedProjectionRegion>& resolved_regions,PrimitiveData& retained_geometry) {
    if(request.targets.empty()||request.sketch_id.empty())
        throw std::invalid_argument("Projection requires source curves, target faces and a positive tolerance.");
    if(request.output==ProjectionResult::Relief&&(!std::isfinite(request.height)||request.height<=0))
        throw std::invalid_argument("Surface thickness must be positive.");
    if(request.output==ProjectionResult::Surface&&request.regions.empty())
        throw std::invalid_argument("Select a retained region on the target face before confirming the projection.");
    const auto field=[](const std::string& value){return std::to_string(value.size())+":"+value;};
    std::vector<detail::ProjectionTarget> targets;
    const auto selected_faces=(request.output==ProjectionResult::Relief||request.output==ProjectionResult::Curves)?
        continued_native_surfaces(input_faces,request.targets,request.tolerance):request.targets;
    for(const auto& selected:selected_faces) {
        if(std::ranges::any_of(targets,[&](const auto& target){return target.target_key==encoded_topology_reference(selected);}))continue;
        const OwnedFace* found=nullptr;
        for(const auto& face:available_faces)if(same_face_identity(face.reference,selected)) {
            if(found&&!found->shape.IsSame(face.shape))throw std::runtime_error("Projection curve ancestry is ambiguous.");
            found=&face;
        }
        if(!found)throw std::runtime_error("Projection source ancestry is missing.");
        detail::ProjectionTarget target{TopoDS::Face(found->shape),encoded_topology_reference(selected)};
        if(request.output==ProjectionResult::Relief) {
            // The current solid, rather than its generating face binding,
            // owns the material normal (also for a native cylindrical bore).
            NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> material_faces;
            for(TopExp_Explorer solid(input,TopAbs_SOLID);solid.More();solid.Next())
                TopExp::MapShapes(solid.Current(),TopAbs_FACE,material_faces);
            const auto index=material_faces.FindIndex(found->shape);
            if(!index)throw std::runtime_error("Projection target must be a preceding bounded face in the active Body.");
            target.face=TopoDS::Face(material_faces.FindKey(index));
        }
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
    std::vector<TopoDS_Face> footprints;
    const auto append_profile=[&](const ExtrusionRequest& profile) {
        const auto wires=make_body_profiles(profile,request.sketch_normal);
        if(request.output==ProjectionResult::Relief) {
            std::vector<TopoDS_Wire> loops;
            for(const auto& wire:wires)loops.push_back(wire.wire);
            footprints.push_back(TopoDS::Face(profile_base(loops,false)));
        }
        for(const auto& wire:wires) {
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
    distinguish_partition_sides(faces,edges);
    // Several arcs of an existing circular junction can have the same adjacent
    // faces. Their endpoints must also retain the authored Sketch curves which
    // cut that junction; face ancestry alone cannot distinguish those arcs.
    std::vector<OwnedFace> boundary_supports;
    for(const auto& curve:propagate_topology(partition.history,curves.edges))
        boundary_supports.push_back({curve.shape,{curve.reference.owner_id,curve.reference.semantic_key,curve.reference.instance_path}});
    edges=complete_boolean_edges(partition.shape,faces,edges,owner,"projection",boundary_supports,vertices);
    if(request.output==ProjectionResult::Split) {
        PrimitiveData result;result.shape=partition.shape;result.faces=std::move(faces);
        result.edges=std::move(edges);result.vertices=std::move(vertices);return result;
    }
    std::vector<OwnedFace> retained;
    if(request.output==ProjectionResult::Relief) {
        // The partition already contains exact projected Sketch boundaries.
        // Classify its native patches against the filled source profile; a
        // user-picked seed must never choose the complement or omit a patch.
        Handle(IntTools_Context) context=new IntTools_Context;
        std::vector<std::unique_ptr<IntCurvesFace_ShapeIntersector>> rays;
        for(const auto& target:targets) {
            auto ray=std::make_unique<IntCurvesFace_ShapeIntersector>();
            ray->Load(target.face,request.tolerance);rays.push_back(std::move(ray));
        }
        const auto normal=sketch.Axis().Direction();
        const double denominator=direction.Dot(normal);
        if(std::abs(denominator)<Precision::Angular())
            throw std::runtime_error("Projection does not completely define a partition.");
        for(const auto& region:partition.regions) {
            gp_Pnt point;gp_Pnt2d uv;
            if(BOPTools_AlgoTools3D::PointInFace(region.face,point,uv,context)!=0)
                throw std::runtime_error("Projection face partition failed.");
            const double depth=gp_Vec(sketch.Location(),point).Dot(gp_Vec(normal))/denominator;
            const auto source=point.Translated(-depth*gp_Vec(direction));
            if(std::ranges::none_of(footprints,[&](const auto& footprint) {
                return BRepClass_FaceClassifier(footprint,source,request.tolerance).State()==TopAbs_IN;
            }))continue;
            // A periodic support also has a far side with the same planar
            // footprint. Preserve the nearest-hit contract of the projection.
            double nearest=std::numeric_limits<double>::infinity(),nearest_depth{};
            bool ambiguous=false;
            for(const auto& ray:rays) {
                ray->Perform(gp_Lin(source,direction),request.both_directions?-Precision::Infinite():-request.tolerance,Precision::Infinite());
                if(!ray->IsDone())continue;
                for(int i=1;i<=ray->NbPnt();++i) {
                    const double candidate=ray->WParameter(i),distance=std::abs(candidate);
                    if(distance<nearest-request.tolerance){nearest=distance;nearest_depth=candidate;ambiguous=false;}
                    else if(std::abs(distance-nearest)<=request.tolerance&&std::abs(candidate-nearest_depth)>request.tolerance)ambiguous=true;
                }
            }
            if(ambiguous)throw std::runtime_error("Projection curve ancestry is ambiguous.");
            if(!std::isfinite(nearest)||std::abs(nearest_depth-depth)>request.tolerance)continue;
            const auto named=std::ranges::find_if(faces,[&](const auto& face){return face.shape.IsSame(region.face);});
            const auto target=std::ranges::find_if(targets,[&](const auto& value){return value.target_key==region.target_key;});
            if(named==faces.end()||target==targets.end())throw std::runtime_error("Projection boundary ancestry is missing.");
            auto selected=*named;selected.shape.Orientation(target->face.Orientation());
            resolved_regions.push_back({owner,retained.size(),selected.reference.semantic_key});
            retained.push_back(std::move(selected));
        }
        if(retained.empty())throw std::runtime_error("Projection face partition failed.");
    } else for(std::size_t intent_index=0;intent_index<request.regions.size();++intent_index) {
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
                // The 3D face classifier can project onto the antipodal
                // stationary point of a sphere and select both complementary
                // regions. Classify the nearest support parameters explicitly.
                GeomAPI_ProjectPointOnSurf locate(seed,BRep_Tool::Surface(target->face),request.tolerance);
                if(!locate.IsDone()||!locate.NbPoints())throw std::runtime_error("The retained trim region changed. Select the region again.");
                double u{},v{};locate.LowerDistanceParameters(u,v);
                const auto state=BRepTopAdaptor_FClass2d(region.face,request.tolerance).Perform(gp_Pnt2d(u,v));
                if(state==TopAbs_ON)throw std::runtime_error("Select a retained region away from its boundary.");
                match=state==TopAbs_IN;
            }
            if(match) {
                if(selected)throw std::runtime_error("Surface trim retained region is ambiguous.");
                selected=*named;
                if(request.output==ProjectionResult::Relief)selected->shape.Orientation(target->face.Orientation());
            }
        }
        if(!selected)throw std::runtime_error("The retained trim region changed. Select the region again.");
        resolved_regions.push_back({owner,intent_index,selected->reference.semantic_key});
        if(std::ranges::none_of(retained,[&](const auto& face){return face.shape.IsSame(selected->shape);}))retained.push_back(*selected);
    }
    PrimitiveData result;TopoDS_Compound combined;builder.MakeCompound(combined);
    if(request.output==ProjectionResult::Relief) {
        // Publish exact derived regions for downstream references. They are
        // not additional display surfaces or user-selected material regions.
        TopoDS_Compound regions;builder.MakeCompound(regions);
        for(auto& face:retained) {face.reference.owner_id=owner;face.reference.instance_path.clear();face.reference.surface_result=true;builder.Add(regions,face.shape);}
        retained_geometry.shape=regions;retained_geometry.faces=retained;
        retained_geometry.edges=edges;retained_geometry.vertices=vertices;
        for(const auto& face:retained) {
            // The complementary periodic region still owns its parameter seam.
            // Join seam-divided boundary curves on the retained region alone,
            // preserving authored points and native history for the offset.
            // Unification can raise tolerances on reused seam vertices even
            // in safe mode. Isolate this offset preparation from the model.
            BRepBuilderAPI_Copy skin_copy(face.shape,true,false);
            NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> skin_members;
            TopExp::MapShapes(face.shape,skin_members);
            const auto copy_members=[&](const auto& source) {
                std::decay_t<decltype(source)> copied;
                for(const auto& item:source)if(skin_members.Contains(item.shape))
                    copied.push_back({skin_copy.ModifiedShape(item.shape),item.reference});
                return copied;
            };
            ShapeUpgrade_UnifySameDomain skin(skin_copy.Shape(),true,false,true);
            skin.SetSafeInputMode(true);skin.SetLinearTolerance(request.tolerance);
            for(const auto& point:original_vertices) {
                const auto& modified=partition.history->Modified(point.shape);
                for(const auto& child:modified)if(child.ShapeType()==TopAbs_VERTEX&&skin_members.Contains(child))skin.KeepShape(skin_copy.ModifiedShape(child));
                if(modified.IsEmpty()&&!partition.history->IsRemoved(point.shape)&&skin_members.Contains(point.shape))skin.KeepShape(skin_copy.ModifiedShape(point.shape));
            }
            skin.Build();
            if(skin.Shape().IsNull())throw std::runtime_error("Projection face partition failed.");
            auto local_edges=propagate_topology(skin.History(),copy_members(edges));
            auto local_vertices=propagate_topology(skin.History(),copy_members(vertices));
            // Unification's sampled tolerance has the same exact certification
            // requirement as the partition; no 3D boundary is moved.
            detail::certify_projection_face(TopoDS::Face(skin.Shape()),request.tolerance,true);
            SurfaceThickenRequest thickness;thickness.face=face.reference;
            thickness.thickness=request.height;thickness.tolerance=request.tolerance;
            thickness.side=request.inward?SurfaceThicknessSide::Second:SurfaceThicknessSide::First;
            auto solid=request.relief_direction==ProjectionReliefDirection::Projection?
                directional_relief(TopoDS::Face(skin.Shape()),face.reference,local_edges,local_vertices,direction,request,owner):
                make_thickened_surface_data(thickness,owner,skin.Shape(),{{skin.Shape(),face.reference}},local_edges,local_vertices).operand;
            if(result.shape.IsNull())result=std::move(solid);
            else {
                // Adjacent patches form one material tool. Passing touching
                // solids as an un-unioned compound can leave their common wall
                // in the final Body even though its total volume is correct.
                BRepAlgoAPI_Fuse join;set_boolean_inputs(join,result.shape,solid.shape);
                join.SetFuzzyValue(request.tolerance);join.Build();
                if(!join.IsDone()||join.HasErrors()||!BRepCheck_Analyzer(join.Shape()).IsValid())
                    throw std::runtime_error("Projection face partition failed.");
                result.faces=propagate_topology(join,result.faces,solid.faces);
                result.edges=propagate_topology(join,result.edges,solid.edges);
                result.vertices=propagate_topology(join,result.vertices,solid.vertices);
                result.shape=join.Shape();
            }
        }
        return result;
    }
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
