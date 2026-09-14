#include <zima/workspace/feature_reference_input.hpp>
#include <zima/workspace/history_policy.hpp>
#include <zima/document/placement_reference_assignment.hpp>
#include <algorithm>
#include <cmath>
namespace zima::workspace {
namespace {
using Ref=document::ConstructionReference;
void reject(const char* code,const char* message){throw PlacementEditError(code,message);}
std::vector<Ref> combined(const std::vector<Ref>& position,const std::vector<Ref>& orientation,std::optional<std::size_t> skip={}) {
    std::vector<Ref> result;
    for(std::size_t i=0;i<position.size();++i)if(skip!=i&&(!position[i].owner_id.empty()||!position[i].semantic_key.empty()))result.push_back(position[i]);
    for(std::size_t i=0;i<orientation.size();++i)if(skip!=i+3&&(!orientation[i].owner_id.empty()||!orientation[i].semantic_key.empty()))result.push_back(orientation[i]);
    return result;
}
void check_source(const document::PartDocument& doc,const std::string& target,const Ref& ref) {
    if(ref.owner_id==doc.document_id+":origin")return;
    const auto* target_body=doc.body_owner_for_object(target);
    const auto* source_body=doc.body_owner_for_object(ref.owner_id);
    if(target_body&&source_body) {
        const auto& order=doc.body_history.order();
        const auto source=std::ranges::find(order,source_body->scope.id),destination=std::ranges::find(order,target_body->scope.id);
        if(source>destination)reject("reference_not_available","A feature reference must precede it in model history.");
        if(ref.owner_id==source_body->origin().id||source<destination)return;
    }
    const auto graph=part_history_dependency_graph(doc);const auto owner=graph.owners.find(ref.owner_id);
    if(owner==graph.owners.end())reject("reference_not_available","A feature reference must precede it in model history.");
    const auto source=std::ranges::find(doc.history_order,owner->second,&document::PartHistoryEntry::id);
    const auto destination=std::ranges::find(doc.history_order,target,&document::PartHistoryEntry::id);
    if(source==doc.history_order.end()||destination==doc.history_order.end()||source>=destination)
        reject("reference_not_available","A feature reference must precede it in model history.");
}
}
document::HistoryContainer prepare_part_feature_reference(Workspace& live,const std::string& id,
    const std::string& container,std::size_t index,Ref source,bool derive_orientation) {
    if(index>4||source.owner_id.empty()||source.semantic_key.empty()||!source.instance_path.empty()||!std::isfinite(source.offset))
        reject("invalid_arguments","A feature reference requires a local source, a slot from 0 to 4 and a finite offset.");
    const auto* state=live.open_part(id);if(!state)reject("unsupported_document","Profile operations require an open Part.");
    const auto& before=state->session.document();const auto* existing=before.find_container(container);
    if(!existing)reject("container_not_found","The requested container does not exist.");
    if(const auto* body=before.body_owner_for_object(container)) {
        if(body->derived_copy)reject("read_only_body","A derived Body cannot be edited directly.");
        if(body->scope.id!=before.body_history.active_body_id())reject("inactive_body","Activate the owning Body before editing its placement.");
    }
    check_source(before,container,source);
    auto geometry=part_construction_dimension_geometry(before,state->session.calculated_boundaries());
    append_reference_geometry(geometry,before.history_origin_reference_geometry_before(container));
    geometry=before.construction_reference_geometry_for(container,std::move(geometry));
    const auto matches=[&](const auto& item){return item.reference.owner_id==source.owner_id&&item.reference.semantic_key==source.semantic_key&&item.reference.instance_path.empty();};
    const bool point=std::ranges::any_of(geometry.points,matches),axis=std::ranges::any_of(geometry.axes,matches),edge=std::ranges::any_of(geometry.edges,matches);
    const bool face=std::ranges::any_of(geometry.triangle_references,[&](const auto& ref){return ref.owner_id==source.owner_id&&ref.semantic_key==source.semantic_key&&ref.instance_path.empty();});
    if(!point&&!axis&&!edge&&!face)reject("reference_not_found","The original feature placement reference is unavailable.");
    if((index>=3||!face)&&source.offset!=0)reject("parameter_not_editable","The placement parameter is unknown, constrained or locked.");
    source.supports_offset=face;source.orientation_only=false;source.orientation_role="none";source.orientation_drives_rotation=false;source.measured_offset.reset();
    auto value=*existing;std::vector<Ref> position,orientation(2);std::array<bool,3> empty_locks{};
    for(const auto& ref:value.placement.references) {
        if(ref.orientation_only&&ref.orientation_role!="direction") {
            const bool second=ref.orientation_role=="top"||ref.orientation_role=="bottom"||ref.orientation_role=="left"||ref.orientation_role=="right";
            orientation[second?1:0]=ref;
        }else position.push_back(ref);
    }
    if(position.size()>3)reject("invalid_placement","The feature has too many position reference rows.");
    auto baseline=value.placement;baseline.references=combined(position,orientation,index);
    if(!document::resolve_placement(baseline,geometry))reject("invalid_placement","Existing feature placement references cannot be resolved.");
    const kernel::Vec3 origin{baseline.x,baseline.y,baseline.z};
    const auto translation=document::point_constraint_remaining_dof(baseline.references,geometry);
    const auto rotation=document::orientation_constraint_remaining_dof(baseline.references,geometry,true,origin);
    const bool direction=index<3&&translation==0&&rotation>0;
    if(index>=3||direction) {
        source.orientation_drives_rotation=true;source.orientation_role=direction?"direction":index==3?"front":"top";
        source.orientation_only=direction;if(direction)source.supports_offset=false;
    }else if(face||axis||edge) {
        // Reassigning a source already present in FRONT/TOP keeps that role.
        // Choosing the next role would make one plane define both FRONT and TOP.
        const auto retained=std::ranges::find_if(baseline.references,[&](const auto& ref){return ref.orientation_drives_rotation&&
            ref.owner_id==source.owner_id&&ref.semantic_key==source.semantic_key&&ref.instance_path==source.instance_path;});
        if(retained!=baseline.references.end())source.orientation_role=retained->orientation_role;
        else {
            std::set<std::string> used;for(const auto& ref:baseline.references)if(ref.orientation_drives_rotation)used.insert(ref.orientation_role);
            source.orientation_role=!used.contains("front")?"front":!used.contains("top")?"top":"none";
        }
        source.orientation_drives_rotation=source.orientation_role!="none";
    }
    source.measured_offset=document::measure_placement_reference_offset(source,geometry,origin);
    const auto assigned=document::assign_placement_reference({position,orientation,empty_locks},true,index,std::move(source),derive_orientation);
    using Error=document::PlacementReferenceError;
    if(assigned.error==Error::Duplicate)reject("duplicate_reference","The same placement reference cannot be assigned twice.");
    if(assigned.error==Error::MissingMeasuredOffset)reject("invalid_reference","The current distance from the placement reference cannot be measured.");
    if(assigned.error!=Error::None)reject("invalid_arguments","The placement reference slot is unavailable.");
    value.placement.references=combined(position,orientation);
    for(auto& ref:value.placement.references)ref.measured_offset.reset();
    if(!document::resolve_placement(value.placement,geometry))reject("invalid_reference","The proposed feature placement references cannot be resolved.");
    return value;
}
}
