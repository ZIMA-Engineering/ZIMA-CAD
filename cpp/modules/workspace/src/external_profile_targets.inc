namespace {
void append_inverse_target_frame(std::vector<kernel::KernelRigidPlacement>& frames,
        const kernel::KernelRigidPlacement& frame) {
    // Inverse of the existing Rz*Ry*Rx frame, in application order.
    const auto t=frame.translation,r=frame.rotation_degrees;
    frames.push_back({{-t.x,-t.y,-t.z},{}});
    frames.push_back({{},{0,0,-r.z}});frames.push_back({{},{0,-r.y,0}});frames.push_back({{}, {-r.x,0,0}});
}
document::ExtrusionParameters::EndTarget resolve_cached_external_target(
        const document::PartDocument& part,const std::string& frame,
        const document::ExtrusionParameters::EndTarget& requested) {
    if(!requested.external_source||requested.kind==document::EndTargetKind::Point)
        throw ProfileOperationError("invalid_reference","The original extrusion target is unavailable.");
    const auto& binding=*requested.external_source;
    if(binding.document_id.empty()||binding.document_id==part.document_id||binding.source_file.empty()||
        binding.source_instance_path!=requested.reference.instance_path)
        throw ProfileOperationError("invalid_reference","The original extrusion target is unavailable.");
    const auto geometry=part.construction_reference_geometry_for(frame,binding.source_face->mesh.original_references);
    auto target=document::resolve_profile_target(requested,geometry);
    if(!target)throw ProfileOperationError("missing_reference","The original extrusion target is unavailable.");
    auto& resolved=*target->external_source;resolved.source_to_body=resolved.source_to_part;
    const auto* body=part.body_owner_for_object(frame);
    if(!body)body=part.body_history.find(frame);
    if(body) {
        const auto& p=body->scope.placement;
        append_inverse_target_frame(resolved.source_to_body,{{p.x,p.y,p.z},{p.rotation_x,p.rotation_y,p.rotation_z}});
    }
    return *target;
}
}

document::ExtrusionParameters::EndTarget prepare_skeleton_end_target(
        const Workspace& live,const document::PartDocument& part,const std::string& frame,
        const document::ExtrusionParameters::EndTarget& offered,const kernel::OcctKernel* capture) {
    auto requested=offered;
    for(unsigned depth=0;depth<256;++depth) {
        const auto parent=kernel::boolean_face_parent(requested.reference.semantic_key);if(!parent)break;
        requested.reference.owner_id=parent->first;requested.reference.semantic_key=parent->second;
    }
    auto result=requested;
    auto binding=requested.external_source.value_or(document::ExternalProfileTarget{});
    if(binding.context_assembly_id.empty()) {
        binding.context_assembly_id=live.displayed_document_id();
        binding.dependent_instance_path=live.active_occurrence_path();
        binding.source_instance_path=requested.reference.instance_path;
    }
    const auto dependent=assembly::InstancePath::decode(binding.dependent_instance_path);
    const auto source_path=assembly::InstancePath::decode(binding.source_instance_path);
    const auto dependent_address=live.resolve_occurrence(binding.context_assembly_id,dependent);
    const auto source_address=live.resolve_occurrence(binding.context_assembly_id,source_path);
    if(!dependent_address||dependent_address->source_document_id!=part.document_id||
        dependent_address->source_kind!=assembly::ComponentSourceKind::Part||
        !source_address||source_address->source_kind!=assembly::ComponentSourceKind::Part||
        binding.source_instance_path!=requested.reference.instance_path)
        throw ProfileOperationError("context_reference","Activate the exact Part occurrence that owns this external reference.");
    if(!binding.document_id.empty()&&binding.document_id!=source_address->source_document_id)
        throw ProfileOperationError("dependency_identity","The component source file belongs to a different document.");
    const auto file=live.occurrence_source_file(binding.context_assembly_id,source_path);
    if(!file||!assembly::is_skeleton_file(*file)||source_address->source_document_id==part.document_id||
        live.derived_source_path(binding.context_assembly_id,source_path)!=source_path)
        throw ProfileOperationError("invalid_reference_source","External extrusion targets must belong to a Skeleton Part.");
    binding.document_id=source_address->source_document_id;binding.source_file=*file;
    require_acyclic_document_dependency(live,part.document_id,binding.document_id,*file);
    const auto matches=[&](OriginalReferenceKind kind,const auto& owner,const auto& key,const auto& path) {
        return kind==OriginalReferenceKind::Face&&owner==requested.reference.owner_id&&
            key==requested.reference.semantic_key&&path==requested.reference.instance_path;
    };
    auto geometry=context_original_reference_geometry(live,binding.context_assembly_id,dependent,binding.document_id,matches);
    if(geometry.triangle_references.empty())throw ProfileOperationError("missing_reference","The original extrusion target is unavailable.");
    kernel::BodyResult cached;
    if(capture&&!document::profile_target_is_datum(requested.reference)) {
        const auto source=read_body_link_source(live,*file,binding.document_id);
        if(source.calculated.empty())throw ProfileOperationError("missing_reference","The original extrusion target is unavailable.");
        auto local=requested.reference;local.instance_path.clear();
        const auto* feature=source.document.find_container(local.owner_id);
        const auto* body=source.document.body_owner_for_object(local.owner_id);
        if(!body)body=source.document.body_history.find(local.owner_id);
        const bool boolean_owner=std::ranges::any_of(source.document.body_history.booleans(),[&](const auto& operation) {
            return operation.id==local.owner_id&&!operation.suppressed;
        });
        if((!feature&&!body&&!boolean_owner)||(feature&&feature->suppressed)||(body&&body->suppressed)||
            source.calculated.back().calculation_errors.contains(local.owner_id))
            throw ProfileOperationError("missing_reference","The original extrusion target is unavailable.");
        cached=capture->capture_reference_face_history(source.calculated,local);
    }
    // Viewer samples are already in the dependent Part frame. Native BRep
    // remains in the source Part frame and consumes the explicit frame chain.
    cached.mesh.original_references=std::move(geometry);binding.source_face=std::move(cached);
    binding.source_to_part=live.occurrence_placements(binding.context_assembly_id,source_path);
    std::ranges::reverse(binding.source_to_part);
    for(const auto& placement:live.occurrence_placements(binding.context_assembly_id,dependent))
        append_inverse_target_frame(binding.source_to_part,placement);
    result.external_source=std::move(binding);
    return resolve_cached_external_target(part,frame,result);
}

void refresh_external_profile_targets(const Workspace& live,const kernel::OcctKernel& kernel,document::PartDocument& part) {
    for(auto& feature:part.history)if(!feature.suppressed&&feature.feature_kind==document::FeatureKind::Feature&&
        feature.feature.type==document::FeatureType::Modeling)
        for(std::size_t index=0;index<(feature.feature.symmetric?1u:2u);++index) {
            auto& side=feature.feature.sides[index];
            if(side.operation!=document::FeatureSideOperation::Extrusion||side.extrusion_extent!=document::EndCondition::UpTo)continue;
            for(auto& target:side.targets)if(target.external_source)
                target=prepare_skeleton_end_target(live,part,feature.id,target,&kernel);
        }
}
void refresh_cached_external_profile_target_frames(document::PartDocument& part) {
    for(auto& feature:part.history)if(!feature.suppressed&&feature.feature_kind==document::FeatureKind::Feature)
        for(auto& side:feature.feature.sides)for(auto& target:side.targets)if(target.external_source)
            target=resolve_cached_external_target(part,feature.id,target);
}
