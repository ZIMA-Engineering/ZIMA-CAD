#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/workspace/feature_reference_input.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
document::Placement sketch_properties_placement(const document::HistoryContainer& owner) {
    auto result=owner.placement;
    if(owner.feature_kind==document::FeatureKind::Extrusion||owner.feature_kind==document::FeatureKind::Revolution) {
        if(owner.value_locks.contains("profile_offset"))result.value_locks.insert("profile_offset");
        else result.value_locks.erase("profile_offset");
    }
    return result;
}
bool commit_part_sketch_properties(Workspace& live,const kernel::OcctKernel& kernel,
    const std::string& id,sketcher::Sketch sketch,document::Placement placement,
    const std::optional<document::HistoryContainer>& new_container) {
    const auto reject=[](const char* code,const char* message){throw SketchOperationError(code,message);};
    auto* state=live.open_part(id);
    if(!state)reject("unsupported_document","Sketch operations require an open Part or Assembly.");
    const auto& before=state->session.document();
    const auto existing=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
    const bool create=existing==before.sketches.end();
    const auto* owner=before.find_container(sketch.owner_container_id);
    if(create){
        if(!new_container||new_container->feature_kind!=document::FeatureKind::Sketch||
            sketch.owner_container_id!=new_container->id||owner)
            reject("invalid_sketch_owner","A new Part Sketch must have its own Sketch container.");
    }else{
        if(existing->owner_container_id!=sketch.owner_container_id||!owner||new_container)
            reject("invalid_sketch_owner","Sketch owning container no longer exists");
        if(owner->feature_kind!=document::FeatureKind::Sketch&&owner->feature_kind!=document::FeatureKind::Extrusion&&
            owner->feature_kind!=document::FeatureKind::Revolution&&owner->feature_kind!=document::FeatureKind::Holes&&owner->feature_kind!=document::FeatureKind::Bend&&owner->feature_kind!=document::FeatureKind::Flat)
            reject("unsupported_sketch","Edit this Sketch through its owning section operation.");
    }
    const auto* body=create?before.body_history.find(before.body_history.active_body_id())
        :before.body_owner_for_object(sketch.owner_container_id);
    if(body&&body->derived_copy)reject("read_only_body","A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())
        reject("inactive_body","Activate the owning Body before editing its history.");
    document::validate_native_metadata_text(sketch.name);
    if(sketch.name.empty()||sketch.name.size()>1024)
        reject("invalid_name","A Sketch name must contain 1 to 1024 UTF-8 bytes.");
    if(!std::isfinite(sketch.plane_offset)||std::abs(sketch.plane_offset)>1000000)
        reject("invalid_arguments","The base Sketch offset is outside the supported range.");
    document::normalize_sketch_front_references(placement.references);
    if(sketch.plane_auto&&document::sketch_placement_uses_front_plane(placement.references))
        sketch.plane=sketcher::SketchPlane::XZ;
    sketch.validate();
    auto locks=owner?owner->value_locks:std::set<std::string>{};
    if(owner&&(owner->feature_kind==document::FeatureKind::Extrusion||owner->feature_kind==document::FeatureKind::Revolution)) {
        if(placement.value_locks.erase("profile_offset"))locks.insert("profile_offset");else locks.erase("profile_offset");
    }
    if(!create&&existing->serialized()==sketch.serialized()&&owner->placement==placement&&owner->value_locks==locks)return false;
    auto next=before;const auto owner_id=sketch.owner_container_id;
    if(create){
        auto container=*new_container;container.placement=std::move(placement);
        insert_new_sketch(next,std::move(sketch),std::move(container));
    }else{
        *std::ranges::find(next.sketches,sketch.id,&sketcher::Sketch::id)=std::move(sketch);
        auto* target=next.find_container(owner_id);
        target->placement=std::move(placement);target->value_locks=std::move(locks);
        const auto& saved=*std::ranges::find(next.sketches,existing->id,&sketcher::Sketch::id);
        if(target->feature_kind==document::FeatureKind::Extrusion)target->extrusion.profile_plane_offset=saved.plane_offset;
        if(target->feature_kind==document::FeatureKind::Revolution)target->revolution.profile_plane_offset=saved.plane_offset;
    }
    auto geometry=construction_reference_source_geometry(state->session.calculated_boundaries());
    append_reference_geometry(geometry,next.origin_viewer_mesh().original_references);
    append_reference_geometry(geometry,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(geometry);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(!create){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(owner_id);}
    auto calculated=calculate_part_with_resolved_references(kernel,next,&state->session.calculated_boundaries(),policy);
    if(!next.find_container(owner_id)->placement.reference_valid)
        reject("invalid_reference","The proposed feature placement references cannot be resolved.");
    commit_part_document(live,id,std::move(next),std::move(calculated));
    return true;
}

bool set_part_sketch_reference(Workspace& live,const kernel::OcctKernel& kernel,
    const std::string& id,const std::string& sketch_id,std::size_t index,
    document::ConstructionReference source) {
    auto sketch=document_sketch(live,id,sketch_id);
    auto feature=prepare_part_feature_reference(live,id,sketch.owner_container_id,index,std::move(source),index==0);
    document::normalize_sketch_front_references(feature.placement.references);
    sketch.plane_reference_owner_id.clear();
    return commit_part_sketch_properties(live,kernel,id,std::move(sketch),sketch_properties_placement(feature));
}
bool commit_sketch_properties(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    sketcher::Sketch sketch,document::Placement placement,const std::optional<document::HistoryContainer>& new_container) {
    if(live.open_part(id))return commit_part_sketch_properties(live,kernel,id,std::move(sketch),std::move(placement),new_container);
    const auto reject=[](const char* code,const char* message){throw SketchOperationError(code,message);};
    auto* state=live.open_assembly(id);
    if(!state)reject("unsupported_document","Sketch operations require an open Part or Assembly.");
    const auto& before=state->session.document();
    const auto existing=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
    const bool create=existing==before.sketches.end();
    if(!create&&(new_container||existing->owner_container_id!=sketch.owner_container_id))
        reject("invalid_sketch_owner","Sketch owning container no longer exists");
    document::validate_native_metadata_text(sketch.name);
    if(sketch.name.empty()||sketch.name.size()>1024)reject("invalid_name","A Sketch name must contain 1 to 1024 UTF-8 bytes.");
    if(!std::isfinite(sketch.plane_offset)||std::abs(sketch.plane_offset)>1000000)
        reject("invalid_arguments","The base Sketch offset is outside the supported range.");
    sketch.validate();document::normalize_sketch_front_references(placement.references);
    if(sketch.plane_auto&&document::sketch_placement_uses_front_plane(placement.references))
        sketch.plane=sketcher::SketchPlane::XZ;
    if(!create)if(const auto* cut=before.find_cut(sketch.owner_container_id)) {
        auto value=cut->definition;
        if(placement.value_locks.erase("profile_offset"))value.value_locks.insert("profile_offset");else value.value_locks.erase("profile_offset");
        value.placement=std::move(placement);
        if(value.feature_kind==document::FeatureKind::Extrusion)value.extrusion.profile_plane_offset=sketch.plane_offset;
        else value.revolution.profile_plane_offset=sketch.plane_offset;
        if(value==cut->definition&&sketch.serialized()==existing->serialized())return false;
        commit_assembly_profile(live,kernel,id,std::move(value),cut->target_occurrence_ids,ProfileEditMode::Replace,sketch);
        return true;
    }
    const auto* owner=before.find_sketch_container(sketch.owner_container_id);
    if(create){
        if(!new_container||owner||new_container->id!=sketch.owner_container_id)
            reject("invalid_sketch_owner","A standalone Assembly Sketch requires its own Sketch container.");
    } else {
        if(!owner)reject("invalid_sketch_owner","Sketch owning container no longer exists");
        if(sketch.serialized()==existing->serialized()&&placement==owner->placement)return false;
    }
    const auto owner_id=sketch.owner_container_id,sketch_id=sketch.id;
    const auto origin_id=create?new_container->container_origin.id:owner->container_origin.id;
    const auto entity_id=create?new_container->feature_id:owner->feature_id;
    for(const auto& ref:placement.references)if(ref.instance_path.empty()&&
        (ref.owner_id==owner_id||ref.owner_id==sketch_id||ref.owner_id==origin_id||ref.owner_id==entity_id))
        reject("reference_not_available","The original feature placement reference is unavailable.");
    auto next=before;
    if(create){auto container=*new_container;container.placement=std::move(placement);next.insert_sketch(std::move(sketch),std::move(container));}
    else {
        *std::ranges::find(next.sketches,sketch_id,&sketcher::Sketch::id)=std::move(sketch);
        auto* target=next.find_sketch_container(owner_id);target->placement=std::move(placement);
        target->name=std::ranges::find(next.sketches,sketch_id,&sketcher::Sketch::id)->name;
    }
    const auto edges=assembly_component_dependencies(next);
    for(const auto& [source,consumer]:edges)if(source==owner_id&&edges.contains({consumer,source}))
        reject("reference_cycle","The reference would create a dependency cycle.");
    next.resolve_constructions();
    if(!next.find_sketch_container(owner_id)->placement.reference_valid)
        reject("invalid_reference","The proposed feature placement references cannot be resolved.");
    next.validate_sketch_containers();
    state->session.commit(std::move(next));return true;
}
bool set_sketch_reference(Workspace& live,const kernel::OcctKernel& kernel,const std::string& id,
    const std::string& sketch_id,std::size_t index,document::ConstructionReference source) {
    if(live.open_part(id))return set_part_sketch_reference(live,kernel,id,sketch_id,index,std::move(source));
    auto sketch=document_sketch(live,id,sketch_id);
    const auto* assembly=live.open_assembly(id);
    if(!assembly)throw SketchOperationError("unsupported_document","Sketch operations require an open Part or Assembly.");
    auto feature=assembly->session.document().find_cut(sketch.owner_container_id)
        ?prepare_assembly_profile_reference(live,id,sketch.owner_container_id,index,std::move(source),index==0)
        :prepare_assembly_sketch_reference(live,id,sketch.owner_container_id,index,std::move(source));
    sketch.plane_reference_owner_id.clear();
    return commit_sketch_properties(live,kernel,id,std::move(sketch),sketch_properties_placement(feature));
}

}
