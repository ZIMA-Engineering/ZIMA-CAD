#include <zima/workspace/opening_operations.hpp>
#include <zima/workspace/hole_operations.hpp>
#include <zima/workspace/profile_operations.hpp>
#include <zima/workspace/part_transactions.hpp>
#include <zima/document/hole_profiles.hpp>
#include <algorithm>
#include <cmath>
namespace zima::workspace {
namespace {
void require_hole(const document::HistoryContainer& feature) {
    if(feature.feature_kind!=document::FeatureKind::Hole)
        throw HoleOperationError("wrong_feature","This container is not a native Hole.");
}
void validate(const document::HistoryContainer& feature) {
    require_hole(feature);
    for(const auto& [name,value]:hole_dimensions(feature)) {
        const bool angle=name=="drill_point_angle";
        const bool zero=name=="entrance_chamfer"||name=="exit_chamfer";
        if(!std::isfinite(value)||value<(zero?0:.001)||value>(angle?179.999:1000000))
            throw HoleOperationError("invalid_arguments","Hole dimension is outside the supported range.");
    }
    const auto& h=feature.hole;
    if(feature.combine_mode!=document::CombineMode::Subtract)
        throw HoleOperationError("invalid_arguments","A Hole removes material.");
    if(h.bore_end_condition==document::EndCondition::UpTo &&
        (h.bore_end_targets.size()!=1||!h.bore_end_targets.front().reference.valid()))
        throw HoleOperationError("missing_reference","Select a target plane or face for the Hole.");
    if(h.bore_end_condition!=document::EndCondition::Length && (h.drill_point_enabled||h.exit_chamfer_enabled))
        throw HoleOperationError("unsupported_end_condition","Hole end treatments require a fixed bore length.");
    if(h.thread_enabled&&h.thread_length>h.bore_length)
        throw HoleOperationError("invalid_arguments","Thread length cannot exceed the cylindrical bore length.");
    const auto check=[&](const std::string& text,const std::string& id) {
        auto sketch=sketcher::Sketch::from_serialized(text);
        if(sketch.id!=id||sketch.owner_container_id!=feature.id)
            throw HoleOperationError("identity_changed","Editing must preserve the Hole profile identities.");
        return sketch;
    };
    const auto bore=check(h.sketch_serialized,h.sketch_id);
    if(std::ranges::none_of(bore.circles,[&](const auto& c){return c.id==h.circle_id&&!c.construction&&bore.find_point(c.center_point_id);}))
        throw HoleOperationError("missing_profile","The Hole circle is missing.");
    const auto axial=[&](const auto& text,const auto& id,const auto& points,const auto& axis) {
        const auto sketch=check(text,id);
        for(const auto& point:points)if(!sketch.find_point(point))throw HoleOperationError("missing_profile","The Hole profile point is missing.");
        if(std::ranges::none_of(sketch.segments,[&](const auto& s){return s.id==axis&&s.construction&&s.centerline;}))
            throw HoleOperationError("missing_profile","The Hole profile axis is missing.");
    };
    axial(h.chamfer_sketch_serialized,h.chamfer_sketch_id,h.chamfer_point_ids,h.chamfer_axis_segment_id);
    axial(h.tip_sketch_serialized,h.tip_sketch_id,h.tip_point_ids,h.tip_axis_segment_id);
}
}
std::vector<std::pair<std::string,double>> hole_dimensions(const document::HistoryContainer& feature) {
    require_hole(feature);const auto& h=feature.hole;
    return {{"diameter",h.diameter},{"bore_length",h.bore_length},{"entrance_chamfer",h.entrance_chamfer},
        {"exit_chamfer",h.exit_chamfer},{"drill_point_angle",h.drill_point_angle_degrees},
        {"thread_diameter",h.thread_nominal_diameter},{"pitch",h.thread_pitch},{"thread_length",h.thread_length}};
}
bool commit_hole(Workspace& live,const kernel::OcctKernel& kernel,const std::string& document_id,
        document::HistoryContainer feature,HoleEditMode mode) {
    const auto id=document_id;auto* state=live.open_part(id);
    if(!state)throw HoleOperationError("unsupported_document","Hole operations require an open Part.");
    require_hole(feature);
    auto& h=feature.hole;
    h.thread_enabled=h.thread_enabled||h.type!=document::HoleType::Plain;
    if(h.drill_point_enabled)h.exit_chamfer_enabled=false;
    if(h.thread_end_condition==document::EndCondition::ThroughAll)h.thread_length=h.bore_length;
    validate(feature);
    document::update_hole_profiles(h);
    normalize_owned_profile_front_references(feature.placement.references);
    const auto& before=state->session.document();const auto* existing=before.find_container(feature.id);
    if(mode==HoleEditMode::Replace) {
        if(!existing)throw HoleOperationError("container_not_found","The requested container does not exist.");
        require_hole(*existing);const auto& old=existing->hole;
        if(existing->feature_id!=feature.feature_id||existing->feature_parent_id!=feature.feature_parent_id||existing->container_origin!=feature.container_origin||
            old.sketch_id!=h.sketch_id||old.circle_id!=h.circle_id||old.chamfer_sketch_id!=h.chamfer_sketch_id||old.tip_sketch_id!=h.tip_sketch_id||
            old.chamfer_point_ids!=h.chamfer_point_ids||old.tip_point_ids!=h.tip_point_ids||old.chamfer_axis_segment_id!=h.chamfer_axis_segment_id||old.tip_axis_segment_id!=h.tip_axis_segment_id)
            throw HoleOperationError("identity_changed","Editing must preserve the Hole profile identities.");
    } else if(existing||feature.id.empty()||feature.feature_id.empty())
        throw HoleOperationError("identity_changed","A new container must have a new nonempty identity.");
    const auto* body=existing?before.body_owner_for_object(feature.id):before.body_history.find(before.body_history.active_body_id());
    if(body&&body->derived_copy)throw HoleOperationError("read_only_body","A derived Body cannot be edited directly.");
    if(body&&body->scope.id!=before.body_history.active_body_id())throw HoleOperationError("inactive_body","Activate the owning Body before editing its Hole.");
    const auto container_id=feature.id;
    auto next=before;
    if(existing)*next.find_container(feature.id)=std::move(feature);
    else {next.insert_history_entry(document::PartHistoryKind::Feature,feature.id);next.history.push_back(std::move(feature));}
    const auto& previous=state->session.calculated_boundaries();
    auto& prepared=*next.find_container(container_id);
    if(prepared.hole.bore_end_condition==document::EndCondition::UpTo)try {
        auto& target=prepared.hole.bore_end_targets.front();
        target=prepare_opening_end_target(next,previous,prepared,target,false);
    }catch(const OpeningOperationError& error){throw HoleOperationError(error.code,error.what());}
    if(existing&&*existing==prepared)return false;
    auto references=construction_reference_source_geometry(previous);
    append_reference_geometry(references,next.origin_viewer_mesh().original_references);
    append_reference_geometry(references,next.construction_viewer_mesh().original_references);
    next.resolve_constructions(references);
    PartCalculationPolicy policy;policy.reject_errors=true;
    if(existing){policy.edited_document_id=id;policy.edited_history_limit=before.history_index(existing->id);}
    auto calculated=calculate_part_with_resolved_references(kernel,next,&previous,policy);
    static_cast<void>(refresh_sketch_external_references(next,calculated));
    commit_part_document(live,id,std::move(next),std::move(calculated));return true;
}
}
