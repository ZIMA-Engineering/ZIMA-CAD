#include <zima/document/sketch_placement.hpp>
#include <zima/workspace/feature_reference_input.hpp>
#include <zima/workspace/sketch_properties.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/metadata.hpp>
#include <algorithm>
#include <cmath>

namespace zima::workspace {
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
            owner->feature_kind!=document::FeatureKind::Revolution)
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
    sketch.validate();
    if(!create&&existing->serialized()==sketch.serialized()&&owner->placement==placement)return false;
    auto next=before;const auto owner_id=sketch.owner_container_id;
    if(create){
        auto container=*new_container;container.placement=std::move(placement);
        insert_new_sketch(next,std::move(sketch),std::move(container));
    }else{
        *std::ranges::find(next.sketches,sketch.id,&sketcher::Sketch::id)=std::move(sketch);
        auto* target=next.find_container(owner_id);
        target->placement=std::move(placement);
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
    if(index==0&&std::ranges::any_of(feature.placement.references,[](const auto& ref){return !ref.orientation_only&&ref.supports_offset;}))
        sketch.plane=sketcher::SketchPlane::XZ;
    sketch.plane_reference_owner_id.clear();
    return commit_part_sketch_properties(live,kernel,id,std::move(sketch),std::move(feature.placement));
}
}
