namespace solid_state_replay {
double volume(const TopoDS_Shape& shape) {
    // Containment compares independently trimmed faces. Fixed quadrature can
    // differ by micrometres cubed even for the same cylinder; use adaptive
    // integration for this proof, without loosening the geometry tolerance.
    GProp_GProps properties;
    const double error=BRepGProp::VolumeProperties(shape,properties,1e-10);
    if(!std::isfinite(error)||error<0||error>1e-8||!std::isfinite(properties.Mass()))
        throw std::runtime_error("Solid state calculation is incomplete.");
    return std::abs(properties.Mass());
}
TopoDS_Shape common(const TopoDS_Shape& a,const TopoDS_Shape& b,double tolerance) {
    BRepAlgoAPI_Common operation;set_boolean_inputs(operation,a,b);
    operation.SetFuzzyValue(tolerance);operation.Build();
    if(!operation.IsDone())throw std::runtime_error("Solid state calculation is incomplete.");
    return operation.Shape();
}
bool no_material_difference(const TopoDS_Shape& a,const TopoDS_Shape& b,double tolerance) {
    BRepAlgoAPI_Cut operation;set_boolean_inputs(operation,a,b);
    operation.SetFuzzyValue(tolerance);operation.Build();
    if(!operation.IsDone())throw std::runtime_error("Solid state calculation is incomplete.");
    return volume(operation.Shape())<=1e-10;
}
ExtrusionRequest moved(ExtrusionRequest request,const gp_Trsf& transform) {
    solid_straightening::transform_loop(request.outer_profile,transform);
    for(auto& loop:request.inner_profiles)solid_straightening::transform_loop(loop,transform);
    for(auto& region:request.additional_profile_regions) {
        solid_straightening::transform_loop(region.outer_profile,transform);
        for(auto& loop:region.inner_profiles)solid_straightening::transform_loop(loop,transform);
    }
    const auto d=request.direction;
    request.direction=solid_straightening::vector(gp_Vec(d.x,d.y,d.z).Transformed(transform).XYZ());
    request.centerlines.origin=solid_straightening::vector(solid_straightening::point(request.centerlines.origin).Transformed(transform).XYZ());
    const auto n=request.centerlines.normal;
    request.centerlines.normal=solid_straightening::vector(gp_Vec(n.x,n.y,n.z).Transformed(transform).XYZ());
    return request;
}
RevolutionRequest moved(RevolutionRequest request,const gp_Trsf& transform) {
    solid_straightening::transform_loop(request.outer_profile,transform);
    for(auto& loop:request.inner_profiles)solid_straightening::transform_loop(loop,transform);
    for(auto& region:request.additional_profile_regions) {
        solid_straightening::transform_loop(region.outer_profile,transform);
        for(auto& loop:region.inner_profiles)solid_straightening::transform_loop(loop,transform);
    }
    const auto position=[&](Vec3& p){p=solid_straightening::vector(solid_straightening::point(p).Transformed(transform).XYZ());};
    const auto direction=[&](Vec3& p){p=solid_straightening::vector(gp_Vec(p.x,p.y,p.z).Transformed(transform).XYZ());};
    position(request.axis_point);direction(request.axis_direction);direction(request.profile_normal);
    position(request.centerlines.origin);direction(request.centerlines.normal);
    if(request.end_limit) {
        position(request.end_limit->origin);direction(request.end_limit->normal);
        for(auto& p:request.end_limit->triangles)position(p);
    }
    return request;
}
Sweep3DRequest moved(Sweep3DRequest request,const gp_Trsf& transform) {
    const auto position=[&](Vec3& p){p=solid_straightening::vector(solid_straightening::point(p).Transformed(transform).XYZ());};
    const auto direction=[&](Vec3& p){p=solid_straightening::vector(gp_Vec(p.x,p.y,p.z).Transformed(transform).XYZ());};
    if(request.twist&&request.twist->axis_point)position(*request.twist->axis_point);
    if(request.twist){position(request.twist->centerlines.origin);direction(request.twist->centerlines.normal);}
    if(request.profile_references) {
        for(auto& axis:request.profile_references->axes){position(axis.point);direction(axis.direction);}
        for(auto& point:request.profile_references->points)position(point.position);
        for(auto& edge:request.profile_references->edges) {
            for(auto& point:edge.points)position(point);
            if(edge.exact_spline)for(auto& point:edge.exact_spline->poles)position(point);
        }
    }
    for(auto& p:request.path_points)position(p);
    for(auto& segment:request.path_segments) {
        position(segment.start);position(segment.end);
        for(auto& p:segment.bezier_control_points)position(p);
        if(segment.arc_midpoint)position(*segment.arc_midpoint);
        for(auto& span:segment.bezier_spans)for(auto& p:span)position(p);
    }
    for(auto& section:request.sections) {
        solid_straightening::transform_loop(section.profile.outer_profile,transform);
        for(auto& loop:section.profile.inner_profiles)solid_straightening::transform_loop(loop,transform);
        direction(section.profile_normal);
        if(section.circle_radial_direction)direction(*section.circle_radial_direction);
    }
    if(request.rotation_axis) {position(request.rotation_axis->point);direction(request.rotation_axis->direction);}
    return request;
}
// Only finite plain extrusion cutters have an unambiguous authored tool here.
// Limit-driven openings must first be resolved by the document calculation.
ExtrusionRequest transfer_cut(const HistoryOperation& operation,std::size_t index,
        const std::vector<HistoryOperation>& authored,const std::vector<SolidStateChange>& changes,
        const SolidSourceStates& targets,const OcctKernel& kernel) {
    const auto unsupported=[](){throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");};
    const auto* cut=std::get_if<ExtrusionRequest>(&operation.primitive);
    if(!cut || cut->surface_result || cut->extent!=ExtrusionRequest::Extent::Blind ||
       cut->reverse_limit || cut->through_all_reverse || cut->draft_angle_degrees!=0 || cut->sheet_cut)unsupported();
    for(std::size_t i=0;i<index;++i)if(!authored[i].suppressed &&
        authored[i].operation==BooleanOperation::Add && !solid_state_candidate(authored[i]) &&
        !std::holds_alternative<FilletRequest>(authored[i].primitive))unsupported();
    const auto tool=make_extrusion_data(*cut,operation.owner_id).shape;
    const auto preceding=solid_states_before(authored,changes,index);
    std::optional<ExtrusionRequest> result;
    std::string affected_owner;
    const double tolerance=operation.boolean_tolerance;
    for(const auto& [owner,before]:preceding) {
        const auto& after=targets.at(owner);
        auto source=authored[before.source_index];source.body={};
        std::vector<solid_straightening::StraightPortion> portions;
        if(const auto* sweep=std::get_if<Sweep3DRequest>(&source.primitive);sweep&&!sweep->transported) {
            const auto plan=solid_straightening::ordinary_sweep(*sweep,before.coefficient,&portions);
            if(before.straight)source.primitive=plan.primitive;
        } else if(before.straight) {
            if(const auto* revolution=std::get_if<RevolutionRequest>(&source.primitive))
                source.primitive=kernel.prepare_straightening(*revolution,before.coefficient).primitive;
            else source.primitive=kernel.prepare_straightening(std::get<Sweep3DRequest>(source.primitive),before.coefficient).primitive;
        }
        const auto source_result=kernel.evaluate_history({source});
        const auto removed=common(tool,read_kernel_shape(source_result.back()),tolerance);
        const double removed_volume=volume(removed);
        if(removed_volume<=1e-10)continue;
        if(result)unsupported(); // One cutter across independently moving sources is ambiguous.
        if(before.straight==after.straight&&before.coefficient==after.coefficient) {result=*cut;affected_owner=owner;continue;}
        for(const auto& portion:portions) {
            auto region=portion.region;
            gp_Trsf current_frame;
            if(before.straight) {
                current_frame.SetTranslation(portion.tangent*(portion.developed_start*before.coefficient));
                const auto d=region.direction;region.direction={d.x*before.coefficient,d.y*before.coefficient,d.z*before.coefficient};
            } else current_frame=portion.from_formed.Inverted();
            region=moved(std::move(region),current_frame);
            const auto region_shape=make_extrusion_data(region,"region").shape;
            const double inside=volume(common(removed,region_shape,tolerance));
            if(std::abs(inside-removed_volume)>std::max(1e-8,removed_volume*1e-8))continue;
            // The complete removed volume belongs to this straight portion.
            // Move a rigid cutter by its material station; never scale its bore.
            GProp_GProps properties;BRepGProp::VolumeProperties(tool,properties,1e-10);
            const auto center=properties.CentreOfMass();
            const auto local=center.Transformed(current_frame.Inverted());
            const double along=gp_Vec(portion.origin,local).Dot(portion.tangent);
            const double material_station=along/(before.straight?before.coefficient:1.);
            if(material_station<0 || material_station>portion.length)unsupported();
            auto target_local=local.Translated(portion.tangent*(material_station*(after.straight?after.coefficient:1.)-along));
            gp_Trsf target_frame;
            if(after.straight)target_frame.SetTranslation(portion.tangent*(portion.developed_start*after.coefficient));
            else target_frame=portion.from_formed.Inverted();
            const auto target_center=target_local.Transformed(target_frame);
            auto movement=target_frame*current_frame.Inverted();
            movement.SetTranslationPart(gp_Vec(center.Transformed(movement),target_center)+movement.TranslationPart());
            auto candidate=moved(*cut,movement);
            // A shortening must not push part of the retained hole across the
            // new segment boundary. Test the moved removed material, not center.
            auto target_region=portion.region;
            if(after.straight) {
                const auto d=target_region.direction;target_region.direction={d.x*after.coefficient,d.y*after.coefficient,d.z*after.coefficient};
            }
            target_region=moved(std::move(target_region),target_frame);
            const auto moved_removed=BRepBuilderAPI_Transform(removed,movement,true).Shape();
            const auto target_shape=make_extrusion_data(target_region,"region").shape;
            if(std::abs(volume(common(moved_removed,target_shape,tolerance))-removed_volume)>
                std::max(1e-8,removed_volume*1e-8))unsupported();
            // The moved tool must not introduce a second cut elsewhere on a
            // looping source. Equal retained-hole volume alone is insufficient.
            auto target_source=authored[after.source_index];target_source.body={};
            if(after.straight)target_source.primitive=kernel.prepare_straightening(
                std::get<Sweep3DRequest>(target_source.primitive),after.coefficient).primitive;
            const auto target_body=kernel.evaluate_history({target_source});
            const auto candidate_tool=make_extrusion_data(candidate,operation.owner_id).shape;
            const auto actual_removed=common(candidate_tool,read_kernel_shape(target_body.back()),tolerance);
            const double actual_volume=volume(actual_removed);
            const double shared_volume=volume(common(actual_removed,moved_removed,tolerance));
            if(std::abs(actual_volume-removed_volume)>std::max(1e-8,removed_volume*1e-8) ||
               std::abs(shared_volume-removed_volume)>std::max(1e-8,removed_volume*1e-8)) {
                // Independently calculated Sweep faces can differ within their
                // existing Boolean tolerance while describing the same bore.
                // Prove both geometric differences empty at that same tolerance;
                // never accept an additional cut by relaxing the volume budget.
                if(!no_material_difference(actual_removed,moved_removed,tolerance)||
                   !no_material_difference(moved_removed,actual_removed,tolerance))unsupported();
            }
            if(result)unsupported();
            result=std::move(candidate);affected_owner=owner;
        }
        if(!result)unsupported();
    }
    if(!result)unsupported();
    const auto transferred_tool=make_extrusion_data(*result,operation.owner_id).shape;
    for(const auto& [owner,after]:targets)if(owner!=affected_owner && after.source_index<index) {
        auto target=authored[after.source_index];target.body={};
        if(after.straight) {
            if(const auto* revolution=std::get_if<RevolutionRequest>(&target.primitive))
                target.primitive=kernel.prepare_straightening(*revolution,after.coefficient).primitive;
            else target.primitive=kernel.prepare_straightening(std::get<Sweep3DRequest>(target.primitive),after.coefficient).primitive;
        }
        const auto body=kernel.evaluate_history({target});
        if(volume(common(transferred_tool,read_kernel_shape(body.back()),tolerance))>1e-10)unsupported();
    }
    return *result;
}
// Rebuild from authored operations at every state boundary. Prior state rows
// contribute intent, not frozen geometry. The ordinary kernel re-applies the
// retained treatments, including ones authored after an earlier state.
std::vector<HistoryOperation> prepare(const std::vector<HistoryOperation>& history,
        std::size_t boundary,const OcctKernel& kernel) {
    std::vector<HistoryOperation> authored;
    std::vector<SolidStateChange> changes;
    std::set<std::string> state_owners;
    for(std::size_t i=0;i<=boundary;++i) {
        const auto& operation=history.at(i);
        if(const auto* state=std::get_if<SolidStateRequest>(&operation.primitive)) {
            if(operation.operation!=BooleanOperation::Add)
                throw std::invalid_argument("Solid state requires an additive history operation.");
            changes.push_back({authored.size(),operation.owner_id,operation.body.id,
                state->restore,state->all,operation.suppressed,state->coefficient,state->owners});
            state_owners.insert(operation.owner_id);
        } else authored.push_back(operation);
    }
    const auto states=solid_states_before(authored,changes,authored.size());
    auto replay=authored;
    std::map<std::pair<std::size_t,double>,SolidStraighteningPlan> plans;
    const auto plan_for=[&](std::size_t index,double coefficient)->const SolidStraighteningPlan& {
        const auto key=std::pair{index,coefficient};
        if(const auto found=plans.find(key);found!=plans.end())return found->second;
        const PrimitiveRequest* input=&authored.at(index).primitive;
        if(const auto& overrides=history.at(boundary).solid_state_placements)
            for(const auto& replacement:*overrides)if(replacement.owner_id==authored.at(index).owner_id)input=&replacement.primitive;
        auto plan=std::visit([&](const auto& request)->SolidStraighteningPlan {
            using T=std::decay_t<decltype(request)>;
            if constexpr(std::is_same_v<T,RevolutionRequest>||std::is_same_v<T,Sweep3DRequest>||std::is_same_v<T,FeatureGroupRequest>)
                return kernel.prepare_straightening(request,coefficient);
            else throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
        },*input);
        return plans.emplace(key,std::move(plan)).first->second;
    };
    std::set<std::string> resolved_placements;
    if(const auto& overrides=history.at(boundary).solid_state_placements) {
        for(const auto& replacement:*overrides) {
            const auto found=std::ranges::find_if(replay,[&](const auto& item){return item.owner_id==replacement.owner_id;});
            if(found==replay.end()||replacement.solid_state_placements||
               std::holds_alternative<SolidStateRequest>(replacement.primitive)||
               replacement.operation!=found->operation||!resolved_placements.insert(replacement.owner_id).second)
                throw std::invalid_argument("History reference view is invalid.");
            found->primitive=replacement.primitive;
            found->input_error=replacement.input_error;
        }
    }
    for(const auto& [owner,state]:states)if(state.straight) {
        replay[state.source_index].primitive=plan_for(state.source_index,state.coefficient).primitive;
    }
    // Carry already-calculated contour coordinates, including in-plane rotation.
    // This is a material operation, not another invocation of placement solving.
    struct CarriedEnd {gp_Trsf movement;gp_Pnt center;gp_Vec tangent;};
    std::map<std::string,CarriedEnd> carried_ends;
    // A curved continuation has its own state timeline. Retain the movement of
    // its input frame separately from its formed/straight end-section change.
    std::map<std::string,gp_Trsf> carried_sources;
    for(const auto& transfer:history.at(boundary).solid_state_face_transfers) {
        const auto found=std::ranges::find(replay,transfer.owner_id,&HistoryOperation::owner_id);
        if(found==replay.end()||!resolved_placements.insert(transfer.owner_id).second)
            throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
        const auto frame=[](const std::array<Vec3,4>& value) {
            for(const auto& vector:value)if(!std::isfinite(vector.x+vector.y+vector.z))
                throw std::invalid_argument("Solid state calculation is incomplete.");
            gp_Trsf result;const auto& o=value[0];const auto& x=value[1];const auto& y=value[2];const auto& z=value[3];
            result.SetValues(x.x,y.x,z.x,o.x,x.y,y.y,z.y,o.y,x.z,y.z,z.z,o.z);return result;
        };
        gp_Trsf movement;gp_Pnt old_center;gp_Vec old_tangent;
        if(const auto parent=carried_ends.find(transfer.source_owner_id);parent!=carried_ends.end()) {
            movement=parent->second.movement;old_center=parent->second.center;old_tangent=parent->second.tangent;
        } else {
            const auto before_states=solid_states_before(authored,changes,static_cast<std::size_t>(found-replay.begin()));
            const auto before=before_states.find(transfer.source_owner_id),after=states.find(transfer.source_owner_id);
            if(before==before_states.end()||after==states.end())
                throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
            const auto& old_plan=plan_for(before->second.source_index,before->second.coefficient);
            const auto& new_plan=plan_for(after->second.source_index,after->second.coefficient);
            const auto end_frame=[&](const SolidSourceState& state,const SolidStraighteningPlan& plan) {
                if(!state.straight)return frame(plan.end_transform);
                gp_Trsf result;result.SetTranslation(gp_Vec(solid_straightening::point(plan.source_start_centroid),
                    solid_straightening::point(plan.straight_end_centroid)));return result;
            };
            const auto old_frame=end_frame(before->second,old_plan);
            movement=end_frame(after->second,new_plan)*old_frame.Inverted();
            if(const auto parent=carried_sources.find(transfer.source_owner_id);parent!=carried_sources.end())
                movement=parent->second*movement;
            old_center=solid_straightening::point(before->second.straight?old_plan.straight_end_centroid:old_plan.source_end_centroid);
            const auto t=old_plan.start_tangent;
            old_tangent=gp_Vec(t.x,t.y,t.z).Transformed(old_frame).Normalized();
        }
        if(const auto source=states.find(transfer.owner_id);source!=states.end()) {
            const auto& plan=plan_for(source->second.source_index,source->second.coefficient);
            const auto t=plan.start_tangent;
            if(solid_straightening::point(plan.source_start_centroid).Distance(old_center)>1e-7||
                gp_Vec(t.x,t.y,t.z).Normalized().Dot(old_tangent)<1.-1e-9)
                throw std::invalid_argument("Straightening requires coincident profile centroids and tangent-continuous directions.");
            std::visit([&](auto& request) {
                using T=std::decay_t<decltype(request)>;
                if constexpr(std::is_same_v<T,ExtrusionRequest>||std::is_same_v<T,RevolutionRequest>||std::is_same_v<T,Sweep3DRequest>)
                    request=moved(std::move(request),movement);
                else throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
            },found->primitive);
            carried_sources.emplace(transfer.owner_id,movement);
            continue;
        }
        std::optional<CarriedEnd> next_end;
        bool ambiguous_end=false;
        const auto move_extrusion=[&](ExtrusionRequest& request) {
            if(request.extent!=ExtrusionRequest::Extent::Blind||request.reverse_limit||request.through_all_reverse||
                request.sheet_cut||request.surface_result)
                throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
            const auto d=request.direction;const gp_Vec direction(d.x,d.y,d.z);
            if(direction.Magnitude()<Precision::Confusion()||direction.Normalized().Dot(old_tangent)<1.-1e-9||request.start_offset!=0)
                throw std::invalid_argument("Straightening requires coincident profile centroids and tangent-continuous directions.");
            // Measure the actual filled section, including voids and thin walls.
            // No swept body, Boolean, or tessellation is needed for this check.
            GProp_GProps area;
            const auto measure=[&](const ExtrusionRequest& profile) {
                std::vector<TopoDS_Wire> wires;
                for(const auto& wire:make_body_profiles(profile,request.direction))wires.push_back(wire.wire);
                GProp_GProps properties;BRepGProp::SurfaceProperties(profile_base(wires,false),properties);area.Add(properties);
            };
            measure(request);
            for(const auto& region:request.additional_profile_regions) {
                auto profile=request;profile.additional_profile_regions.clear();
                profile.outer_profile=region.outer_profile;profile.inner_profiles=region.inner_profiles;
                profile.outer_edge_source_ids=region.outer_edge_source_ids;profile.outer_vertex_source_ids=region.outer_vertex_source_ids;
                profile.inner_edge_source_ids=region.inner_edge_source_ids;profile.inner_vertex_source_ids=region.inner_vertex_source_ids;
                measure(profile);
            }
            if(!std::isfinite(area.Mass())||area.Mass()<=0||area.CentreOfMass().Distance(old_center)>1e-7)
                throw std::invalid_argument("Straightening requires coincident profile centroids and tangent-continuous directions.");
            const CarriedEnd end{movement,area.CentreOfMass().Translated(direction),direction.Normalized()};
            // Draft may move the end-section centroid relative to this start.
            // Carrying the feature is rigid, but extending a chain needs that
            // separate end-section calculation and must not guess its center.
            if(request.draft_angle_degrees!=0)ambiguous_end=true;
            if(next_end&&(next_end->center.Distance(end.center)>1e-7||next_end->tangent.Dot(end.tangent)<1.-1e-9))ambiguous_end=true;
            else next_end=end;
            request=moved(std::move(request),movement);
        };
        if(auto* request=std::get_if<ExtrusionRequest>(&found->primitive))move_extrusion(*request);
        else if(auto* group=std::get_if<FeatureGroupRequest>(&found->primitive);group&&!group->children.empty()) {
            for(auto& child:group->children) {
                auto* request=std::get_if<ExtrusionRequest>(&child);
                if(!request)throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
                move_extrusion(*request);
            }
            const auto position=[&](Vec3& p){p=solid_straightening::vector(solid_straightening::point(p).Transformed(movement).XYZ());};
            const auto direction=[&](Vec3& p){p=solid_straightening::vector(gp_Vec(p.x,p.y,p.z).Transformed(movement).XYZ());};
            for(auto& axis:group->axes){position(axis.point);direction(axis.direction);}
            for(auto& point:group->reference_points)position(point.position);
            for(auto& plane:group->reference_planes) {
                for(auto& p:plane.corners)position(p);
                if(plane.reference.surface) {
                    auto surface=std::make_shared<SurfaceGeometry>(*plane.reference.surface);
                    position(surface->origin);direction(surface->axis);direction(surface->radial);plane.reference.surface=std::move(surface);
                }
            }
            for(auto& profile:group->reference_profiles)std::visit([&](auto& request) {
                using T=std::decay_t<decltype(request)>;
                if constexpr(std::is_same_v<T,ExtrusionRequest>)request=moved(std::move(request),movement);
                else request=moved(std::move(request),movement);
            },profile);
        } else throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
        if(next_end&&!ambiguous_end)carried_ends.emplace(transfer.owner_id,*next_end);
    }
    const auto restore_parent=[&](auto& reference) {
        std::size_t count=0;
        while(state_owners.contains(reference.owner_id)) {
            const auto parent=solid_state_parent(reference.semantic_key);
            if(!parent || ++count>state_owners.size())
                throw std::invalid_argument("A solid state reference has no unique correspondence.");
            reference.owner_id=parent->first;reference.semantic_key=parent->second;
        }
    };
    for(std::size_t index=0;index<replay.size();++index) {
        auto& operation=replay[index];
        if(operation.suppressed)continue;
        // Do not silently carry a cutter to the wrong segment. Whole-cutter
        // classification and placement-aware replay are separate acceptance gates.
        if(operation.operation==BooleanOperation::Subtract) {
            const auto before=solid_states_before(authored,changes,index);
            const bool unchanged=std::ranges::all_of(before,[&](const auto& item) {
                const auto& target=states.at(item.first);
                return item.second.straight==target.straight&&
                    (!target.straight||item.second.coefficient==target.coefficient);
            });
            if(!unchanged&&!resolved_placements.contains(operation.owner_id))
                operation.primitive=transfer_cut(authored[index],index,authored,changes,states,kernel);
            continue;
        }
        if(operation.feature_copy ||
           operation.sheet_operation!=SheetOperation::None ||
           std::holds_alternative<SheetStateRequest>(operation.primitive))
            throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
        if(auto* fillet=std::get_if<FilletRequest>(&operation.primitive)) {
            for(auto& edge:fillet->edges)restore_parent(edge);
            for(auto& vertex:fillet->contour_start_vertices)restore_parent(vertex);
        } else if(std::holds_alternative<ChamferRequest>(operation.primitive) ||
                  std::holds_alternative<ShellRequest>(operation.primitive) ||
                  std::holds_alternative<DrillPointRequest>(operation.primitive))
            throw std::invalid_argument("Solid state replay cannot yet preserve this operation.");
    }
    return replay;
}
template<class Reference> void own(Reference& reference,const std::string& owner) {
    if(!reference.valid())throw std::invalid_argument("A solid state reference has no unique correspondence.");
    if constexpr(requires{reference.display_owner_id;})
        if(reference.display_owner_id.empty())reference.display_owner_id=reference.owner_id;
    reference.semantic_key=solid_state_child_key(reference.owner_id,reference.semantic_key);
    reference.owner_id=owner;
}
void publish_display(BodyResult& boundary,const std::string& owner,ViewerReferenceGeometry& persisted) {
    const auto& packet=*boundary.solid_state_reference_views.at(owner);
    std::set<std::string> sources;
    for(const auto& ref:packet.triangle_references)sources.insert(ref.owner_id);
    for(const auto& edge:packet.edges)sources.insert(edge.reference.owner_id);
    for(const auto& point:packet.points)sources.insert(point.reference.owner_id);
    for(const auto& axis:packet.axes)sources.insert(axis.reference.owner_id);
    const auto replaced=[&](const auto& reference) {
        auto source=reference.owner_id,key=reference.semantic_key;
        while(const auto parent=solid_state_parent(key)){source=parent->first;key=parent->second;}
        return sources.contains(source);
    };
    // Like Unbend, publish separate state-owned datums and retain immutable
    // authored references. The replay packet already contains their calculated
    // positions, including carried cuts and attached features.
    std::erase_if(boundary.mesh.axes,[&](const auto& axis){return replaced(axis.reference);});
    for(auto axis:packet.axes) {
        own(axis.reference,owner);
        if(std::ranges::none_of(persisted.axes,[&](const auto& value){return value.reference==axis.reference;}))
            persisted.axes.push_back(axis);
        boundary.mesh.axes.push_back(std::move(axis));
    }
    const auto centerline=[](std::string key) {
        while(const auto parent=solid_state_parent(key))key=parent->second;
        return key.starts_with("centerline:from:");
    };
    std::erase_if(boundary.mesh.edges,[&](const auto& edge){return centerline(edge.reference.semantic_key)&&replaced(edge.reference);});
    for(const auto& source:packet.edges)if(centerline(source.reference.semantic_key)) {
        auto edge=source;
        own(edge.reference,owner);
        if(std::ranges::none_of(persisted.edges,[&](const auto& value){return value.reference==edge.reference;}))
            persisted.edges.push_back(edge);
        boundary.mesh.edges.push_back(std::move(edge));
    }
    const auto endpoint=[](std::string key) {
        while(const auto parent=solid_state_parent(key))key=parent->second;
        return key.starts_with("sweep:path-point:")||key.starts_with("profile:path-point:")||key.starts_with("helical:axis-point:");
    };
    std::erase_if(boundary.mesh.points,[&](const auto& point){return endpoint(point.reference.semantic_key)&&replaced(point.reference);});
    for(const auto& source:packet.points)if(endpoint(source.reference.semantic_key)) {
        auto point=source;
        own(point.reference,owner);
        if(std::ranges::none_of(persisted.points,[&](const auto& value){return value.reference==point.reference;}))
            persisted.points.push_back(point);
        boundary.mesh.points.push_back(std::move(point));
    }
}
} // namespace solid_state_replay
